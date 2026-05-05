#pragma once
#include "lvgl.h"
#include "wlogger_stats.h"
#include "wlogger_writer.h"

typedef struct {
    lv_obj_t *root;
    void (*update)(stats_t *s, recent_q_t *r);
} ui_page_t;

ui_page_t page_stats_create(lv_obj_t *parent);
ui_page_t page_feed_create(lv_obj_t *parent);
ui_page_t page_bars_create(lv_obj_t *parent);
ui_page_t page_status_create(lv_obj_t *parent);
