#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WLOG_NAME_MAX  33    // 32 + null
#define WLOG_TOPN      8

typedef enum {
    DET_WIFI_AP    = 0,
    DET_WIFI_PROBE = 1,
    DET_BLE        = 2,
} det_type_t;

typedef struct {
    uint32_t   t_sec;
    det_type_t type;
    uint8_t    mac[6];
    int8_t     rssi;
    uint8_t    channel;
    char       name[WLOG_NAME_MAX];
    uint8_t    auth;          // wifi: wifi_auth_mode_t (0..); ble: 0=public 1=random
    uint16_t   mfg_id;
    bool       mac_random;
} detection_t;

typedef struct {
    uint8_t    mac[6];
    int8_t     rssi;
    char       name[WLOG_NAME_MAX];
    det_type_t type;
    uint32_t   last_seen_t;
} top_entry_t;
