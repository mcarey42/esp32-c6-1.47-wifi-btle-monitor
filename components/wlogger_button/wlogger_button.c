#include "wlogger_button.h"
#include "driver/gpio.h"

#define PIN 9
#define DEBOUNCE_SAMPLES 3
#define LONG_MS 2000

esp_err_t wlogger_button_init(void) {
    gpio_config_t c = { .pin_bit_mask = 1ULL << PIN, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_ENABLE };
    return gpio_config(&c);
}

btn_event_t wlogger_button_poll(uint32_t poll_ms) {
    static int samples_high = 0;
    static bool down = false;
    static uint32_t held_ms = 0;
    static bool emitted_long = false;

    int raw = gpio_get_level(PIN);
    if (raw) {
        if (++samples_high >= DEBOUNCE_SAMPLES) {
            if (!down) { down = true; held_ms = 0; emitted_long = false; }
            held_ms += poll_ms;
            if (!emitted_long && held_ms >= LONG_MS) {
                emitted_long = true;
                return BTN_LONG;
            }
        }
    } else {
        samples_high = 0;
        if (down) {
            bool was_short = !emitted_long && held_ms < LONG_MS;
            down = false; held_ms = 0;
            if (was_short) return BTN_SHORT;
        }
    }
    return BTN_NONE;
}
