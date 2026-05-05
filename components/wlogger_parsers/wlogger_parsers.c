#include "wlogger_parsers.h"
#include <string.h>

static char sanitize(uint8_t c) { return (c < 0x20 || c > 0x7E) ? '?' : (char)c; }

void parse_ssid(const uint8_t *p, size_t n, char *out, size_t out_size) {
    if (out_size == 0) return;
    out[0] = '\0';
    size_t i = 0;
    while (i + 2 <= n) {
        uint8_t id = p[i], len = p[i + 1];
        if (i + 2 + len > n) return;        // malformed — stop
        if (id == 0x00) {                   // SSID tag
            size_t copy = len < out_size - 1 ? len : out_size - 1;
            for (size_t k = 0; k < copy; ++k) out[k] = sanitize(p[i + 2 + k]);
            out[copy] = '\0';
            return;
        }
        i += 2 + len;
    }
}

void parse_adv_data(const uint8_t *adv, size_t len,
                    char *out_name, size_t out_size, uint16_t *mfg_id) {
    if (out_size) out_name[0] = '\0';
    if (mfg_id) *mfg_id = 0;
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t L = adv[i];
        if (L == 0) break;
        if (i + 1 + L > len) return;       // truncated
        uint8_t T = adv[i + 1];
        const uint8_t *V = &adv[i + 2];
        size_t VL = L - 1;

        if ((T == 0x08 || T == 0x09) && out_size > 0) {
            size_t copy = VL < out_size - 1 ? VL : out_size - 1;
            for (size_t k = 0; k < copy; ++k) {
                uint8_t c = V[k];
                out_name[k] = (c < 0x20 || c > 0x7E) ? '?' : (char)c;
            }
            out_name[copy] = '\0';
        } else if (T == 0xFF && mfg_id && VL >= 2) {
            *mfg_id = (uint16_t)(V[0] | (V[1] << 8));
        }
        i += 1 + L;
    }
}
