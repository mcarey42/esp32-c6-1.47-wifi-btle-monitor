#include "wlogger_scan_internal.h"
#include "wlogger_parsers.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "scan-wifi";
QueueHandle_t g_detect_q = NULL;

static void IRAM_ATTR promisc_cb(void *buf, wifi_promiscuous_pkt_type_t t) {
    if (t != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) return;
    uint16_t fc = p[0] | (p[1] << 8);
    uint8_t  st = (fc >> 4) & 0xF;

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .rssi    = pkt->rx_ctrl.rssi,
        .channel = pkt->rx_ctrl.channel,
    };

    if (st == 8) {
        d.type = DET_WIFI_AP;
        memcpy(d.mac, p + 16, 6);
        if (pkt->rx_ctrl.sig_len > 36)
            parse_ssid(p + 36, pkt->rx_ctrl.sig_len - 36, d.name, sizeof d.name);
    } else if (st == 4) {
        d.type = DET_WIFI_PROBE;
        memcpy(d.mac, p + 10, 6);
        if (pkt->rx_ctrl.sig_len > 24)
            parse_ssid(p + 24, pkt->rx_ctrl.sig_len - 24, d.name, sizeof d.name);
    } else {
        return;
    }
    d.mac_random = (d.mac[0] & 0x02) != 0;

    if (g_detect_q) {
        BaseType_t hp = pdFALSE;
        xQueueSendFromISR(g_detect_q, &d, &hp);
        portYIELD_FROM_ISR(hp);
    }
    (void)TAG;
}

esp_err_t wlogger_scan_wifi_init(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "init: %s", esp_err_to_name(err)); return err; }
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promisc_cb));
    wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&f));
    return ESP_OK;
}

void wlogger_scan_wifi_sweep(void) {
    esp_wifi_set_promiscuous(true);
    for (int ch = 1; ch <= 13; ++ch) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WLOGGER_WIFI_DWELL_MS));
    }
    esp_wifi_set_promiscuous(false);
}
