#include "wlogger_lcd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
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

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int x1 = area->x1, y1 = area->y1, x2 = area->x2 + 1, y2 = area->y2 + 1;
    esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, px);
    lv_display_flush_ready(disp);
}

esp_err_t wlogger_lcd_init(void) {
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(PIN_BL, 1);

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
        .trans_queue_depth = 10,
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
    return ESP_OK;
}

lv_display_t *wlogger_lcd_lv_display(void) { return s_disp; }
void wlogger_lcd_tick_ms(uint32_t ms) { lv_tick_inc(ms); }
