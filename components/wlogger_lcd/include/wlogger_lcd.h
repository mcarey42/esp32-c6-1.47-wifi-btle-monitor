#pragma once
#include "esp_err.h"
#include "lvgl.h"

#define WLOG_LCD_W  172
#define WLOG_LCD_H  320

esp_err_t wlogger_lcd_init(void);
lv_display_t *wlogger_lcd_lv_display(void);
void wlogger_lcd_tick_ms(uint32_t ms);
