#include "page_internal.h"
#include "wlogger_bloom.h"
#include "esp_timer.h"
#include <stdio.h>

typedef struct {
    lv_obj_t *uptime, *wifi_n, *ble_n, *probe_n, *rate, *chart;
    lv_chart_series_t *series;
    int chart_pts;
} ctx_t;
static ctx_t s;

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    char tmp[32];
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(tmp, sizeof tmp, "%02u:%02u:%02u",
             (unsigned)(up/3600), (unsigned)((up/60)%60), (unsigned)(up%60));
    lv_label_set_text(s.uptime, tmp);

    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_wifi));
    lv_label_set_text(s.wifi_n, tmp);
    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_ble));
    lv_label_set_text(s.ble_n, tmp);
    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_probe));
    lv_label_set_text(s.probe_n, tmp);

    uint16_t now_rate = stats_rate_last_minute(st);
    snprintf(tmp, sizeof tmp, "%u/m", (unsigned)now_rate);
    lv_label_set_text(s.rate, tmp);

    if (s.chart_pts < 30) s.chart_pts++;
    for (int i = 0; i < s.chart_pts - 1; ++i) {
        int idx = (st->rate_head + 60 - s.chart_pts + 1 + i) % 60;
        lv_chart_set_series_value_by_id(s.chart, s.series, i, st->rate_per_min[idx]);
    }
    lv_chart_set_series_value_by_id(s.chart, s.series, s.chart_pts - 1, now_rate);
    lv_chart_refresh(s.chart);
}

ui_page_t page_stats_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *l;
    s.uptime = lv_label_create(p); lv_label_set_text(s.uptime, "00:00:00");

    l = lv_label_create(p); lv_label_set_text(l, "Wi-Fi APs");
    s.wifi_n = lv_label_create(p); lv_obj_set_style_text_font(s.wifi_n, &lv_font_montserrat_28, 0);

    l = lv_label_create(p); lv_label_set_text(l, "BLE devs");
    s.ble_n = lv_label_create(p); lv_obj_set_style_text_font(s.ble_n, &lv_font_montserrat_28, 0);

    l = lv_label_create(p); lv_label_set_text(l, "Probe MACs");
    s.probe_n = lv_label_create(p); lv_obj_set_style_text_font(s.probe_n, &lv_font_montserrat_20, 0);

    l = lv_label_create(p); lv_label_set_text(l, "Rate (60 s)");
    s.rate = lv_label_create(p); lv_obj_set_style_text_font(s.rate, &lv_font_montserrat_20, 0);

    s.chart = lv_chart_create(p);
    lv_obj_set_size(s.chart, LV_PCT(100), 60);
    lv_chart_set_type(s.chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s.chart, 30);
    lv_chart_set_axis_range(s.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    s.series = lv_chart_add_series(s.chart, lv_color_hex(0xFFE27A), LV_CHART_AXIS_PRIMARY_Y);
    s.chart_pts = 0;

    return (ui_page_t){ .root = p, .update = update };
}
