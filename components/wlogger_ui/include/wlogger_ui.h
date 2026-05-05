#pragma once
#include "wlogger_stats.h"
#include "wlogger_writer.h"
#include "esp_err.h"

esp_err_t wlogger_ui_init(stats_t *stats, recent_q_t *recent);
esp_err_t wlogger_ui_start_task(void);
