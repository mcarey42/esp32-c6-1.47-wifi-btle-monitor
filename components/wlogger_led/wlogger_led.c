#include "wlogger_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "led";
#define PIN_LED  8
static led_strip_handle_t s_strip = NULL;

esp_err_t wlogger_led_init(void) {
    led_strip_config_t scfg = {
        .strip_gpio_num = PIN_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10 * 1000 * 1000 };
    esp_err_t err = led_strip_new_rmt_device(&scfg, &rmt, &s_strip);
    if (err != ESP_OK) { ESP_LOGW(TAG, "init failed: %s", esp_err_to_name(err)); s_strip = NULL; }
    return err;
}

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b) {
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.f, 2) - 1)), m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if      (h < 60)  { rf=c; gf=x; bf=0; }
    else if (h < 120) { rf=x; gf=c; bf=0; }
    else if (h < 180) { rf=0; gf=c; bf=x; }
    else if (h < 240) { rf=0; gf=x; bf=c; }
    else if (h < 300) { rf=x; gf=0; bf=c; }
    else              { rf=c; gf=0; bf=x; }
    *r = (uint8_t)((rf + m) * 255);
    *g = (uint8_t)((gf + m) * 255);
    *b = (uint8_t)((bf + m) * 255);
}

void wlogger_led_set(led_phase_t phase, uint16_t rate, led_event_t evt) {
    if (!s_strip) return;
    uint8_t r=0, g=0, b=0;
    if (evt == LED_EVT_FAULT) { r=180; }
    else if (evt == LED_EVT_DROP) { r=255; }
    else if (evt == LED_EVT_LOW_MEM) { r=200; g=80; }
    else if (evt == LED_EVT_ROTATED) { r=180; g=160; }
    else {
        float br = 0.05f + 0.55f * (logf(1 + (float)rate) / logf(1 + 60.f));
        if (br > 0.60f) br = 0.60f;
        float hue = (phase == LED_PHASE_WIFI) ? 180.f : 300.f;
        hsv_to_rgb(hue, 1.f, br, &r, &g, &b);
    }
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}
