#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wlogger_types.h"

extern QueueHandle_t g_detect_q;

esp_err_t wlogger_scan_wifi_init(void);
void      wlogger_scan_wifi_sweep(void);

esp_err_t wlogger_scan_ble_init(void);
void      wlogger_scan_ble_window(uint32_t ms);

esp_err_t wlogger_scan_fake_init(void);
void      wlogger_scan_fake_burst(uint32_t ms);
