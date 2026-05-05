#include "wlogger_scan_internal.h"
esp_err_t wlogger_scan_fake_init(void) { return 0; }
void wlogger_scan_fake_burst(uint32_t ms) { (void)ms; }
