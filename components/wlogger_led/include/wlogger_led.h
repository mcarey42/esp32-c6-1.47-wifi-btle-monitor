#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef enum { LED_PHASE_WIFI = 0, LED_PHASE_BLE = 1 } led_phase_t;
typedef enum {
    LED_EVT_NONE = 0,
    LED_EVT_DROP,
    LED_EVT_ROTATED,
    LED_EVT_LOW_MEM,
    LED_EVT_FAULT,
} led_event_t;

esp_err_t wlogger_led_init(void);
void wlogger_led_set(led_phase_t phase, uint16_t rate_per_min, led_event_t evt);
