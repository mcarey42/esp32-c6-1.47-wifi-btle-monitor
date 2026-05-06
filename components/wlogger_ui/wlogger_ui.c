#include "wlogger_ui.h"
#include "page_internal.h"
#include "wlogger_lcd.h"
#include "wlogger_button.h"
#include "wlogger_led.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UI_TICK_MS 100

static stats_t      *s_stats;
static recent_q_t   *s_recent;
static lv_obj_t     *s_tabview;
static ui_page_t     s_pages[4];
static int           s_active = 0;

esp_err_t wlogger_ui_init(stats_t *stats, recent_q_t *recent) {
    s_stats = stats; s_recent = recent;
    if (!stats->sd_ok) {
        lv_obj_t *bg = lv_obj_create(lv_screen_active());
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x600000), 0);
        lv_obj_t *l = lv_label_create(bg);
        lv_label_set_text(l, "NO SD CARD\n\nINSERT AND\nREBOOT");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
        return ESP_OK;
    }
    s_tabview = lv_tabview_create(lv_screen_active());
    lv_tabview_set_tab_bar_size(s_tabview, 0);

    lv_obj_t *t1 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t2 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t3 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t4 = lv_tabview_add_tab(s_tabview, "");
    s_pages[0] = page_stats_create(t1);
    s_pages[1] = page_feed_create(t2);
    s_pages[2] = page_bars_create(t3);
    s_pages[3] = page_status_create(t4);
    return ESP_OK;
}

static void ui_task(void *_) {
    (void)_;
    uint32_t bars_last = 0, status_last = 0;
    for (;;) {
        wlogger_lcd_tick_ms(UI_TICK_MS);
        lv_timer_handler();

        btn_event_t be = wlogger_button_poll(UI_TICK_MS);
        if (be == BTN_SHORT && s_tabview) {
            s_active = (s_active + 1) % 4;
            lv_tabview_set_active(s_tabview, s_active, LV_ANIM_OFF);
        } else if (be == BTN_LONG) {
            wlogger_writer_request_rotate();
            wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_ROTATED);
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        ui_page_t *p = &s_pages[s_active];
        bool render = false;
        if (s_active == 0) render = true;
        if (s_active == 1) render = true;
        if (s_active == 2 && now - bars_last >= 500) { bars_last = now; render = true; }
        if (s_active == 3 && now - status_last >= 1000) { status_last = now; render = true; }
        if (render && p->update) p->update(s_stats, s_recent);

        static uint32_t led_last = 0;
        if (now - led_last >= 1000) {
            led_last = now;
            led_phase_t ph = (((now / 1000) % 9) < 7) ? LED_PHASE_WIFI : LED_PHASE_BLE;
            uint16_t rate = stats_rate_last_minute(s_stats);
            led_event_t evt = LED_EVT_NONE;
            if (!s_stats->sd_ok) evt = LED_EVT_FAULT;
            else if (s_stats->dropped_events > 0 &&
                     (s_stats->dropped_events % 10) == 0)
                evt = LED_EVT_DROP;
            wlogger_led_set(ph, rate, evt);
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

esp_err_t wlogger_ui_start_task(void) {
    BaseType_t r = xTaskCreate(ui_task, "ui", 6144, NULL, 3, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
