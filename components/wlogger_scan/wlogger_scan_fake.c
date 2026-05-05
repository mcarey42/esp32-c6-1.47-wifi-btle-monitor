#include "wlogger_scan_internal.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>

esp_err_t wlogger_scan_fake_init(void) { return ESP_OK; }

void wlogger_scan_fake_burst(uint32_t ms) {
    uint32_t until = (uint32_t)(esp_timer_get_time() / 1000) + ms;
    while ((uint32_t)(esp_timer_get_time() / 1000) < until) {
        detection_t d = { .t_sec = (uint32_t)(esp_timer_get_time() / 1000000) };
        uint32_t r = esp_random();
        d.type    = (r & 1) ? DET_BLE : ((r & 2) ? DET_WIFI_PROBE : DET_WIFI_AP);
        d.rssi    = -30 - (int8_t)(esp_random() % 60);
        d.channel = (d.type == DET_BLE) ? 37 + (esp_random() % 3) : 1 + (esp_random() % 13);
        for (int i = 0; i < 6; ++i) d.mac[i] = (uint8_t)(esp_random() & 0xff);
        d.mac_random = (d.mac[0] & 0x02) != 0;
        const char *names[] = { "", "home-net", "guest", "AirPods", "Tile", "Mi Band", "" };
        strncpy(d.name, names[esp_random() % 7], sizeof d.name - 1);
        d.mfg_id = (esp_random() & 1) ? 0x004C : 0;

        if (g_detect_q) xQueueSend(g_detect_q, &d, 0);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
