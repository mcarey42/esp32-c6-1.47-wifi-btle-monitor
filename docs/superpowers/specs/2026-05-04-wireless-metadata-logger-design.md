# Wireless Metadata Logger — Design Spec

**Project:** `esp32-c6-LCD-1.47/wlogger`
**Target board:** Waveshare ESP32-C6-LCD-1.47
**Date:** 2026-05-04
**Status:** Approved design, ready for implementation planning

## 1. Goal

Build a long-term, wall-powered wireless-presence logger that runs continuously on a Waveshare ESP32-C6-LCD-1.47 and records every Wi-Fi and BLE device it observes onto a microSD card, while showing live activity on the LCD and an at-a-glance status on the on-board WS2812B LED.

The device is intended for "set-and-forget" deployments (sit on a shelf for days/weeks) where the user wants a rich, time-stamped record of devices that have appeared in range, plus a glanceable indication of how active the local RF environment is.

## 2. Hardware

| Function | Pin | Notes |
|---|---|---|
| SD_CS | IO4 | SPI chip-select |
| SD_MISO | IO5 | |
| SD_MOSI | IO6 | |
| SD_SCLK | IO7 | |
| WS2812B data | IO8 | RMT-driven |
| LCD_CS | IO14 | |
| LCD_DC | IO15 | |
| LCD_RST | IO21 | |
| LCD_BL | IO22 | Drive high to enable backlight |
| Button | IO9 | Active high (also strapping pin for boot — runtime-only use) |

Other constraints:

- ESP32-C6: single 160 MHz RISC-V HP core, 512 KB SRAM.
- Single 2.4 GHz radio shared between Wi-Fi 6 and BLE 5.0 (no Bluetooth Classic).
- LCD is an ST7789 controller, 172 × 320 portrait, with a 34-pixel column offset on this board variant.

## 3. Functional requirements

1. Capture Wi-Fi management frames in promiscuous mode (beacons + probe-requests), across all 13 2.4 GHz channels.
2. Capture BLE 5.0 advertisements (active scan, all three primary advertising channels).
3. Persist every observation as a CSV row on a microSD card.
4. Rotate the log file at the first of: 5 MB written, 1 hour elapsed, or long-press of the button.
5. Display live counters, a per-minute rate sparkline, a strongest-devices list, a recent-events feed, and SD/file status across four button-cycled LCD pages.
6. Encode current scan phase (hue) and recent rate (brightness) on the WS2812B LED.
7. Continue logging through non-essential subsystem failures (LCD, WS2812).

## 4. Non-goals

- Bluetooth Classic capture (hardware-impossible on C6).
- Wall-clock timestamps (deferred — monotonic seconds + per-boot session number).
- Battery operation, sleep modes, low-power tuning.
- Network upload / remote viewing / OTA.
- MAC vendor lookup at the device (post-process on a real computer).
- Decryption or content-level Wi-Fi capture (only management frames).

## 5. Architecture

### 5.1 Stack

