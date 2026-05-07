#include "page_internal.h"
#include "wlogger_thermal.h"
#include "freertos/semphr.h"
#include <stdio.h>

static lv_obj_t *s_file, *s_size, *s_free, *s_drops, *s_flags, *s_temp;

static const char *band_str(thermal_band_t b) {
    switch (b) {
        case THERMAL_COOL:     return "COOL";
        case THERMAL_WARM:     return "WARM";
        case THERMAL_HOT:      return "HOT";
        case THERMAL_CRITICAL: return "CRIT";
        default:               return "?";
    }
}

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    char tmp[64];
    xSemaphoreTake(st->mtx, portMAX_DELAY);
    snprintf(tmp, sizeof tmp, "%s", st->current_file[0] ? st->current_file : "(none)");
    lv_label_set_text(s_file, tmp);
    snprintf(tmp, sizeof tmp, "Size: %lu B", (unsigned long)st->current_file_bytes);
    lv_label_set_text(s_size, tmp);
    double mb_free = (double)st->sd_free_bytes / (1024.0 * 1024.0);
    snprintf(tmp, sizeof tmp, "Free: %.1f MB", mb_free);
    lv_label_set_text(s_free, tmp);
    snprintf(tmp, sizeof tmp, "Drops: %llu", (unsigned long long)st->dropped_events);
    lv_label_set_text(s_drops, tmp);
    snprintf(tmp, sizeof tmp, "WIFI:%s BLE:%s SD:%s",
        st->wifi_ok ? "Y" : "N", st->ble_ok ? "Y" : "N", st->sd_ok ? "Y" : "N");
    lv_label_set_text(s_flags, tmp);
    xSemaphoreGive(st->mtx);

    int8_t c = wlogger_thermal_celsius();
    thermal_band_t b = wlogger_thermal_band();
    if (c == -127) {
        lv_label_set_text(s_temp, "Temp: --");
    } else {
        snprintf(tmp, sizeof tmp, "Temp: %dC %s", (int)c, band_str(b));
        lv_label_set_text(s_temp, tmp);
    }
}

ui_page_t page_status_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    s_file  = lv_label_create(p);
    s_size  = lv_label_create(p);
    s_free  = lv_label_create(p);
    s_drops = lv_label_create(p);
    s_flags = lv_label_create(p);
    s_temp  = lv_label_create(p);
    return (ui_page_t){ .root = p, .update = update };
}
