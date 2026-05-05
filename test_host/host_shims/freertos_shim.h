#pragma once
#include <stdbool.h>
typedef int *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int x; return &x; }
static inline bool xSemaphoreTake(SemaphoreHandle_t s, int t) { (void)s;(void)t; return true; }
static inline void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
#define portMAX_DELAY 0xffffffff
