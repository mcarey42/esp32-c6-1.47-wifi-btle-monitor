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
        if (be == BTN_SHORT) {
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

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

esp_err_t wlogger_ui_start_task(void) {
    BaseType_t r = xTaskCreate(ui_task, "ui", 6144, NULL, 3, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
