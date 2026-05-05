#pragma once
#include "wlogger_types.h"
#include "wlogger_bloom.h"

#ifdef __has_include
#  if __has_include("freertos_shim.h")
#    include "freertos_shim.h"
#  else
#    include "freertos/FreeRTOS.h"
#    include "freertos/semphr.h"
#  endif
#endif

typedef struct {
    bloom_t           bloom_wifi;
    bloom_t           bloom_probe;
    bloom_t           bloom_ble;
    uint64_t          total_events;
    uint64_t          dropped_events;
    uint16_t          rate_per_min[60];
    uint8_t           rate_head;
    uint32_t          last_minute_t;
    char              current_file[40];
    uint32_t          current_file_bytes;
    uint64_t          sd_free_bytes;
    bool              sd_ok, wifi_ok, ble_ok;
    top_entry_t       strongest[WLOG_TOPN];
    SemaphoreHandle_t mtx;
} stats_t;

void     stats_init(stats_t *s);
void     stats_record_event(stats_t *s, const detection_t *d);
void     stats_increment_rate_bucket(stats_t *s, uint32_t now_t_sec);
uint16_t stats_rate_last_minute(const stats_t *s);
void     stats_update_topn(stats_t *s, const detection_t *d);
