#pragma once
#include "esp_err.h"
#include "lvgl.h"

#define WLOG_LCD_W  172
#define WLOG_LCD_H  320

esp_err_t wlogger_lcd_init(void);
lv_display_t *wlogger_lcd_lv_display(void);
void wlogger_lcd_tick_ms(uint32_t ms);

// Application-level mutex protecting the shared SPI2 bus (LCD + SD).
// Any non-LCD user of SPI2 (currently: writer_task's SD writes via FATFS)
// MUST take this mutex before issuing transactions and release it after.
// The LCD's flush callback takes it internally and holds it until the
// transaction-done event fires, so the bus is genuinely idle when released.
void wlogger_lcd_bus_lock(void);
void wlogger_lcd_bus_unlock(void);
