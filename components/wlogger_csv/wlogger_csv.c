#include "wlogger_csv.h"
#include <stdio.h>
#include <string.h>

static const char *type_letter(det_type_t t) {
    switch (t) { case DET_WIFI_AP: return "W"; case DET_WIFI_PROBE: return "P";
                 case DET_BLE: return "B"; default: return "?"; }
}

static const char *wifi_auth_str(uint8_t a) {
    switch (a) {
        case 0: return "OPEN"; case 1: return "WEP"; case 2: return "WPA";
        case 3: return "WPA2"; case 4: return "WPA_WPA2"; case 5: return "WPA2_ENT";
        case 6: return "WPA3"; case 7: return "WPA2_WPA3"; default: return "";
    }
}

static int append_escaped_name(char *o, size_t n, const char *name) {
    if (n < 3) return -1;
    int w = 0; o[w++] = '"';
    for (const char *p = name; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E) c = '?';
        if (c == '"') {
            if (w + 2 >= (int)n) return -1;
            o[w++] = '"'; o[w++] = '"';
        } else {
            if (w + 1 >= (int)n) return -1;
            o[w++] = (char)c;
        }
    }
    if (w + 1 >= (int)n) return -1;
    o[w++] = '"';
    return w;
}

int wlogger_csv_format(char *out, size_t n, const detection_t *d) {
    if (!out || !d || n < 32) return -1;
    int w = snprintf(out, n, "%u,%s,%02x%02x%02x%02x%02x%02x,%d,%u,",
        (unsigned)d->t_sec, type_letter(d->type),
        d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
        (int)d->rssi, (unsigned)d->channel);
    if (w < 0 || (size_t)w >= n) return -1;

    int nw = append_escaped_name(out + w, n - w, d->name);
    if (nw < 0) return -1;
    w += nw;

    const char *auth_str = "";
    if (d->type == DET_BLE)           auth_str = (d->auth == 0) ? "public" : "random";
    else if (d->type == DET_WIFI_AP)  auth_str = wifi_auth_str(d->auth);
    /* DET_WIFI_PROBE: probes don't carry auth info, leave empty */

    char mfg_buf[8] = "";
    if (d->type == DET_BLE && d->mfg_id != 0)
        snprintf(mfg_buf, sizeof mfg_buf, "%04X", d->mfg_id);

    int t = snprintf(out + w, n - w, ",%s,%s,%s\n",
        auth_str, mfg_buf, d->mac_random ? "R" : "");
    if (t < 0 || (size_t)t >= n - w) return -1;
    return w + t;
}
