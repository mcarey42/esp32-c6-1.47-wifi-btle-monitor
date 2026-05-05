#pragma once
#include "wlogger_types.h"
#include "esp_err.h"
#include <stdio.h>

#define WLOG_SD_MOUNT  "/sdcard"
#define WLOG_DIR       "/sdcard/wlogger"

typedef struct {
    FILE     *fp;
    uint16_t  session;
    uint16_t  index;
    uint32_t  bytes_written;
    uint32_t  opened_at_t_sec;
    char      path[40];
} wlogger_file_t;

esp_err_t wlogger_sd_mount(void);
esp_err_t wlogger_sd_open_new_file(wlogger_file_t *f, uint32_t now_t_sec, const char *cycle_label);
esp_err_t wlogger_sd_close(wlogger_file_t *f);
bool      wlogger_sd_should_rotate(const wlogger_file_t *f, uint32_t now_t_sec);
uint64_t  wlogger_sd_free_bytes(void);
