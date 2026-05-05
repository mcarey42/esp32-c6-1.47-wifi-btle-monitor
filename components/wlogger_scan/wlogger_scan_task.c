#include "wlogger_scan.h"
#include "wlogger_scan_internal.h"
esp_err_t wlogger_scan_init(QueueHandle_t q) { g_detect_q = q; return 0; }
esp_err_t wlogger_scan_start_task(void) { return 0; }
bool wlogger_scan_wifi_ok(void) { return false; }
bool wlogger_scan_ble_ok(void) { return false; }