- **Build system:** ESP-IDF v5.x (matches the user's neighboring projects).
- **Display:** LVGL 9 layered on `esp_lcd` ST7789 driver.
- **BLE:** NimBLE host stack (controller-only scanning).
- **SD:** FATFS over SPI via `esp_vfs_fat_sdspi_mount`.
- **LED:** `led_strip` ESP-IDF component (RMT-backed).

### 5.2 Tasks and shared state

Three FreeRTOS tasks on the single HP core, prioritised data path > writer > UI:

| Task | Priority | Responsibility |
|---|---|---|
| `scan_task` | 5 | Drives radio. Alternates Wi-Fi promiscuous sweep with BLE scan window. |
| `writer_task` | 4 | Drains `detect_q`, formats CSV, batched flush to SD, updates `stats` and `recent_q`. |
| `ui_task` | 3 | LVGL tick & render, button polling, WS2812 update. |

Shared state:

- **`detect_q`** — FreeRTOS queue of 256 × `detection_t` (≈13 KB). Backpressure barrier between radio and storage. Producer: scan callbacks (non-blocking sends). Consumer: writer.
- **`recent_q`** — 32-deep ring buffer of recent detections for the live-feed page. Producer: writer. Consumer: UI.
- **`stats`** — mutex-guarded struct of unique-MAC bloom-filter counts, 60-bucket per-minute rate ring, top-8-by-RSSI list, current file metadata, dropped-event counter.

### 5.3 Data flow

```
radio rx → scan callbacks → detect_q → writer_task ┬→ SD card (rotated CSV)
                                                   ├→ recent_q (last 32)
                                                   └→ stats (uniques, rate, top-N)

ui_task tick (100 ms) ← stats + recent_q ← LVGL render → ST7789

button IO9 → ui_task short-press: next page · long-press (2 s): force-flush + rotate
```

## 6. Data model

### 6.1 In-memory `detection_t`

```c
typedef enum { DET_WIFI_AP = 0, DET_WIFI_PROBE = 1, DET_BLE = 2 } det_type_t;

typedef struct {
    uint32_t   t_sec;     // monotonic seconds since boot
    det_type_t type;
    uint8_t    mac[6];
    int8_t     rssi;
    uint8_t    channel;   // wifi 1–13, ble 37/38/39
    char       name[33];  // SSID or BLE local name, "" if none
    uint8_t    auth;      // wifi: wifi_auth_mode_t; ble: addr_type
    uint16_t   mfg_id;    // BLE manufacturer-specific company id
    bool       mac_random; // locally-administered bit set
} detection_t;
```

### 6.2 CSV format

Each file begins with a header line and a column line:

```
# session=7  start_t_sec=0  fw=wlogger-0.1  cycle=balanced
t_sec,type,mac,rssi,ch,name,auth,mfg,meta
```

Rows:

- `type`: `W` = Wi-Fi AP (beacon), `P` = Wi-Fi probe-request, `B` = BLE advertisement.
- `mac`: 12 hex chars, lowercase, no separators.
- `name`: CSV-escaped (quotes doubled), control bytes replaced with `?`.
- `auth`: `OPEN` / `WEP` / `WPA` / `WPA2` / `WPA3` / `WPA2_ENT` for Wi-Fi; `public` / `random` for BLE.
- `mfg`: BLE company-id hex (e.g. `004C` = Apple), empty otherwise.
- `meta`: optional flags. `R` = randomized MAC (locally-administered bit set).

Sample:

```
12,W,a4c3f7110e22,-48,6,"home-net",WPA2,,
13,P,3c2a8d7e0033,-71,11,"",,,R
13,B,f87b5c204c4d,-62,38,"AirPods Pro",public,004C,
14,B,d2118a99fe11,-78,39,"",random,,R
```

### 6.3 File rotation and naming

Files: `/sdcard/wlogger/log_<session>_<index>.csv` (zero-padded, e.g. `log_0007_0003.csv`).

- **Session** assigned at boot by scanning the directory and incrementing the highest existing value.
- **Index** starts at 1 each session, increments on every rotation.
- **Rotation triggers** (whichever first):
  - 5 MB written
  - 3600 seconds elapsed
  - Long-press of IO9 (≥ 2 s)

### 6.4 `stats` struct

```c
typedef struct {
    bloom_t    bloom_wifi;        // ~16 KB, 1% error
    bloom_t    bloom_probe;
    bloom_t    bloom_ble;
    uint64_t   total_events;
    uint64_t   dropped_events;
    uint16_t   rate_per_min[60];  // per-minute ring buffer
    uint8_t    rate_head;
    char       current_file[40];
    uint32_t   current_file_bytes;
    uint64_t   sd_free_bytes;
    bool       sd_ok;
    bool       wifi_ok;
    bool       ble_ok;
    struct {
        uint8_t    mac[6];
        int8_t     rssi;
        char       name[33];
        det_type_t type;
        uint32_t   last_seen_t;
    } strongest[8];
    SemaphoreHandle_t mtx;
} stats_t;
```

Bloom filters are the source of truth for "unique device count" (true sets would blow the heap on multi-day runs).

## 7. Scan engine

### 7.1 State machine

```
PHASE_WIFI    (~6.5 s)
  esp_wifi_set_promiscuous(true)
  esp_wifi_set_promiscuous_filter(MGMT only)
  for ch in 1..13:
      esp_wifi_set_channel(ch, SECOND_CHAN_NONE)
      vTaskDelay(500 ms)
  esp_wifi_set_promiscuous(false)

PHASE_BLE     (~2.0 s)
  ble_gap_disc(0, dur=2000, params={active=1, window=100, itvl=100})
  wait for completion event
```

Phases are fully sequential — only one stack ever has the radio at a time, eliminating coexistence-scheduler arbitration loss.

Active Wi-Fi scan is **not** used: promiscuous mode already captures every received beacon, so `esp_wifi_scan_start` would only duplicate work and force unnecessary STA-mode toggling.

### 7.2 Wi-Fi RX callback

```c
static void IRAM_ATTR wifi_promisc_cb(void *buf, wifi_promiscuous_pkt_type_t t) {
    if (t != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    uint16_t fc = p[0] | (p[1] << 8);
    uint8_t  subtype = (fc >> 4) & 0xF;       // 8=beacon, 4=probe-req

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .rssi    = pkt->rx_ctrl.rssi,
        .channel = pkt->rx_ctrl.channel,
    };

    if (subtype == 8) {
        d.type = DET_WIFI_AP;
        memcpy(d.mac, p + 16, 6);             // BSSID = addr3
        parse_ssid(p + 36, pkt->rx_ctrl.sig_len - 36, d.name, sizeof d.name);
        parse_auth(p + 36, &d.auth);
    } else if (subtype == 4) {
        d.type = DET_WIFI_PROBE;
        memcpy(d.mac, p + 10, 6);             // src = addr2
        parse_ssid(p + 24, pkt->rx_ctrl.sig_len - 24, d.name, sizeof d.name);
    } else {
        return;
    }
    d.mac_random = (d.mac[0] & 0x02) != 0;

    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(detect_q, &d, &hp);
    portYIELD_FROM_ISR(hp);
}
```

`IRAM_ATTR` is required: without it, the callback is paged from flash and will drop packets in dense traffic.

### 7.3 BLE GAP callback

```c
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .type    = DET_BLE,
        .rssi    = event->disc.rssi,
        .auth    = event->disc.addr.type,     // 0=public, 1=random
    };
    memcpy(d.mac, event->disc.addr.val, 6);
    d.mac_random = (event->disc.addr.type == 1);

    parse_adv_data(event->disc.data, event->disc.length_data,
                   d.name, sizeof d.name, &d.mfg_id);

    xQueueSend(detect_q, &d, 0);
    return 0;
}
```

`parse_adv_data` walks the LE AD-structure list, extracts Complete (`0x09`) or Shortened (`0x08`) Local Name and the company-id from Manufacturer Specific Data (`0xFF`).

### 7.4 Writer task

```c
void writer_task(void *_) {
    detection_t d;
    char        buf[4096];
    size_t      buf_len = 0;
    TickType_t  last_flush = xTaskGetTickCount();

    open_or_rotate_file();
    for (;;) {
        if (xQueueReceive(detect_q, &d, pdMS_TO_TICKS(200)) == pdTRUE) {
            buf_len += format_csv_line(buf + buf_len, sizeof buf - buf_len, &d);
            update_stats(&d);
            push_recent_q(&d);
        }
        bool full  = buf_len > sizeof(buf) - 256;
        bool stale = (xTaskGetTickCount() - last_flush) > pdMS_TO_TICKS(1000);
        if (full || stale) {
            flush(buf, buf_len); buf_len = 0;
            last_flush = xTaskGetTickCount();
        }
        if (rotation_needed()) {
            close_current_file();
            open_or_rotate_file();
        }
    }
}
```

Buffer is flushed on the first of: ≥3.8 KB pending, or 1 s elapsed, or rotation. Batched writes are essential for SD-card longevity (read-modify-erase amplification on tiny writes).

## 8. UI

### 8.1 LCD initialisation order

```
1. gpio_set_direction(IO22, OUTPUT); gpio_set_level(IO22, 1)   // backlight on
2. esp_lcd_new_panel_io_spi(SPI2_HOST, ...)                    // CS=14, DC=15
3. esp_lcd_new_panel_st7789(io, &cfg)                          // RST=21
4. esp_lcd_panel_reset(panel); esp_lcd_panel_init(panel)
5. esp_lcd_panel_invert_color(panel, true)
6. esp_lcd_panel_set_gap(panel, 34, 0)                         // Waveshare 1.47" offset
7. lv_init(); attach panel as LVGL display driver
```

The 34-pixel X gap is specific to this board variant — the visible region is columns 34..205 of the controller's 240-wide buffer.

### 8.2 Pages (LVGL `lv_tabview`)

| # | Page | Widgets | Refresh policy |
|---|---|---|---|
| 1 | Stats | big-number labels, `lv_chart` 60-bucket sparkline | every 100 ms |
| 2 | Live Feed | `lv_list` from `recent_q` | only when new event arrives |
| 3 | Strength Bars | 8 × `lv_bar` from `stats.strongest[]` | every 500 ms (avoids flicker) |
| 4 | File / SD | labels: current file, bytes, free MB, dropped events, ok flags | every 1 s |

Page transitions are abrupt (`LV_ANIM_OFF`) to keep render time bounded.

### 8.3 Button (IO9)

```
short press  (<300 ms)   → next page
long  press  (≥2000 ms)  → flush writer + rotate file
```

Polled at 100 ms in `ui_task`. Debounce: require 3 consecutive same-state samples (~300 ms total). No interrupt — we're never asleep so latency doesn't matter.

### 8.4 WS2812B status

Updated once per second from `ui_task` via `set_status_led(phase, rate, event)`:

- **Hue:** scan phase. Cyan during Wi-Fi sweep, magenta during BLE window.
- **Brightness:** log-mapped to recent-minute rate. 0 events → 5 %, 60+/min → 60 %.
- **Single red flash (50 ms):** SD write error or `detect_q` overflow.
- **Slow yellow breath (2 s):** file rotation just occurred.
- **Solid red:** persistent fault (SD lost / SD full / no radios).
- **Orange flash (200 ms):** heap below 16 KB warning.

## 9. Error handling

Logging is the highest-priority subsystem; UI and LED degrade gracefully.

| Failure | Response |
|---|---|
| SD missing at boot | Full-screen `NO SD CARD — INSERT AND REBOOT`, solid red LED, `scan_task` not started. |
| SD pulled at runtime | Close file, `sd_ok=false`, queues drain (events lost), toast `SD LOST`, solid red LED. |
| SD full | Stop opening new files, `sd_ok=false`, `SD FULL` toast, solid red. |
| Wi-Fi init fail | Skip Wi-Fi phase, BLE-only, write `# wifi_off` marker into the CSV. |
| BLE init fail | Skip BLE phase, Wi-Fi-only, similar marker. |
| Both radios fail | Halt `scan_task`, `NO RADIOS` screen, red LED, UI still updates. |
| LCD init fail | Kill `ui_task`, log warning, continue logging headlessly, slow red/blue LED pulse. |
| WS2812 init fail | NULL handle; all LED calls become no-ops. |
| `detect_q` overflow | Increment `dropped_events`; visible on file/SD page. |
| Heap below 16 KB | `LOW MEM` toast, orange LED flash. |
| Brownout | Hardware reset; new session on reboot. |

Edge cases:

- Corrupted last file from prior boot: tolerated. We never re-open old files; FATFS may have a partially-flushed last sector, parser tolerates row-level truncation.
- Sustained over-rate detections: dropped silently, counted; no backpressure to radio.
- BLE/SSID names with non-printable bytes: filtered to `?`, CSV-escaped.
- Session number wrap: 65 535 boots — accepted limit.

## 10. Testing

### 10.1 Host-side unit tests (`test_host/`)

- `format_csv_line()` — table-driven: every `det_type_t`, names with `"`/`,`/control bytes, empty names.
- `parse_ssid()` — synthetic 802.11 tagged-parameter buffers including malformed (length byte exceeds buffer).
- `parse_adv_data()` — synthetic LE AD lists: truncated, missing name, mfg-data variations.
- `bloom_*()` — verify estimate error against an `unordered_set` ground truth for 1 k–100 k MACs.

### 10.2 Hardware integration

- *Smoke:* boot with empty SD → `NO SD` screen.
- *Wi-Fi:* boot near known AP → `W,...` row matching that BSSID within 7 s.
- *BLE:* boot near a known advertiser → `B,...` row within 10 s.
- *Long-press:* hold IO9 for 2 s → file index increments, yellow LED breath fires.
- *Soak:* 24 h run in a normal room → ≥ 24 rotated files, no crashes in `esp_log`, heap-free not trending down.

### 10.3 Hardware-stub mode

`CONFIG_WLOGGER_FAKE_RADIOS` (Kconfig) replaces real callbacks with a synthetic generator pushing ~30 events/s into `detect_q`. Used for offline development of writer, UI, and SD path. ~80 lines.

## 11. Estimated resource budget

| Resource | Estimate |
|---|---|
| Flash | ~700 KB (LVGL + Wi-Fi + NimBLE + FATFS + app) |
| Heap free at idle | ~140 KB out of ~512 KB SRAM |
| `detect_q` | 13 KB (256 × 52 bytes) |
| Bloom filters | 3 × 16 KB = 48 KB |
| LVGL display buffer | 2 × 8 KB partial buffers |
| SD write rate (sustained) | 5–10 KB/s |
| File volume in busy environment | 10–30 MB/day |

## 12. Out-of-scope (deferred work)

- NTP/RTC time source (currently monotonic + session number).
- Battery / sleep / power optimisation.
- Web UI for browsing logs from the device.
- BLE advertisement deduplication on-device.
- MAC vendor / OUI lookup on-device.
- Optional Zigbee/Thread (802.15.4) capture — possible on C6 hardware, not in this revision.
