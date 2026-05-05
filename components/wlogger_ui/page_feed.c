#include "page_internal.h"
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 14
static lv_obj_t *s_root;
static lv_obj_t *s_rows[MAX_ROWS];

static const char *type_letter(det_type_t t) {
    return t == DET_WIFI_AP ? "W" : t == DET_WIFI_PROBE ? "P" : "B";
}

static void update(stats_t *st, recent_q_t *r) {
    (void)st;
    detection_t snap[MAX_ROWS];
    int n = recent_q_snapshot(r, snap, MAX_ROWS);
    for (int i = 0; i < MAX_ROWS; ++i) {
        if (i < n) {
            const detection_t *d = &snap[n - 1 - i];
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%s %02x:%02x %4d  %s",
                type_letter(d->type), d->mac[4], d->mac[5], (int)d->rssi, d->name);
            lv_label_set_text(s_rows[i], tmp);
        } else {
            lv_label_set_text(s_rows[i], "");
        }
    }
}

ui_page_t page_feed_create(lv_obj_t *parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    for (int i = 0; i < MAX_ROWS; ++i) {
        s_rows[i] = lv_label_create(s_root);
        lv_label_set_text(s_rows[i], "");
        lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_12, 0);
    }
    return (ui_page_t){ .root = s_root, .update = update };
}
