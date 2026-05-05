#include "wlogger_scan_internal.h"
#include "wlogger_parsers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "scan-ble";
static EventGroupHandle_t s_evg = NULL;
#define BIT_DONE BIT0

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        if (s_evg) xEventGroupSetBits(s_evg, BIT_DONE);
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .type    = DET_BLE,
        .rssi    = event->disc.rssi,
        .channel = 37,
        .auth    = event->disc.addr.type,
    };
    memcpy(d.mac, event->disc.addr.val, 6);
    d.mac_random = (event->disc.addr.type == 1);
    parse_adv_data(event->disc.data, event->disc.length_data,
                   d.name, sizeof d.name, &d.mfg_id);

    if (g_detect_q) xQueueSend(g_detect_q, &d, 0);
    return 0;
}

static void on_sync(void) {}
static void host_task(void *p) { (void)p; nimble_port_run(); nimble_port_freertos_deinit(); }

esp_err_t wlogger_scan_ble_init(void) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init: %d", err); return err; }
    ble_hs_cfg.sync_cb = on_sync;
    s_evg = xEventGroupCreate();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void wlogger_scan_ble_window(uint32_t dur_ms) {
    if (!s_evg || !ble_hs_synced()) return;
    uint8_t addr_type = 0;
    ble_hs_id_infer_auto(0, &addr_type);
    struct ble_gap_disc_params p = {
        .itvl   = 0x0010,
        .window = 0x0010,
        .filter_policy = 0,
        .limited = 0, .passive = 0,
        .filter_duplicates = 0,
    };
    xEventGroupClearBits(s_evg, BIT_DONE);
    int rc = ble_gap_disc(addr_type, dur_ms, &p, gap_event, NULL);
    if (rc != 0) { ESP_LOGW(TAG, "ble_gap_disc: %d", rc); return; }
    xEventGroupWaitBits(s_evg, BIT_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(dur_ms + 500));
    ble_gap_disc_cancel();
}
