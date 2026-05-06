#include "wlogger_scan.h"
#include "wlogger_scan_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "scan";
static bool s_wifi_ok = false, s_ble_ok = false;

bool wlogger_scan_wifi_ok(void) { return s_wifi_ok; }
bool wlogger_scan_ble_ok(void)  { return s_ble_ok; }

esp_err_t wlogger_scan_init(QueueHandle_t q) {
    g_detect_q = q;
#ifdef CONFIG_WLOGGER_FAKE_RADIOS
    if (wlogger_scan_fake_init() == ESP_OK) { s_wifi_ok = s_ble_ok = true; return ESP_OK; }
    return ESP_FAIL;
#else
    if (wlogger_scan_wifi_init() == ESP_OK) s_wifi_ok = true;
    else ESP_LOGW(TAG, "wifi init failed");
    if (wlogger_scan_ble_init()  == ESP_OK) s_ble_ok  = true;
    else ESP_LOGW(TAG, "ble init failed");
    if (!s_wifi_ok && !s_ble_ok) return ESP_FAIL;
    return ESP_OK;
#endif
}

static void scan_task(void *_) {
    (void)_;
    for (;;) {
#ifdef CONFIG_WLOGGER_FAKE_RADIOS
        wlogger_scan_fake_burst(6500);
        vTaskDelay(pdMS_TO_TICKS(100));
        wlogger_scan_fake_burst(2000);
        vTaskDelay(pdMS_TO_TICKS(100));
#else
        if (s_wifi_ok) wlogger_scan_wifi_sweep();
        vTaskDelay(pdMS_TO_TICKS(1000));   // radio rest, lets the chip cool between phases
        if (s_ble_ok)  wlogger_scan_ble_window(CONFIG_WLOGGER_BLE_WINDOW_MS);
        vTaskDelay(pdMS_TO_TICKS(1000));   // and again before the next Wi-Fi sweep
#endif
    }
}

esp_err_t wlogger_scan_start_task(void) {
    // 8 KB stack — Wi-Fi/BLE callbacks chain into deep IDF stacks.
    BaseType_t r = xTaskCreate(scan_task, "scan", 8192, NULL, 5, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
