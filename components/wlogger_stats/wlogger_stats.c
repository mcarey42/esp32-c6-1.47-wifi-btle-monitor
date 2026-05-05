#include "wlogger_stats.h"
#include <string.h>

void stats_init(stats_t *s) {
    memset(s, 0, sizeof *s);
    bloom_init(&s->bloom_wifi);
    bloom_init(&s->bloom_probe);
    bloom_init(&s->bloom_ble);
    s->mtx = xSemaphoreCreateMutex();
    s->sd_ok = true;
}

static int find_mac(const stats_t *s, const uint8_t mac[6]) {
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (memcmp(s->strongest[i].mac, mac, 6) == 0) return i;
    return -1;
}

/* RSSI of 0 marks a slot as empty (real RSSI values are always negative). */
static int slot_empty(const top_entry_t *e) {
    return e->rssi == 0;
}

static int find_weakest(const stats_t *s) {
    int idx = 0; int8_t worst = 0;
    for (int i = 0; i < WLOG_TOPN; ++i) {
        if (slot_empty(&s->strongest[i])) return i;
        if (s->strongest[i].rssi < worst) { worst = s->strongest[i].rssi; idx = i; }
    }
    return idx;
}

void stats_update_topn(stats_t *s, const detection_t *d) {
    int existing = find_mac(s, d->mac);
    if (existing >= 0) {
        if (d->rssi > s->strongest[existing].rssi) {
            s->strongest[existing].rssi = d->rssi;
            s->strongest[existing].last_seen_t = d->t_sec;
            strncpy(s->strongest[existing].name, d->name, WLOG_NAME_MAX - 1);
        }
        return;
    }
    int slot = find_weakest(s);
    if (slot_empty(&s->strongest[slot]) || d->rssi > s->strongest[slot].rssi) {
        memcpy(s->strongest[slot].mac, d->mac, 6);
        s->strongest[slot].rssi = d->rssi;
        s->strongest[slot].type = d->type;
        s->strongest[slot].last_seen_t = d->t_sec;
        strncpy(s->strongest[slot].name, d->name, WLOG_NAME_MAX - 1);
        s->strongest[slot].name[WLOG_NAME_MAX - 1] = 0;
    }
}

void stats_increment_rate_bucket(stats_t *s, uint32_t now_t_sec) {
    uint32_t now_min = now_t_sec / 60;
    uint32_t last_min = s->last_minute_t / 60;
    if (s->last_minute_t == 0 && s->rate_per_min[s->rate_head] == 0) {
        s->last_minute_t = now_t_sec;
        last_min = now_min;
    }
    while (now_min > last_min) {
        s->rate_head = (s->rate_head + 1) % 60;
        s->rate_per_min[s->rate_head] = 0;
        last_min++;
    }
    s->last_minute_t = now_t_sec;
    s->rate_per_min[s->rate_head]++;
}

uint16_t stats_rate_last_minute(const stats_t *s) {
    return s->rate_per_min[s->rate_head];
}

void stats_record_event(stats_t *s, const detection_t *d) {
    if (xSemaphoreTake(s->mtx, portMAX_DELAY) != true) return;
    s->total_events++;
    switch (d->type) {
        case DET_WIFI_AP:    bloom_add(&s->bloom_wifi,  d->mac); break;
        case DET_WIFI_PROBE: bloom_add(&s->bloom_probe, d->mac); break;
        case DET_BLE:        bloom_add(&s->bloom_ble,   d->mac); break;
    }
    stats_update_topn(s, d);
    stats_increment_rate_bucket(s, d->t_sec);
    xSemaphoreGive(s->mtx);
}
