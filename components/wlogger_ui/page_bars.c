#include "page_internal.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

#define N WLOG_TOPN
static lv_obj_t *s_bars[N], *s_labels[N];

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    top_entry_t copy[N];
    xSemaphoreTake(st->mtx, portMAX_DELAY);
    memcpy(copy, st->strongest, sizeof copy);
    xSemaphoreGive(st->mtx);
    for (int i = 0; i < N - 1; ++i)
        for (int j = i + 1; j < N; ++j)
            if (copy[j].rssi > copy[i].rssi) {
                top_entry_t t = copy[i]; copy[i] = copy[j]; copy[j] = t;
            }
    for (int i = 0; i < N; ++i) {
        if (copy[i].rssi == 0) {  // empty slot sentinel (matches stats impl)
            lv_bar_set_value(s_bars[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_labels[i], "");
            continue;
        }
        int pct = ((int)copy[i].rssi + 100) * 100 / 70;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(s_bars[i], pct, LV_ANIM_OFF);
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%-12.12s %d", copy[i].name[0] ? copy[i].name : "(noname)", (int)copy[i].rssi);
        lv_label_set_text(s_labels[i], tmp);
    }
}

ui_page_t page_bars_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, 1, 0);
    for (int i = 0; i < N; ++i) {
        s_labels[i] = lv_label_create(p);
        lv_obj_set_style_text_font(s_labels[i], &lv_font_montserrat_12, 0);
        s_bars[i] = lv_bar_create(p);
        lv_obj_set_size(s_bars[i], LV_PCT(95), 5);
        lv_bar_set_range(s_bars[i], 0, 100);
    }
    return (ui_page_t){ .root = p, .update = update };
}
