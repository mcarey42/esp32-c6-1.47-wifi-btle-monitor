#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "wlogger_stats.h"
#include "esp_err.h"

#define RECENT_SIZE 32

typedef struct {
    detection_t entries[RECENT_SIZE];
    int         head;
    int         count;
    SemaphoreHandle_t mtx;
} recent_q_t;

esp_err_t wlogger_writer_init(QueueHandle_t detect_q, stats_t *stats, recent_q_t *recent);
esp_err_t wlogger_writer_start_task(void);
void      wlogger_writer_request_rotate(void);

void recent_q_init(recent_q_t *r);
void recent_q_push(recent_q_t *r, const detection_t *d);
int  recent_q_snapshot(const recent_q_t *r, detection_t *out, int max);
