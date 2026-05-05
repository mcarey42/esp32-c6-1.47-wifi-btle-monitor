#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { BTN_NONE = 0, BTN_SHORT = 1, BTN_LONG = 2 } btn_event_t;

esp_err_t    wlogger_button_init(void);
btn_event_t  wlogger_button_poll(uint32_t poll_period_ms);
