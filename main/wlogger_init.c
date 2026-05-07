#include "wlogger_init.h"
#include "wlogger_sd.h"
#include "wlogger_lcd.h"
#include "wlogger_led.h"
#include "wlogger_button.h"
#include "wlogger_stats.h"
#include "wlogger_writer.h"
#include "wlogger_scan.h"
#include "wlogger_thermal.h"
#include "wlogger_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "init";
static stats_t       g_stats;
static recent_q_t    g_recent;
static QueueHandle_t g_detect_q;

esp_err_t wlogger_bringup(void) {
    // Default event loop is required by esp_wifi for internal event delivery
    // (e.g. WIFI_EVENT_HOME_CHANNEL_CHANGE). Without it the Wi-Fi stack
    // logs "failed to post WiFi event" repeatedly.
    esp_err_t evloop = esp_event_loop_create_default();
    if (evloop != ESP_OK && evloop != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(evloop));
    }

    stats_init(&g_stats);
    recent_q_init(&g_recent);
    g_detect_q = xQueueCreate(256, sizeof(detection_t));
    if (!g_detect_q) return ESP_ERR_NO_MEM;

    bool led_ok = (wlogger_led_init() == ESP_OK);
    (void)led_ok;
    if (wlogger_lcd_init() != ESP_OK) ESP_LOGW(TAG, "LCD init failed; running headless");
    wlogger_button_init();

    if (wlogger_sd_mount() != ESP_OK) {
        g_stats.sd_ok = false;
        wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_FAULT);
        wlogger_ui_init(&g_stats, &g_recent);
        wlogger_ui_start_task();
        return ESP_ERR_NOT_FOUND;
    }
    g_stats.sd_ok = true;

    wlogger_writer_init(g_detect_q, &g_stats, &g_recent);
    wlogger_writer_start_task();

    // Start the thermal task BEFORE scan_task — scan_task reads
    // wlogger_thermal_extra_delay_ms() each cycle; if thermal isn't
    // running yet that's fine (returns 0), but having it up first
    // means scan starts at the correct cadence from cycle 1.
    wlogger_thermal_init();
    wlogger_thermal_start_task();

    if (wlogger_scan_init(g_detect_q) != ESP_OK) {
        ESP_LOGE(TAG, "all radios failed");
        wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_FAULT);
    } else {
        g_stats.wifi_ok = wlogger_scan_wifi_ok();
        g_stats.ble_ok  = wlogger_scan_ble_ok();
        wlogger_scan_start_task();
    }

    wlogger_ui_init(&g_stats, &g_recent);
    wlogger_ui_start_task();
    return ESP_OK;
}
