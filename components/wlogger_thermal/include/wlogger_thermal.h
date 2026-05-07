#pragma once
#include "esp_err.h"
#include <stdint.h>

// Thermal bands. The exact thresholds (in degrees C) live in wlogger_thermal.c;
// the band number can be displayed on the UI for at-a-glance status.
typedef enum {
    THERMAL_COOL     = 0,   // < warm threshold, no throttle
    THERMAL_WARM     = 1,   // first throttle band
    THERMAL_HOT      = 2,   // heavier throttle
    THERMAL_CRITICAL = 3,   // emergency throttle
    THERMAL_UNKNOWN  = 4,   // sensor not yet read (sentinel)
} thermal_band_t;

esp_err_t wlogger_thermal_init(void);
esp_err_t wlogger_thermal_start_task(void);

// Latest temperature reading, in degrees C. Returns -127 before the first
// successful read, or if the sensor is unavailable.
int8_t wlogger_thermal_celsius(void);

// Extra rest delay (ms) the scan loop should add per cycle. Updated by the
// thermal task as the temperature crosses band thresholds. Returns 0 when
// the board is in the COOL band.
uint32_t wlogger_thermal_extra_delay_ms(void);

// Current band — for UI display.
thermal_band_t wlogger_thermal_band(void);
