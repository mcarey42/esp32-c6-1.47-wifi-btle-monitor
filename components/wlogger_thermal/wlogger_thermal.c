#include "wlogger_thermal.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

static const char *TAG = "thermal";

// Bands. Edit here to retune.
#define WARM_C   60
#define HOT_C    75
#define CRIT_C   85

// Hysteresis: only step *down* a band once we drop 3 °C below the entry
// threshold, so the throttle doesn't oscillate at the boundary.
#define HYST_C    3

// Per-band extra delay added to the existing 1 s rest periods between scan
// phases. Cool=0 → no extra. Critical=8 s → effectively pauses scanning.
static const uint32_t band_extra_ms[] = {
    [THERMAL_COOL]     = 0,
    [THERMAL_WARM]     = 1000,
    [THERMAL_HOT]      = 3000,
    [THERMAL_CRITICAL] = 8000,
    [THERMAL_UNKNOWN]  = 0,
};

static _Atomic int8_t   s_temp_c   = -127;
static _Atomic uint32_t s_extra_ms = 0;
static _Atomic int      s_band     = THERMAL_UNKNOWN;

int8_t wlogger_thermal_celsius(void)         { return atomic_load(&s_temp_c); }
uint32_t wlogger_thermal_extra_delay_ms(void) { return atomic_load(&s_extra_ms); }
thermal_band_t wlogger_thermal_band(void)     { return (thermal_band_t)atomic_load(&s_band); }

static thermal_band_t classify(int c, thermal_band_t prev) {
    // Step up immediately on threshold; step down only after dropping
    // HYST_C below the band's entry temp.
    if (c >= CRIT_C) return THERMAL_CRITICAL;
    if (c >= HOT_C)  return THERMAL_HOT;
    if (c >= WARM_C) return THERMAL_WARM;

    switch (prev) {
        case THERMAL_CRITICAL: return c < CRIT_C - HYST_C ? THERMAL_HOT      : prev;
        case THERMAL_HOT:      return c < HOT_C  - HYST_C ? THERMAL_WARM     : prev;
        case THERMAL_WARM:     return c < WARM_C - HYST_C ? THERMAL_COOL     : prev;
        default:               return THERMAL_COOL;
    }
}

static void thermal_task(void *_) {
    (void)_;
    temperature_sensor_handle_t h = NULL;
    // C6's actual calibrated ranges (per IDF v6.0 esp_hal_ana_conv/esp32c6):
    //   (-40, 20) (-30, 50) (-10, 80) (20, 100) (50, 125)
    // (20, 100) covers our band space (warm 60 → critical 85) with
    // ±2 C accuracy. The board is wall-powered indoors and will never
    // sit below 20 C, so clamping the floor to 20 is acceptable.
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    if (temperature_sensor_install(&cfg, &h) != ESP_OK) {
        ESP_LOGW(TAG, "temperature_sensor_install failed; thermal task exiting");
        atomic_store(&s_band, THERMAL_UNKNOWN);
        vTaskDelete(NULL);
    }
    if (temperature_sensor_enable(h) != ESP_OK) {
        ESP_LOGW(TAG, "temperature_sensor_enable failed; thermal task exiting");
        temperature_sensor_uninstall(h);
        vTaskDelete(NULL);
    }

    thermal_band_t prev = THERMAL_COOL;
    for (;;) {
        float celsius;
        if (temperature_sensor_get_celsius(h, &celsius) == ESP_OK) {
            int c = (int)(celsius + 0.5f);
            if (c >  127) c =  127;
            if (c < -127) c = -127;
            atomic_store(&s_temp_c, (int8_t)c);

            thermal_band_t band = classify(c, prev);
            if (band != prev) {
                ESP_LOGI(TAG, "band %d -> %d at %d C", prev, band, c);
                prev = band;
            }
            atomic_store(&s_band, (int)band);
            atomic_store(&s_extra_ms, band_extra_ms[band]);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t wlogger_thermal_init(void) { return ESP_OK; }

esp_err_t wlogger_thermal_start_task(void) {
    BaseType_t r = xTaskCreate(thermal_task, "thermal", 3072, NULL, 1, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
