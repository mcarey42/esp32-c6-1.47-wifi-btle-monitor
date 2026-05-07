#include "wlogger_lcd.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "lcd";
#define PIN_BL    22
#define PIN_RST   21
#define PIN_DC    15
#define PIN_CS    14
#define PIN_MISO   5    // shared with SD
#define PIN_MOSI   6    // shared with SD
#define PIN_SCLK   7    // shared with SD
// LCD owns the SPI2 bus init because it needs the larger max_transfer_sz
// (LVGL partial-buffer flushes are ~14 KB). SD attaches as a second device
// and tolerates the bus already being initialized.
#define HOST_ID  SPI2_HOST

#define X_GAP    34
#define Y_GAP     0

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static lv_display_t *s_disp = NULL;
static lv_color_t s_buf1[WLOG_LCD_W * 40];
static lv_color_t s_buf2[WLOG_LCD_W * 40];

// Bus arbitration. The IDF SPI master driver's bus mutex doesn't reliably
// serialize across the SDSPI host's polling-mode transactions and the
// esp_lcd panel-io's queued DMA — they can collide on the hardware-level
// "running command" register. We add an application-level mutex that
// every SPI2 user must hold for the full duration of their work.
static SemaphoreHandle_t s_bus_mtx       = NULL;
// Binary semaphore signaled by on_color_trans_done; lets lvgl_flush_cb
// block until the LCD transaction is genuinely complete.
static SemaphoreHandle_t s_trans_done_sem = NULL;

void wlogger_lcd_bus_lock(void) {
    if (s_bus_mtx) xSemaphoreTake(s_bus_mtx, portMAX_DELAY);
}
void wlogger_lcd_bus_unlock(void) {
    if (s_bus_mtx) xSemaphoreGive(s_bus_mtx);
}

// Called from the esp_lcd driver task when one panel SPI transaction
// completes. Returns whether a higher-priority task was woken — letting
// FreeRTOS yield correctly when called in ISR-style contexts.
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx) {
    (void)panel_io; (void)edata; (void)user_ctx;
    BaseType_t hp_woken = pdFALSE;
    if (s_trans_done_sem) xSemaphoreGiveFromISR(s_trans_done_sem, &hp_woken);
    return hp_woken == pdTRUE;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int x1 = area->x1, y1 = area->y1, x2 = area->x2 + 1, y2 = area->y2 + 1;

    wlogger_lcd_bus_lock();
    esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, px);
    // Block until the panel-io reports the transaction has actually
    // completed on the hardware. Only then is it safe to let go of
    // the bus and tell LVGL it can submit another flush.
    xSemaphoreTake(s_trans_done_sem, portMAX_DELAY);
    wlogger_lcd_bus_unlock();

    lv_display_flush_ready(disp);
}

esp_err_t wlogger_lcd_init(void) {
    s_bus_mtx        = xSemaphoreCreateMutex();
    s_trans_done_sem = xSemaphoreCreateBinary();
    if (!s_bus_mtx || !s_trans_done_sem) return ESP_ERR_NO_MEM;

    // LEDC PWM backlight @ 80% duty (5 kHz, 10-bit). Lower duty = cooler board.
    ledc_timer_config_t lt = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&lt);
    ledc_channel_config_t lc = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PIN_BL,
        .duty       = 819,    // 80% of 1024
        .hpoint     = 0,
    };
    ledc_channel_config(&lc);

    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO, .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    esp_err_t bus_err = spi_bus_initialize(HOST_ID, &bus, SPI_DMA_CH_AUTO);
    if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(bus_err));
        return bus_err;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num     = PIN_CS,
        .dc_gpio_num     = PIN_DC,
        .spi_mode        = 0,
        .pclk_hz         = 40 * 1000 * 1000,
        // queue depth 1 — only one LCD transaction outstanding on the bus at
        // a time, so the SPI-master driver's bus mutex can cleanly arbitrate
        // between LCD and SDSPI without overlapping HW commands.
        .trans_queue_depth = 1,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    esp_err_t err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HOST_ID, &io_cfg, &s_io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel_io_spi: %s", esp_err_to_name(err)); return err; }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel_st7789: %s", esp_err_to_name(err)); return err; }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, X_GAP, Y_GAP);
    esp_lcd_panel_disp_on_off(s_panel, true);

    lv_init();
    s_disp = lv_display_create(WLOG_LCD_W, WLOG_LCD_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof s_buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);

    // Hook the panel-io transaction-complete event so we only let LVGL
    // submit the next flush after the SPI bus is genuinely idle.
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_trans_done };
    esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, s_disp);
    return ESP_OK;
}

lv_display_t *wlogger_lcd_lv_display(void) { return s_disp; }
void wlogger_lcd_tick_ms(uint32_t ms) { lv_tick_inc(ms); }
