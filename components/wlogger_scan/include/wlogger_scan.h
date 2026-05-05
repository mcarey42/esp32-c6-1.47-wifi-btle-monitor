#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wlogger_types.h"
#include "esp_err.h"

esp_err_t wlogger_scan_init(QueueHandle_t detect_q);
esp_err_t wlogger_scan_start_task(void);
bool      wlogger_scan_wifi_ok(void);
bool      wlogger_scan_ble_ok(void);
