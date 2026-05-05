#include "page_internal.h"
#include "freertos/semphr.h"
#include <stdio.h>

static lv_obj_t *s_file, *s_size, *s_free, *s_drops, *s_flags;

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
    return (ui_page_t){ .root = p, .update = update };
}
