# Wireless Metadata Logger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a long-term Wi-Fi + BLE presence logger on the Waveshare ESP32-C6-LCD-1.47 that writes CSV records to microSD, displays activity on a 4-page LVGL UI, and uses the WS2812B as a phase/rate indicator.

**Architecture:** ESP-IDF v5.x application with three FreeRTOS tasks: `scan_task` alternates between Wi-Fi promiscuous channel-hopping (6.5 s) and BLE scanning (2 s), pushing `detection_t` events into `detect_q`; `writer_task` drains the queue, batches CSV lines into a 4 KB buffer, flushes to a rotated file on SD, and updates a mutex-guarded `stats` struct; `ui_task` renders LVGL pages, polls the IO9 button, and drives the WS2812 LED. Pure data modules (CSV formatter, AD parsers, bloom filter) are host-testable under `test_host/`.

**Tech Stack:** ESP-IDF v5.x · LVGL 9 · NimBLE · FATFS over SPI · `esp_lcd` ST7789 · `led_strip` RMT · Unity (host tests) · CMocka-style assertions.

**Reference spec:** `docs/superpowers/specs/2026-05-04-wireless-metadata-logger-design.md`

---

## File Structure

### Production source (`main/` + `components/`)

| Path | Responsibility |
|---|---|
| `main/CMakeLists.txt` | Component manifest |
| `main/app_main.c` | Tiny entrypoint: init nvs, start tasks |
| `main/wlogger_init.c` / `.h` | Subsystem init + graceful-degradation orchestration |
| `components/wlogger_types/include/wlogger_types.h` | `detection_t`, `det_type_t`, `stats_t` |
| `components/wlogger_csv/include/wlogger_csv.h` | CSV row formatter API |
| `components/wlogger_csv/wlogger_csv.c` | CSV row formatter (host-buildable) |
| `components/wlogger_parsers/include/wlogger_parsers.h` | `parse_ssid`, `parse_adv_data` API |
| `components/wlogger_parsers/wlogger_parsers.c` | 802.11 + BLE AD parsers (host-buildable) |
| `components/wlogger_bloom/include/wlogger_bloom.h` | Bloom filter API |
| `components/wlogger_bloom/wlogger_bloom.c` | Bloom filter impl (host-buildable) |
| `components/wlogger_stats/include/wlogger_stats.h` | `stats_t` access helpers |
| `components/wlogger_stats/wlogger_stats.c` | Stats update / mutex / rate ring / top-N |
| `components/wlogger_sd/include/wlogger_sd.h` | SD mount + file rotation API |
| `components/wlogger_sd/wlogger_sd.c` | FATFS mount, session number, rotation |
| `components/wlogger_writer/include/wlogger_writer.h` | Writer task entrypoint |
| `components/wlogger_writer/wlogger_writer.c` | `writer_task` impl |
| `components/wlogger_scan/include/wlogger_scan.h` | Scan task entrypoint, fake-radio toggle |
| `components/wlogger_scan/wlogger_scan_wifi.c` | Wi-Fi promiscuous + channel hop |
| `components/wlogger_scan/wlogger_scan_ble.c` | NimBLE scan |
| `components/wlogger_scan/wlogger_scan_task.c` | State machine driving both phases |
| `components/wlogger_scan/wlogger_scan_fake.c` | Synthetic event generator (`CONFIG_WLOGGER_FAKE_RADIOS`) |
| `components/wlogger_lcd/include/wlogger_lcd.h` | LCD init + LVGL display attach |
| `components/wlogger_lcd/wlogger_lcd.c` | ST7789 panel + LVGL display driver |
| `components/wlogger_led/include/wlogger_led.h` | WS2812 status policy API |
| `components/wlogger_led/wlogger_led.c` | `set_status_led(phase, rate, event)` |
| `components/wlogger_button/include/wlogger_button.h` | Button event API |
| `components/wlogger_button/wlogger_button.c` | Polled debounce / short / long press |
| `components/wlogger_ui/include/wlogger_ui.h` | UI task entrypoint |
| `components/wlogger_ui/wlogger_ui.c` | `ui_task`: tabview + page render dispatch |
| `components/wlogger_ui/page_stats.c` | Page 1 |
| `components/wlogger_ui/page_feed.c` | Page 2 |
| `components/wlogger_ui/page_bars.c` | Page 3 |
| `components/wlogger_ui/page_status.c` | Page 4 |

### Build/config

| Path | Responsibility |
|---|---|
| `CMakeLists.txt` | Top-level ESP-IDF project file |
| `partitions.csv` | NVS + factory app (8 MB) |
| `sdkconfig.defaults` | Target = esp32c6, FAT, BLE/NimBLE, LVGL |
| `main/Kconfig.projbuild` | `CONFIG_WLOGGER_FAKE_RADIOS`, dwell-time knobs |
| `build-and-flash.sh` | Helper script (mirrors `esp32demo`) |
| `README.md` | One-page hardware/build/run summary |

### Tests (`test_host/`)

| Path | Responsibility |
|---|---|
| `test_host/CMakeLists.txt` | Host build of testable components against Unity |
| `test_host/run.sh` | One-line build + ctest invocation |
| `test_host/unity/unity.[ch]` | Vendored Unity (copy from `esp32demo`) |
| `test_host/host_shims/esp_log.h` etc. | Stub macros for IDF headers used by testable code |
| `test_host/test_csv.c` | `format_csv_line` cases |
| `test_host/test_parsers.c` | `parse_ssid`, `parse_adv_data` cases |
| `test_host/test_bloom.c` | Bloom filter accuracy bounds |
| `test_host/test_stats.c` | Rate ring, top-N maintenance |

---

## Phase 0 — Project skeleton

### Task 0.1: ESP-IDF project bootstrap

**Files:**
- Create: `CMakeLists.txt`
- Create: `partitions.csv`
- Create: `sdkconfig.defaults`
- Create: `main/CMakeLists.txt`
- Create: `main/app_main.c`
- Create: `main/Kconfig.projbuild`
- Create: `build-and-flash.sh`
- Create: `README.md`

- [ ] **Step 1: Write top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wlogger)
```

- [ ] **Step 2: Write `partitions.csv`**

```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x800000,
```

- [ ] **Step 3: Write `sdkconfig.defaults`**

```
CONFIG_IDF_TARGET="esp32c6"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_LONG_FILENAMES=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096
CONFIG_LV_USE_STDLIB_MALLOC=1
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_USE_OS=LV_OS_FREERTOS
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_FREERTOS_HZ=1000
```

- [ ] **Step 4: Write `main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event nvs_flash
)
```

- [ ] **Step 5: Write `main/app_main.c`**

```c
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wlogger";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "wlogger boot — skeleton stage");
}
```

- [ ] **Step 6: Write `main/Kconfig.projbuild`**

```
menu "wlogger"
    config WLOGGER_FAKE_RADIOS
        bool "Use synthetic radio events instead of real Wi-Fi/BLE"
        default n

    config WLOGGER_WIFI_DWELL_MS
        int "Per-channel Wi-Fi dwell time (ms)"
        default 500

    config WLOGGER_BLE_WINDOW_MS
        int "BLE scan window per cycle (ms)"
        default 2000
endmenu
```

- [ ] **Step 7: Write `build-and-flash.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
PORT="${ESP_PORT:-/dev/ttyACM0}"
idf.py set-target esp32c6
idf.py build
idf.py -p "$PORT" flash monitor
```

Make it executable: `chmod +x build-and-flash.sh`.

- [ ] **Step 8: Write a one-page `README.md`**

```markdown
# wlogger — Waveshare ESP32-C6 LCD 1.47 wireless metadata logger

See `docs/superpowers/specs/2026-05-04-wireless-metadata-logger-design.md`.

## Build & flash
    ./build-and-flash.sh

## Host-side tests
    cd test_host && ./run.sh

## Pin map
| GPIO | Function |
|------|----------|
| 4    | SD CS    |
| 5    | SD MISO  |
| 6    | SD MOSI  |
| 7    | SD SCLK  |
| 8    | WS2812 data |
| 9    | Button   |
| 14   | LCD CS   |
| 15   | LCD DC   |
| 21   | LCD RST  |
| 22   | LCD BL   |
```

- [ ] **Step 9: Verify the skeleton builds**

Run: `idf.py set-target esp32c6 && idf.py build`
Expected: `Project build complete.` with `wlogger.bin` in `build/`.

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt partitions.csv sdkconfig.defaults main/ build-and-flash.sh README.md
git commit -m "feat: ESP-IDF skeleton for wlogger (boots to nvs init)"
```

---

## Phase 1 — Pure-data modules with TDD on host

### Task 1.1: Vendored Unity + host-test scaffold

**Files:**
- Create: `test_host/CMakeLists.txt`
- Create: `test_host/run.sh`
- Copy: `test_host/unity/unity.c`, `test_host/unity/unity.h`, `test_host/unity/unity_internals.h` from `../esp32demo/test_host/unity/`
- Create: `test_host/host_shims/esp_log.h`
- Create: `test_host/test_smoke.c`

- [ ] **Step 1: Copy Unity sources**

```bash
mkdir -p test_host/unity test_host/host_shims
cp ../esp32demo/test_host/unity/unity.c test_host/unity/
cp ../esp32demo/test_host/unity/unity.h test_host/unity/
cp ../esp32demo/test_host/unity/unity_internals.h test_host/unity/
```

- [ ] **Step 2: Write `test_host/host_shims/esp_log.h`** (no-op IDF logging stub)

```c
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) (void)0
```

- [ ] **Step 3: Write `test_host/test_smoke.c`**

```c
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_smoke(void) { TEST_ASSERT_EQUAL_INT(2, 1 + 1); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
```

- [ ] **Step 4: Write `test_host/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(wlogger_host_tests C)
enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O0 -g")

include_directories(unity host_shims)
add_library(unity STATIC unity/unity.c)

add_executable(test_smoke test_smoke.c)
target_link_libraries(test_smoke unity)
add_test(NAME smoke COMMAND test_smoke)
```

- [ ] **Step 5: Write `test_host/run.sh` and chmod +x**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Run the smoke test**

Run: `cd test_host && ./run.sh`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 7: Commit**

```bash
git add test_host/
git commit -m "test: vendored Unity + host test scaffolding"
```

---

### Task 1.2: `wlogger_types.h` (shared structs)

**Files:**
- Create: `components/wlogger_types/CMakeLists.txt`
- Create: `components/wlogger_types/include/wlogger_types.h`

- [ ] **Step 1: Write `components/wlogger_types/CMakeLists.txt`**

```cmake
idf_component_register(INCLUDE_DIRS "include")
```

- [ ] **Step 2: Write `components/wlogger_types/include/wlogger_types.h`**

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WLOG_NAME_MAX  33    // 32 + null
#define WLOG_TOPN      8

typedef enum {
    DET_WIFI_AP    = 0,
    DET_WIFI_PROBE = 1,
    DET_BLE        = 2,
} det_type_t;

typedef struct {
    uint32_t   t_sec;
    det_type_t type;
    uint8_t    mac[6];
    int8_t     rssi;
    uint8_t    channel;
    char       name[WLOG_NAME_MAX];
    uint8_t    auth;          // wifi: wifi_auth_mode_t (0..); ble: 0=public 1=random
    uint16_t   mfg_id;
    bool       mac_random;
} detection_t;

typedef struct {
    uint8_t    mac[6];
    int8_t     rssi;
    char       name[WLOG_NAME_MAX];
    det_type_t type;
    uint32_t   last_seen_t;
} top_entry_t;
```

- [ ] **Step 3: Verify it compiles**

Run: `idf.py build` (should still succeed; component is header-only)

- [ ] **Step 4: Commit**

```bash
git add components/wlogger_types/
git commit -m "feat(types): shared detection_t / top_entry_t definitions"
```

---

### Task 1.3: CSV formatter (TDD)

**Files:**
- Create: `components/wlogger_csv/CMakeLists.txt`
- Create: `components/wlogger_csv/include/wlogger_csv.h`
- Create: `components/wlogger_csv/wlogger_csv.c`
- Create: `test_host/test_csv.c`
- Modify: `test_host/CMakeLists.txt` (add target)

- [ ] **Step 1: Write the failing test `test_host/test_csv.c`**

```c
#include "unity.h"
#include "wlogger_csv.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static void test_wifi_ap_basic(void) {
    detection_t d = { .t_sec=12, .type=DET_WIFI_AP, .rssi=-48, .channel=6,
        .mac={0xa4,0xc3,0xf7,0x11,0x0e,0x22}, .auth=3 /* WPA2 */ };
    strcpy(d.name, "home-net");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "12,W,a4c3f7110e22,-48,6,\"home-net\",WPA2,,\n", buf);
}

static void test_probe_random_mac(void) {
    detection_t d = { .t_sec=13, .type=DET_WIFI_PROBE, .rssi=-71, .channel=11,
        .mac={0x3e,0x2a,0x8d,0x7e,0x00,0x33}, .mac_random=true };
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "13,P,3e2a8d7e0033,-71,11,\"\",,,R\n", buf);
}

static void test_ble_with_mfg(void) {
    detection_t d = { .t_sec=13, .type=DET_BLE, .rssi=-62, .channel=38,
        .mac={0xf8,0x7b,0x5c,0x20,0x4c,0x4d}, .auth=0, .mfg_id=0x004C };
    strcpy(d.name, "AirPods Pro");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_EQUAL_STRING(
        "13,B,f87b5c204c4d,-62,38,\"AirPods Pro\",public,004C,\n", buf);
}

static void test_name_with_quote_and_comma(void) {
    detection_t d = { .t_sec=20, .type=DET_BLE, .rssi=-70, .channel=37,
        .mac={1,2,3,4,5,6}, .auth=1 };
    strcpy(d.name, "He said \"hi, there\"");
    char buf[256];
    int n = wlogger_csv_format(buf, sizeof buf, &d);
    TEST_ASSERT_EQUAL_STRING(
        "20,B,010203040506,-70,37,\"He said \"\"hi, there\"\"\",random,,\n", buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_ap_basic);
    RUN_TEST(test_probe_random_mac);
    RUN_TEST(test_ble_with_mfg);
    RUN_TEST(test_name_with_quote_and_comma);
    return UNITY_END();
}
```

- [ ] **Step 2: Write `components/wlogger_csv/include/wlogger_csv.h`**

```c
#pragma once
#include "wlogger_types.h"
#include <stddef.h>

// Formats one detection as a CSV row (with trailing \n).
// Returns number of bytes written (excluding NUL), or -1 on overflow.
int wlogger_csv_format(char *out, size_t out_size, const detection_t *d);
```

- [ ] **Step 3: Write `components/wlogger_csv/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_csv.c"
    INCLUDE_DIRS "include"
    REQUIRES wlogger_types
)
```

- [ ] **Step 4: Add a `wlogger_csv_host` library to `test_host/CMakeLists.txt`**

```cmake
include_directories(
    ../components/wlogger_types/include
    ../components/wlogger_csv/include
)

add_library(wlogger_csv_host STATIC ../components/wlogger_csv/wlogger_csv.c)
target_include_directories(wlogger_csv_host PUBLIC
    ../components/wlogger_types/include
    ../components/wlogger_csv/include
    host_shims)

add_executable(test_csv test_csv.c)
target_link_libraries(test_csv unity wlogger_csv_host)
add_test(NAME csv COMMAND test_csv)
```

- [ ] **Step 5: Run the test, watch it fail (no impl yet)**

Run: `cd test_host && ./run.sh`
Expected: `test_csv` fails to link (`undefined reference to wlogger_csv_format`).

- [ ] **Step 6: Implement `components/wlogger_csv/wlogger_csv.c`**

```c
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
    char auth_buf[16] = "";
    if (d->type == DET_BLE) auth_str = (d->auth == 0) ? "public" : "random";
    else                    auth_str = wifi_auth_str(d->auth);

    char mfg_buf[8] = "";
    if (d->type == DET_BLE && d->mfg_id != 0)
        snprintf(mfg_buf, sizeof mfg_buf, "%04X", d->mfg_id);

    int t = snprintf(out + w, n - w, ",%s,%s,%s\n",
        auth_str, mfg_buf, d->mac_random ? "R" : "");
    if (t < 0 || (size_t)t >= n - w) return -1;
    (void)auth_buf;
    return w + t;
}
```

- [ ] **Step 7: Run the test, watch it pass**

Run: `cd test_host && ./run.sh`
Expected: `100% tests passed, 0 tests failed out of 2` (smoke + csv).

- [ ] **Step 8: Commit**

```bash
git add components/wlogger_csv/ test_host/test_csv.c test_host/CMakeLists.txt
git commit -m "feat(csv): TDD'd row formatter with name/auth/mfg/random handling"
```

---

### Task 1.4: 802.11 SSID parser (TDD)

**Files:**
- Create: `components/wlogger_parsers/CMakeLists.txt`
- Create: `components/wlogger_parsers/include/wlogger_parsers.h`
- Create: `components/wlogger_parsers/wlogger_parsers.c`
- Create: `test_host/test_parsers.c`
- Modify: `test_host/CMakeLists.txt`

- [ ] **Step 1: Write `test_host/test_parsers.c` (SSID parser cases first)**

```c
#include "unity.h"
#include "wlogger_parsers.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

// SSID tag: id=0x00 len=N value=N bytes
static void test_ssid_present(void) {
    uint8_t tagged[] = {0x00, 0x08, 'h','o','m','e','-','n','e','t', 0x01, 0x00};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("home-net", name);
}

static void test_ssid_hidden_zero_length(void) {
    uint8_t tagged[] = {0x00, 0x00, 0x01, 0x00};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("", name);
}

static void test_ssid_after_other_tag(void) {
    uint8_t tagged[] = {0x05, 0x02, 0xaa, 0xbb,    // a tag we don't care about
                        0x00, 0x04, 't','e','s','t',
                        0x03, 0x01, 0x06};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("test", name);
}

static void test_ssid_truncated_length(void) {
    // Length byte claims 99 but only 4 bytes follow — must not overrun
    uint8_t tagged[] = {0x00, 0x99, 'a','b','c','d'};
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("", name);
}

static void test_ssid_with_control_byte(void) {
    uint8_t tagged[] = {0x00, 0x05, 'a',0x07,'b','c','d'};  // bell char
    char name[33] = {0};
    parse_ssid(tagged, sizeof tagged, name, sizeof name);
    TEST_ASSERT_EQUAL_STRING("a?bcd", name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_present);
    RUN_TEST(test_ssid_hidden_zero_length);
    RUN_TEST(test_ssid_after_other_tag);
    RUN_TEST(test_ssid_truncated_length);
    RUN_TEST(test_ssid_with_control_byte);
    return UNITY_END();
}
```

- [ ] **Step 2: Write `components/wlogger_parsers/include/wlogger_parsers.h`**

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

void parse_ssid(const uint8_t *tagged, size_t len, char *out, size_t out_size);

// BLE AD list parser. Pulls Local Name (0x08/0x09) and mfg-id from 0xFF.
// out_name buffer must be at least WLOG_NAME_MAX bytes. mfg_id may be NULL.
void parse_adv_data(const uint8_t *adv, size_t len,
                    char *out_name, size_t out_size, uint16_t *mfg_id);
```

- [ ] **Step 3: Write `components/wlogger_parsers/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "wlogger_parsers.c" INCLUDE_DIRS "include")
```

- [ ] **Step 4: Add to `test_host/CMakeLists.txt`**

```cmake
include_directories(../components/wlogger_parsers/include)
add_library(wlogger_parsers_host STATIC ../components/wlogger_parsers/wlogger_parsers.c)
target_include_directories(wlogger_parsers_host PUBLIC
    ../components/wlogger_parsers/include host_shims)

add_executable(test_parsers test_parsers.c)
target_link_libraries(test_parsers unity wlogger_parsers_host)
add_test(NAME parsers COMMAND test_parsers)
```

- [ ] **Step 5: Run, watch failures (link error)**

Run: `cd test_host && ./run.sh`
Expected: `undefined reference to parse_ssid` and `parse_adv_data`.

- [ ] **Step 6: Stub `parse_adv_data` and implement `parse_ssid`**

```c
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
    (void)adv; (void)len;
    if (out_size) out_name[0] = '\0';
    if (mfg_id) *mfg_id = 0;
}
```

- [ ] **Step 7: Run, parser tests pass**

Run: `cd test_host && ./run.sh`
Expected: 5 SSID tests pass.

- [ ] **Step 8: Commit**

```bash
git add components/wlogger_parsers/ test_host/test_parsers.c test_host/CMakeLists.txt
git commit -m "feat(parsers): TDD'd parse_ssid with malformed-tag tolerance"
```

---

### Task 1.5: BLE AD parser (extend Task 1.4)

**Files:**
- Modify: `test_host/test_parsers.c` (append BLE cases + RUN_TESTs)
- Modify: `components/wlogger_parsers/wlogger_parsers.c`

- [ ] **Step 1: Append BLE tests to `test_host/test_parsers.c`**

```c
// (placed above main(), additional RUN_TEST() lines added)

static void test_adv_complete_local_name(void) {
    // type=0x09 (Complete Local Name)
    uint8_t adv[] = {0x0C, 0x09, 'A','i','r','P','o','d','s',' ','P','r','o', /*flags*/ 0x02, 0x01, 0x06};
    char name[33] = {0}; uint16_t mfg = 0xFFFF;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_STRING("AirPods Pro", name);
    TEST_ASSERT_EQUAL_UINT16(0, mfg);
}

static void test_adv_short_local_name_only(void) {
    uint8_t adv[] = {0x05, 0x08, 'M','i','B','d'};
    char name[33] = {0}; uint16_t mfg = 0;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_STRING("MiBd", name);
}

static void test_adv_mfg_data_apple(void) {
    // type=0xFF, len 5 = 4-byte mfg payload + 1 type byte; first 2 bytes = company id LE
    uint8_t adv[] = {0x05, 0xFF, 0x4C, 0x00, 0x12, 0x34};
    char name[33] = {0}; uint16_t mfg = 0;
    parse_adv_data(adv, sizeof adv, name, sizeof name, &mfg);
    TEST_ASSERT_EQUAL_UINT16(0x004C, mfg);
}

static void test_adv_truncated(void) {
    uint8_t adv[] = {0x07, 0x09, 'B','i'};   // claims 7 bytes, only 4 follow
    char name[33] = {'x',0};
    parse_adv_data(adv, sizeof adv, name, sizeof name, NULL);
    TEST_ASSERT_EQUAL_STRING("", name);
}
```

Add to `main()`:
```c
RUN_TEST(test_adv_complete_local_name);
RUN_TEST(test_adv_short_local_name_only);
RUN_TEST(test_adv_mfg_data_apple);
RUN_TEST(test_adv_truncated);
```

- [ ] **Step 2: Run, watch BLE cases fail (stub returns empty)**

Run: `cd test_host && ./run.sh`
Expected: 4 new failures.

- [ ] **Step 3: Replace stub with full impl in `components/wlogger_parsers/wlogger_parsers.c`**

```c
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
```

- [ ] **Step 4: Run, all parsers pass**

Run: `cd test_host && ./run.sh`
Expected: 9 parser tests pass.

- [ ] **Step 5: Commit**

```bash
git add components/wlogger_parsers/wlogger_parsers.c test_host/test_parsers.c
git commit -m "feat(parsers): parse_adv_data extracts Local Name + mfg company id"
```

---

### Task 1.6: Bloom filter (TDD)

**Files:**
- Create: `components/wlogger_bloom/CMakeLists.txt`
- Create: `components/wlogger_bloom/include/wlogger_bloom.h`
- Create: `components/wlogger_bloom/wlogger_bloom.c`
- Create: `test_host/test_bloom.c`
- Modify: `test_host/CMakeLists.txt`

- [ ] **Step 1: Write `test_host/test_bloom.c`**

```c
#include "unity.h"
#include "wlogger_bloom.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static void mk_mac(uint8_t mac[6], uint32_t i) {
    mac[0]=0x02; mac[1]=(i>>24)&0xff; mac[2]=(i>>16)&0xff;
    mac[3]=(i>>8)&0xff;  mac[4]=i&0xff; mac[5]=0;
}

static void test_inserts_then_contains_all(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 1000; ++i) {
        mk_mac(mac, i);
        bloom_add(&b, mac);
    }
    int hits = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        mk_mac(mac, i);
        if (bloom_contains(&b, mac)) ++hits;
    }
    TEST_ASSERT_EQUAL_INT(1000, hits);
}

static void test_false_positive_rate_under_2_pct(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 5000; ++i) { mk_mac(mac, i); bloom_add(&b, mac); }
    int fp = 0;
    for (uint32_t i = 5000; i < 15000; ++i) {
        mk_mac(mac, i);
        if (bloom_contains(&b, mac)) ++fp;
    }
    // Allow up to 2% — the design target is 1% but we leave headroom.
    TEST_ASSERT_TRUE(fp < 200);
}

static void test_count_estimate_within_5_pct(void) {
    bloom_t b; bloom_init(&b);
    uint8_t mac[6];
    for (uint32_t i = 0; i < 2000; ++i) { mk_mac(mac, i); bloom_add(&b, mac); }
    uint32_t est = bloom_count_estimate(&b);
    TEST_ASSERT_TRUE(est >= 1900 && est <= 2100);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inserts_then_contains_all);
    RUN_TEST(test_false_positive_rate_under_2_pct);
    RUN_TEST(test_count_estimate_within_5_pct);
    return UNITY_END();
}
```

- [ ] **Step 2: Write `components/wlogger_bloom/include/wlogger_bloom.h`**

```c
#pragma once
#include <stdint.h>
#include <stddef.h>

#define BLOOM_BITS    (16 * 8 * 1024)   // 16 KB = 131072 bits
#define BLOOM_BYTES   (BLOOM_BITS / 8)
#define BLOOM_K       7

typedef struct {
    uint8_t  bits[BLOOM_BYTES];
    uint32_t inserted;
} bloom_t;

void     bloom_init(bloom_t *b);
void     bloom_add(bloom_t *b, const uint8_t mac[6]);
int      bloom_contains(const bloom_t *b, const uint8_t mac[6]);
uint32_t bloom_count_estimate(const bloom_t *b);
```

- [ ] **Step 3: Write `components/wlogger_bloom/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "wlogger_bloom.c" INCLUDE_DIRS "include")
```

- [ ] **Step 4: Add to `test_host/CMakeLists.txt`**

```cmake
include_directories(../components/wlogger_bloom/include)
add_library(wlogger_bloom_host STATIC ../components/wlogger_bloom/wlogger_bloom.c)
target_include_directories(wlogger_bloom_host PUBLIC
    ../components/wlogger_bloom/include host_shims)

add_executable(test_bloom test_bloom.c)
target_link_libraries(test_bloom unity wlogger_bloom_host)
add_test(NAME bloom COMMAND test_bloom)
```

- [ ] **Step 5: Run, watch link failure**

Run: `cd test_host && ./run.sh`

- [ ] **Step 6: Implement `components/wlogger_bloom/wlogger_bloom.c`**

```c
#include "wlogger_bloom.h"
#include <string.h>
#include <math.h>

// FNV-1a 32-bit, then split into K hash positions via double-hashing.
static uint32_t fnv1a(const uint8_t *p, size_t n, uint32_t seed) {
    uint32_t h = 0x811C9DC5u ^ seed;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x01000193u; }
    return h;
}

void bloom_init(bloom_t *b) {
    memset(b->bits, 0, BLOOM_BYTES);
    b->inserted = 0;
}

static void positions(const uint8_t mac[6], uint32_t out[BLOOM_K]) {
    uint32_t h1 = fnv1a(mac, 6, 0);
    uint32_t h2 = fnv1a(mac, 6, 0xDEADBEEF);
    for (int i = 0; i < BLOOM_K; ++i)
        out[i] = (h1 + (uint32_t)i * h2) % BLOOM_BITS;
}

void bloom_add(bloom_t *b, const uint8_t mac[6]) {
    uint32_t pos[BLOOM_K];
    positions(mac, pos);
    int already = 1;
    for (int i = 0; i < BLOOM_K; ++i) {
        uint32_t p = pos[i];
        if (!(b->bits[p >> 3] & (1u << (p & 7)))) {
            already = 0;
            b->bits[p >> 3] |= (1u << (p & 7));
        }
    }
    if (!already) b->inserted++;
}

int bloom_contains(const bloom_t *b, const uint8_t mac[6]) {
    uint32_t pos[BLOOM_K];
    positions(mac, pos);
    for (int i = 0; i < BLOOM_K; ++i) {
        uint32_t p = pos[i];
        if (!(b->bits[p >> 3] & (1u << (p & 7)))) return 0;
    }
    return 1;
}

uint32_t bloom_count_estimate(const bloom_t *b) {
    // Swamidass & Baldi: n ≈ -(m/k) * ln(1 - X/m), where X = bits set.
    uint32_t set = 0;
    for (size_t i = 0; i < BLOOM_BYTES; ++i)
        for (int j = 0; j < 8; ++j) if (b->bits[i] & (1u << j)) set++;
    if (set == 0) return 0;
    double m = (double)BLOOM_BITS, k = (double)BLOOM_K, X = (double)set;
    if (X >= m) return b->inserted;          // saturated, fall back to counter
    double n = -(m / k) * log(1.0 - X / m);
    return (uint32_t)(n + 0.5);
}
```

- [ ] **Step 7: Add `-lm` for the math library in test_host CMakeLists.txt**

```cmake
target_link_libraries(test_bloom unity wlogger_bloom_host m)
```

- [ ] **Step 8: Run, all bloom tests pass**

Run: `cd test_host && ./run.sh`

- [ ] **Step 9: Commit**

```bash
git add components/wlogger_bloom/ test_host/test_bloom.c test_host/CMakeLists.txt
git commit -m "feat(bloom): 16KB bloom filter with FP rate < 2% at 5k inserts"
```

---

### Task 1.7: Stats helpers (TDD: rate ring + top-N)

**Files:**
- Create: `components/wlogger_stats/CMakeLists.txt`
- Create: `components/wlogger_stats/include/wlogger_stats.h`
- Create: `components/wlogger_stats/wlogger_stats.c`
- Create: `test_host/test_stats.c`
- Create: `test_host/host_shims/freertos_shim.h`
- Modify: `test_host/CMakeLists.txt`

- [ ] **Step 1: Write `test_host/host_shims/freertos_shim.h`** (no-op mutex stub for host build)

```c
#pragma once
#include <stdbool.h>
typedef int *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int x; return &x; }
static inline bool xSemaphoreTake(SemaphoreHandle_t s, int t) { (void)s;(void)t; return true; }
static inline void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
#define portMAX_DELAY 0xffffffff
```

- [ ] **Step 2: Write `components/wlogger_stats/include/wlogger_stats.h`**

```c
#pragma once
#include "wlogger_types.h"
#include "wlogger_bloom.h"

#ifdef __has_include
#  if __has_include("freertos_shim.h")
#    include "freertos_shim.h"
#  else
#    include "freertos/FreeRTOS.h"
#    include "freertos/semphr.h"
#  endif
#endif

typedef struct {
    bloom_t           bloom_wifi;
    bloom_t           bloom_probe;
    bloom_t           bloom_ble;
    uint64_t          total_events;
    uint64_t          dropped_events;
    uint16_t          rate_per_min[60];
    uint8_t           rate_head;
    uint32_t          last_minute_t;
    char              current_file[40];
    uint32_t          current_file_bytes;
    uint64_t          sd_free_bytes;
    bool              sd_ok, wifi_ok, ble_ok;
    top_entry_t       strongest[WLOG_TOPN];
    SemaphoreHandle_t mtx;
} stats_t;

void     stats_init(stats_t *s);
void     stats_record_event(stats_t *s, const detection_t *d);
void     stats_increment_rate_bucket(stats_t *s, uint32_t now_t_sec);
uint16_t stats_rate_last_minute(const stats_t *s);
void     stats_update_topn(stats_t *s, const detection_t *d);
```

- [ ] **Step 3: Write `test_host/test_stats.c`** (rate ring + top-N maintenance)

```c
#include "unity.h"
#include "wlogger_stats.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

static detection_t mk(int rssi, uint8_t mac_last) {
    detection_t d = { .type = DET_BLE, .rssi = rssi, .channel = 37 };
    d.mac[5] = mac_last;
    return d;
}

static void test_topn_keeps_strongest(void) {
    stats_t s; stats_init(&s);
    int rssis[] = { -90, -50, -80, -40, -70, -60, -55, -85, -45, -75 };
    for (int i = 0; i < 10; ++i) {
        detection_t d = mk(rssis[i], i);
        stats_update_topn(&s, &d);
    }
    int best = -127;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].rssi > best) best = s.strongest[i].rssi;
    TEST_ASSERT_EQUAL_INT8(-40, best);
    int worst = 0;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].rssi < worst) worst = s.strongest[i].rssi;
    TEST_ASSERT_TRUE(worst > -90);   // -90 should have been displaced
}

static void test_topn_dedupes_same_mac(void) {
    stats_t s; stats_init(&s);
    detection_t a = mk(-70, 1);
    detection_t b = mk(-50, 1);   // same MAC, stronger
    stats_update_topn(&s, &a);
    stats_update_topn(&s, &b);
    int matches = 0;
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (s.strongest[i].mac[5] == 1) ++matches;
    TEST_ASSERT_EQUAL_INT(1, matches);
}

static void test_rate_bucket_increments(void) {
    stats_t s; stats_init(&s);
    s.last_minute_t = 0;
    stats_increment_rate_bucket(&s, 5);   // same minute as init
    stats_increment_rate_bucket(&s, 10);
    TEST_ASSERT_EQUAL_UINT16(2, stats_rate_last_minute(&s));

    stats_increment_rate_bucket(&s, 65);  // next minute
    TEST_ASSERT_EQUAL_UINT16(1, stats_rate_last_minute(&s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_topn_keeps_strongest);
    RUN_TEST(test_topn_dedupes_same_mac);
    RUN_TEST(test_rate_bucket_increments);
    return UNITY_END();
}
```

- [ ] **Step 4: Write `components/wlogger_stats/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_stats.c"
    INCLUDE_DIRS "include"
    REQUIRES wlogger_types wlogger_bloom
)
```

- [ ] **Step 5: Add to `test_host/CMakeLists.txt`**

```cmake
include_directories(../components/wlogger_stats/include)
add_library(wlogger_stats_host STATIC ../components/wlogger_stats/wlogger_stats.c)
target_include_directories(wlogger_stats_host PUBLIC
    ../components/wlogger_types/include
    ../components/wlogger_bloom/include
    ../components/wlogger_stats/include
    host_shims)

add_executable(test_stats test_stats.c)
target_link_libraries(test_stats unity wlogger_stats_host wlogger_bloom_host m)
add_test(NAME stats COMMAND test_stats)
```

- [ ] **Step 6: Run, watch link/test failure**

Run: `cd test_host && ./run.sh`

- [ ] **Step 7: Implement `components/wlogger_stats/wlogger_stats.c`**

```c
#include "wlogger_stats.h"
#include <string.h>

void stats_init(stats_t *s) {
    memset(s, 0, sizeof *s);
    bloom_init(&s->bloom_wifi);
    bloom_init(&s->bloom_probe);
    bloom_init(&s->bloom_ble);
    s->mtx = xSemaphoreCreateMutex();
    s->sd_ok = true;
}

static int find_mac(const stats_t *s, const uint8_t mac[6]) {
    for (int i = 0; i < WLOG_TOPN; ++i)
        if (memcmp(s->strongest[i].mac, mac, 6) == 0) return i;
    return -1;
}

static int find_weakest(const stats_t *s) {
    int idx = 0; int8_t worst = 0;
    for (int i = 0; i < WLOG_TOPN; ++i) {
        if (s->strongest[i].last_seen_t == 0) return i;
        if (s->strongest[i].rssi < worst) { worst = s->strongest[i].rssi; idx = i; }
    }
    return idx;
}

void stats_update_topn(stats_t *s, const detection_t *d) {
    int existing = find_mac(s, d->mac);
    if (existing >= 0) {
        if (d->rssi > s->strongest[existing].rssi) {
            s->strongest[existing].rssi = d->rssi;
            s->strongest[existing].last_seen_t = d->t_sec;
            strncpy(s->strongest[existing].name, d->name, WLOG_NAME_MAX - 1);
        }
        return;
    }
    int slot = find_weakest(s);
    if (s->strongest[slot].last_seen_t == 0 || d->rssi > s->strongest[slot].rssi) {
        memcpy(s->strongest[slot].mac, d->mac, 6);
        s->strongest[slot].rssi = d->rssi;
        s->strongest[slot].type = d->type;
        s->strongest[slot].last_seen_t = d->t_sec;
        strncpy(s->strongest[slot].name, d->name, WLOG_NAME_MAX - 1);
        s->strongest[slot].name[WLOG_NAME_MAX - 1] = 0;
    }
}

void stats_increment_rate_bucket(stats_t *s, uint32_t now_t_sec) {
    uint32_t now_min = now_t_sec / 60;
    uint32_t last_min = s->last_minute_t / 60;
    if (s->last_minute_t == 0 && s->rate_per_min[s->rate_head] == 0) {
        s->last_minute_t = now_t_sec;
        last_min = now_min;
    }
    while (now_min > last_min) {
        s->rate_head = (s->rate_head + 1) % 60;
        s->rate_per_min[s->rate_head] = 0;
        last_min++;
    }
    s->last_minute_t = now_t_sec;
    s->rate_per_min[s->rate_head]++;
}

uint16_t stats_rate_last_minute(const stats_t *s) {
    return s->rate_per_min[s->rate_head];
}

void stats_record_event(stats_t *s, const detection_t *d) {
    if (xSemaphoreTake(s->mtx, portMAX_DELAY) != true) return;
    s->total_events++;
    switch (d->type) {
        case DET_WIFI_AP:    bloom_add(&s->bloom_wifi,  d->mac); break;
        case DET_WIFI_PROBE: bloom_add(&s->bloom_probe, d->mac); break;
        case DET_BLE:        bloom_add(&s->bloom_ble,   d->mac); break;
    }
    stats_update_topn(s, d);
    stats_increment_rate_bucket(s, d->t_sec);
    xSemaphoreGive(s->mtx);
}
```

- [ ] **Step 8: Run, stats tests pass**

Run: `cd test_host && ./run.sh`
Expected: 3 stats tests pass.

- [ ] **Step 9: Commit**

```bash
git add components/wlogger_stats/ test_host/test_stats.c test_host/host_shims/freertos_shim.h test_host/CMakeLists.txt
git commit -m "feat(stats): TDD'd top-N tracker, per-minute rate ring, mutex-guarded record"
```

---

## Phase 2 — Hardware drivers

### Task 2.1: SD card mount + file rotation

**Files:**
- Create: `components/wlogger_sd/CMakeLists.txt`
- Create: `components/wlogger_sd/include/wlogger_sd.h`
- Create: `components/wlogger_sd/wlogger_sd.c`

- [ ] **Step 1: Write `components/wlogger_sd/include/wlogger_sd.h`**

```c
#pragma once
#include "wlogger_types.h"
#include "esp_err.h"
#include <stdio.h>

#define WLOG_SD_MOUNT  "/sdcard"
#define WLOG_DIR       "/sdcard/wlogger"

typedef struct {
    FILE     *fp;
    uint16_t  session;
    uint16_t  index;
    uint32_t  bytes_written;
    uint32_t  opened_at_t_sec;
    char      path[40];
} wlogger_file_t;

esp_err_t wlogger_sd_mount(void);
esp_err_t wlogger_sd_open_new_file(wlogger_file_t *f, uint32_t now_t_sec, const char *cycle_label);
esp_err_t wlogger_sd_close(wlogger_file_t *f);
bool      wlogger_sd_should_rotate(const wlogger_file_t *f, uint32_t now_t_sec);
uint64_t  wlogger_sd_free_bytes(void);
```

- [ ] **Step 2: Write `components/wlogger_sd/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_sd.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs sdmmc esp_driver_spi vfs wlogger_types
)
```

- [ ] **Step 3: Write `components/wlogger_sd/wlogger_sd.c`**

```c
#include "wlogger_sd.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "sd";

#define PIN_CS    4
#define PIN_MISO  5
#define PIN_MOSI  6
#define PIN_SCLK  7
#define HOST_ID   SPI2_HOST

#define ROT_BYTES   (5UL * 1024 * 1024)
#define ROT_SECONDS 3600

esp_err_t wlogger_sd_mount(void) {
    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO, .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(HOST_ID, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) { ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err)); return err; }

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = PIN_CS;
    dev.host_id = HOST_ID;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HOST_ID;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount(WLOG_SD_MOUNT, &host, &dev, &mcfg, &card);
    if (err != ESP_OK) { ESP_LOGE(TAG, "mount: %s", esp_err_to_name(err)); return err; }

    mkdir(WLOG_DIR, 0777);
    return ESP_OK;
}

static uint16_t scan_max_session(void) {
    DIR *d = opendir(WLOG_DIR);
    if (!d) return 0;
    uint16_t maxs = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        unsigned int s, i;
        if (sscanf(e->d_name, "log_%u_%u.csv", &s, &i) == 2 && s <= 0xFFFF)
            if ((uint16_t)s > maxs) maxs = s;
    }
    closedir(d);
    return maxs;
}

esp_err_t wlogger_sd_open_new_file(wlogger_file_t *f, uint32_t now, const char *cycle) {
    if (f->fp == NULL) f->session = scan_max_session() + 1;
    f->index = (f->fp == NULL) ? 1 : f->index + 1;
    f->bytes_written = 0;
    f->opened_at_t_sec = now;
    snprintf(f->path, sizeof f->path, WLOG_DIR "/log_%04u_%04u.csv", f->session, f->index);
    f->fp = fopen(f->path, "w");
    if (!f->fp) return ESP_FAIL;
    int n = fprintf(f->fp,
        "# session=%u start_t_sec=%u fw=wlogger-0.1 cycle=%s\n"
        "t_sec,type,mac,rssi,ch,name,auth,mfg,meta\n",
        f->session, (unsigned)now, cycle ? cycle : "balanced");
    if (n > 0) f->bytes_written += n;
    return ESP_OK;
}

esp_err_t wlogger_sd_close(wlogger_file_t *f) {
    if (f->fp) { fflush(f->fp); fclose(f->fp); f->fp = NULL; }
    return ESP_OK;
}

bool wlogger_sd_should_rotate(const wlogger_file_t *f, uint32_t now) {
    if (!f->fp) return true;
    if (f->bytes_written >= ROT_BYTES) return true;
    if (now - f->opened_at_t_sec >= ROT_SECONDS) return true;
    return false;
}

uint64_t wlogger_sd_free_bytes(void) {
    FATFS *fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return 0;
    return (uint64_t)free_clusters * fs->csize * 512;
}
```

- [ ] **Step 4: Build to confirm component links**

Run: `idf.py build`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add components/wlogger_sd/
git commit -m "feat(sd): mount FAT over SPI, session-numbered file rotation"
```

---

### Task 2.2: LCD driver + LVGL display attach

**Files:**
- Create: `components/wlogger_lcd/CMakeLists.txt`
- Create: `components/wlogger_lcd/include/wlogger_lcd.h`
- Create: `components/wlogger_lcd/wlogger_lcd.c`

- [ ] **Step 1: Write `components/wlogger_lcd/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_lcd.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver lvgl
)
```

- [ ] **Step 2: Write `components/wlogger_lcd/include/wlogger_lcd.h`**

```c
#pragma once
#include "esp_err.h"
#include "lvgl.h"

#define WLOG_LCD_W  172
#define WLOG_LCD_H  320

esp_err_t wlogger_lcd_init(void);
lv_display_t *wlogger_lcd_lv_display(void);
void wlogger_lcd_tick_ms(uint32_t ms);
```

- [ ] **Step 3: Write `components/wlogger_lcd/wlogger_lcd.c`**

```c
#include "wlogger_lcd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lcd";
#define PIN_BL   22
#define PIN_RST  21
#define PIN_DC   15
#define PIN_CS   14
// LCD shares SPI2 bus with SD; bus is initialized by wlogger_sd_mount before us.
#define HOST_ID  SPI2_HOST

#define X_GAP    34
#define Y_GAP     0

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static lv_display_t *s_disp = NULL;
static lv_color_t s_buf1[WLOG_LCD_W * 40];
static lv_color_t s_buf2[WLOG_LCD_W * 40];

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x1 + w, area->y1 + h, px);
    lv_display_flush_ready(disp);
}

esp_err_t wlogger_lcd_init(void) {
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(PIN_BL, 1);

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC,
        .spi_mode = 0, .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HOST_ID, &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, X_GAP, Y_GAP);
    esp_lcd_panel_disp_on_off(s_panel, true);

    lv_init();
    s_disp = lv_display_create(WLOG_LCD_W, WLOG_LCD_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof s_buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    return ESP_OK;
}

lv_display_t *wlogger_lcd_lv_display(void) { return s_disp; }
void wlogger_lcd_tick_ms(uint32_t ms) { lv_tick_inc(ms); }
```

- [ ] **Step 4: Add LVGL via ESP-IDF Component Manager**

Run: `idf.py add-dependency "lvgl/lvgl^9.0.0"`
Expected: `dependencies.lock` updated.

- [ ] **Step 5: Build**

Run: `idf.py build`
Expected: succeeds.

- [ ] **Step 6: Commit**

```bash
git add components/wlogger_lcd/ idf_component.yml dependencies.lock
git commit -m "feat(lcd): ST7789 panel + LVGL 9 partial-buffer display"
```

---

### Task 2.3: WS2812 LED driver

**Files:**
- Create: `components/wlogger_led/CMakeLists.txt`
- Create: `components/wlogger_led/include/wlogger_led.h`
- Create: `components/wlogger_led/wlogger_led.c`

- [ ] **Step 1: Write `components/wlogger_led/include/wlogger_led.h`**

```c
#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef enum { LED_PHASE_WIFI = 0, LED_PHASE_BLE = 1 } led_phase_t;
typedef enum {
    LED_EVT_NONE = 0,
    LED_EVT_DROP,         // 50 ms red flash
    LED_EVT_ROTATED,      // 2 s yellow breath
    LED_EVT_LOW_MEM,      // 200 ms orange flash
    LED_EVT_FAULT,        // solid red
} led_event_t;

esp_err_t wlogger_led_init(void);
// rate_per_min: log-mapped to brightness 5..60%; phase chooses hue.
void wlogger_led_set(led_phase_t phase, uint16_t rate_per_min, led_event_t evt);
```

- [ ] **Step 2: Write `components/wlogger_led/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_led.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip
)
```

- [ ] **Step 3: Add `led_strip` dependency**

Run: `idf.py add-dependency "espressif/led_strip^3.0.0"`

- [ ] **Step 4: Write `components/wlogger_led/wlogger_led.c`**

```c
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
    float rf, gf, bf;
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
        // log-mapped brightness: 0..60+ → 0.05..0.60
        float br = 0.05f + 0.55f * (logf(1 + (float)rate) / logf(1 + 60.f));
        if (br > 0.60f) br = 0.60f;
        float hue = (phase == LED_PHASE_WIFI) ? 180.f : 300.f;
        hsv_to_rgb(hue, 1.f, br, &r, &g, &b);
    }
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}
```

- [ ] **Step 5: Build**

Run: `idf.py build`

- [ ] **Step 6: Commit**

```bash
git add components/wlogger_led/ idf_component.yml dependencies.lock
git commit -m "feat(led): WS2812 status policy (hue=phase, brightness=rate, events)"
```

---

### Task 2.4: Button driver (debounce + short/long press)

**Files:**
- Create: `components/wlogger_button/CMakeLists.txt`
- Create: `components/wlogger_button/include/wlogger_button.h`
- Create: `components/wlogger_button/wlogger_button.c`

- [ ] **Step 1: Write `components/wlogger_button/include/wlogger_button.h`**

```c
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum { BTN_NONE = 0, BTN_SHORT = 1, BTN_LONG = 2 } btn_event_t;

esp_err_t    wlogger_button_init(void);
btn_event_t  wlogger_button_poll(uint32_t poll_period_ms);
```

- [ ] **Step 2: Write `components/wlogger_button/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "wlogger_button.c" INCLUDE_DIRS "include" REQUIRES driver)
```

- [ ] **Step 3: Write `components/wlogger_button/wlogger_button.c`**

```c
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
```

- [ ] **Step 4: Build**

Run: `idf.py build`

- [ ] **Step 5: Commit**

```bash
git add components/wlogger_button/
git commit -m "feat(button): polled debounce with short/long press classification"
```

---

## Phase 3 — Scan engines

### Task 3.1: Wi-Fi promiscuous channel-hop sweep

**Files:**
- Create: `components/wlogger_scan/CMakeLists.txt`
- Create: `components/wlogger_scan/include/wlogger_scan.h`
- Create: `components/wlogger_scan/wlogger_scan_wifi.c`
- Create: `components/wlogger_scan/wlogger_scan_internal.h`

- [ ] **Step 1: Write `wlogger_scan/include/wlogger_scan.h`**

```c
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wlogger_types.h"
#include "esp_err.h"

esp_err_t wlogger_scan_init(QueueHandle_t detect_q);
esp_err_t wlogger_scan_start_task(void);
bool      wlogger_scan_wifi_ok(void);
bool      wlogger_scan_ble_ok(void);
```

- [ ] **Step 2: Write `wlogger_scan_internal.h`**

```c
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wlogger_types.h"

extern QueueHandle_t g_detect_q;

esp_err_t wlogger_scan_wifi_init(void);
void      wlogger_scan_wifi_sweep(void);   // blocks for full 13-channel hop

esp_err_t wlogger_scan_ble_init(void);
void      wlogger_scan_ble_window(uint32_t ms);

esp_err_t wlogger_scan_fake_init(void);    // CONFIG_WLOGGER_FAKE_RADIOS only
void      wlogger_scan_fake_burst(uint32_t ms);
```

- [ ] **Step 3: Write `components/wlogger_scan/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_scan_wifi.c" "wlogger_scan_ble.c" "wlogger_scan_task.c"
         "wlogger_scan_fake.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES wlogger_types esp_wifi esp_event nvs_flash bt esp_timer
)
```

- [ ] **Step 4: Write `wlogger_scan_wifi.c`**

```c
#include "wlogger_scan_internal.h"
#include "wlogger_parsers.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "scan-wifi";
QueueHandle_t g_detect_q = NULL;

static void IRAM_ATTR promisc_cb(void *buf, wifi_promiscuous_pkt_type_t t) {
    if (t != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) return;
    uint16_t fc = p[0] | (p[1] << 8);
    uint8_t  st = (fc >> 4) & 0xF;

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .rssi    = pkt->rx_ctrl.rssi,
        .channel = pkt->rx_ctrl.channel,
    };

    if (st == 8) {
        d.type = DET_WIFI_AP;
        memcpy(d.mac, p + 16, 6);
        // tagged params start at 36 (24 hdr + 12 fixed beacon body)
        if (pkt->rx_ctrl.sig_len > 36)
            parse_ssid(p + 36, pkt->rx_ctrl.sig_len - 36, d.name, sizeof d.name);
    } else if (st == 4) {
        d.type = DET_WIFI_PROBE;
        memcpy(d.mac, p + 10, 6);
        if (pkt->rx_ctrl.sig_len > 24)
            parse_ssid(p + 24, pkt->rx_ctrl.sig_len - 24, d.name, sizeof d.name);
    } else {
        return;
    }
    d.mac_random = (d.mac[0] & 0x02) != 0;

    if (g_detect_q) {
        BaseType_t hp = pdFALSE;
        xQueueSendFromISR(g_detect_q, &d, &hp);
        portYIELD_FROM_ISR(hp);
    }
}

esp_err_t wlogger_scan_wifi_init(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "init: %s", esp_err_to_name(err)); return err; }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promisc_cb));
    wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&f));
    return ESP_OK;
}

void wlogger_scan_wifi_sweep(void) {
    esp_wifi_set_promiscuous(true);
    for (int ch = 1; ch <= 13; ++ch) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WLOGGER_WIFI_DWELL_MS));
    }
    esp_wifi_set_promiscuous(false);
}
```

- [ ] **Step 5: Build (BLE / fake stubs still empty)**

Run: `idf.py build`
Expected: succeeds (BLE/fake just need to compile from stubs in next tasks).

- [ ] **Step 6: Commit**

```bash
git add components/wlogger_scan/
git commit -m "feat(scan-wifi): promiscuous + 13-channel hop with frame parsing"
```

---

### Task 3.2: BLE NimBLE scan window

**Files:**
- Modify: `components/wlogger_scan/wlogger_scan_ble.c`

- [ ] **Step 1: Write `components/wlogger_scan/wlogger_scan_ble.c`**

```c
#include "wlogger_scan_internal.h"
#include "wlogger_parsers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "scan-ble";
static EventGroupHandle_t s_evg;
#define BIT_DONE BIT0

static int gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xEventGroupSetBits(s_evg, BIT_DONE);
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    detection_t d = {
        .t_sec   = (uint32_t)(esp_timer_get_time() / 1000000),
        .type    = DET_BLE,
        .rssi    = event->disc.rssi,
        .channel = 37,
        .auth    = event->disc.addr.type,
    };
    memcpy(d.mac, event->disc.addr.val, 6);
    d.mac_random = (event->disc.addr.type == 1);
    parse_adv_data(event->disc.data, event->disc.length_data,
                   d.name, sizeof d.name, &d.mfg_id);

    if (g_detect_q) xQueueSend(g_detect_q, &d, 0);
    return 0;
}

static void on_sync(void) { /* nothing — we kick scans manually */ }
static void host_task(void *p) { (void)p; nimble_port_run(); nimble_port_freertos_deinit(); }

esp_err_t wlogger_scan_ble_init(void) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = on_sync;
    s_evg = xEventGroupCreate();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void wlogger_scan_ble_window(uint32_t dur_ms) {
    if (!ble_hs_synced()) return;
    uint8_t addr_type = 0;
    ble_hs_id_infer_auto(0, &addr_type);
    struct ble_gap_disc_params p = {
        .itvl   = 0x0010,           // 10 ms
        .window = 0x0010,
        .filter_policy = 0,
        .limited = 0, .passive = 0,
        .filter_duplicates = 0,
    };
    xEventGroupClearBits(s_evg, BIT_DONE);
    ble_gap_disc(addr_type, dur_ms, &p, gap_event, NULL);
    xEventGroupWaitBits(s_evg, BIT_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(dur_ms + 500));
    ble_gap_disc_cancel();
}
```

- [ ] **Step 2: Build**

Run: `idf.py build`

- [ ] **Step 3: Commit**

```bash
git add components/wlogger_scan/wlogger_scan_ble.c
git commit -m "feat(scan-ble): NimBLE active-scan window with AD parsing"
```

---

### Task 3.3: Fake-radio synthetic generator

**Files:**
- Modify: `components/wlogger_scan/wlogger_scan_fake.c`

- [ ] **Step 1: Write the synthetic generator**

```c
#include "wlogger_scan_internal.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <string.h>

esp_err_t wlogger_scan_fake_init(void) { return ESP_OK; }

void wlogger_scan_fake_burst(uint32_t ms) {
    uint32_t until = (uint32_t)(esp_timer_get_time() / 1000) + ms;
    while ((uint32_t)(esp_timer_get_time() / 1000) < until) {
        detection_t d = { .t_sec = (uint32_t)(esp_timer_get_time() / 1000000) };
        uint32_t r = esp_random();
        d.type    = (r & 1) ? DET_BLE : ((r & 2) ? DET_WIFI_PROBE : DET_WIFI_AP);
        d.rssi    = -30 - (int8_t)(esp_random() % 60);
        d.channel = (d.type == DET_BLE) ? 37 + (esp_random() % 3) : 1 + (esp_random() % 13);
        for (int i = 0; i < 6; ++i) d.mac[i] = (uint8_t)(esp_random() & 0xff);
        d.mac_random = (d.mac[0] & 0x02) != 0;
        const char *names[] = { "", "home-net", "guest", "AirPods", "Tile", "Mi Band", "" };
        strncpy(d.name, names[esp_random() % 7], sizeof d.name - 1);
        d.mfg_id = (esp_random() & 1) ? 0x004C : 0;

        if (g_detect_q) xQueueSend(g_detect_q, &d, 0);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
```

- [ ] **Step 2: Build**

Run: `idf.py build`

- [ ] **Step 3: Commit**

```bash
git add components/wlogger_scan/wlogger_scan_fake.c
git commit -m "feat(scan-fake): synthetic event generator for offline development"
```

---

### Task 3.4: Scan task state machine

**Files:**
- Modify: `components/wlogger_scan/wlogger_scan_task.c`

- [ ] **Step 1: Write the scan task**

```c
#include "wlogger_scan.h"
#include "wlogger_scan_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "scan";
static bool s_wifi_ok = false, s_ble_ok = false;

bool wlogger_scan_wifi_ok(void) { return s_wifi_ok; }
bool wlogger_scan_ble_ok(void)  { return s_ble_ok; }

esp_err_t wlogger_scan_init(QueueHandle_t q) {
    g_detect_q = q;
#ifdef CONFIG_WLOGGER_FAKE_RADIOS
    if (wlogger_scan_fake_init() == ESP_OK) { s_wifi_ok = s_ble_ok = true; return ESP_OK; }
    return ESP_FAIL;
#else
    if (wlogger_scan_wifi_init() == ESP_OK) s_wifi_ok = true;
    if (wlogger_scan_ble_init()  == ESP_OK) s_ble_ok  = true;
    if (!s_wifi_ok && !s_ble_ok) return ESP_FAIL;
    return ESP_OK;
#endif
}

static void scan_task(void *_) {
    for (;;) {
#ifdef CONFIG_WLOGGER_FAKE_RADIOS
        wlogger_scan_fake_burst(6500);
        vTaskDelay(pdMS_TO_TICKS(100));
        wlogger_scan_fake_burst(2000);
        vTaskDelay(pdMS_TO_TICKS(100));
#else
        if (s_wifi_ok) wlogger_scan_wifi_sweep();
        if (s_ble_ok)  wlogger_scan_ble_window(CONFIG_WLOGGER_BLE_WINDOW_MS);
#endif
    }
}

esp_err_t wlogger_scan_start_task(void) {
    BaseType_t r = xTaskCreate(scan_task, "scan", 4096, NULL, 5, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 2: Build**

Run: `idf.py build`

- [ ] **Step 3: Commit**

```bash
git add components/wlogger_scan/wlogger_scan_task.c components/wlogger_scan/include/wlogger_scan.h components/wlogger_scan/wlogger_scan_internal.h
git commit -m "feat(scan): two-phase Wi-Fi/BLE state machine task"
```

---

## Phase 4 — Writer task and UI

### Task 4.1: Writer task (drain queue, batch CSV, rotate)

**Files:**
- Create: `components/wlogger_writer/CMakeLists.txt`
- Create: `components/wlogger_writer/include/wlogger_writer.h`
- Create: `components/wlogger_writer/wlogger_writer.c`

- [ ] **Step 1: Write `wlogger_writer/include/wlogger_writer.h`**

```c
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wlogger_stats.h"
#include "esp_err.h"

#define RECENT_SIZE 32

typedef struct {
    detection_t entries[RECENT_SIZE];
    int         head;     // next write
    int         count;    // up to RECENT_SIZE
    SemaphoreHandle_t mtx;
} recent_q_t;

esp_err_t wlogger_writer_init(QueueHandle_t detect_q, stats_t *stats, recent_q_t *recent);
esp_err_t wlogger_writer_start_task(void);
void      wlogger_writer_request_rotate(void);   // long-press path

// recent_q helpers (also used by UI page)
void recent_q_init(recent_q_t *r);
void recent_q_push(recent_q_t *r, const detection_t *d);
int  recent_q_snapshot(const recent_q_t *r, detection_t *out, int max);
```

- [ ] **Step 2: Write `components/wlogger_writer/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_writer.c"
    INCLUDE_DIRS "include"
    REQUIRES wlogger_types wlogger_csv wlogger_stats wlogger_sd
)
```

- [ ] **Step 3: Write `wlogger_writer.c`**

```c
#include "wlogger_writer.h"
#include "wlogger_csv.h"
#include "wlogger_sd.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>
#include <atomic>

static const char *TAG = "writer";

static QueueHandle_t  s_q;
static stats_t       *s_stats;
static recent_q_t    *s_recent;
static wlogger_file_t s_file = {0};
static volatile bool  s_rotate_req = false;

void recent_q_init(recent_q_t *r) {
    r->head = 0; r->count = 0;
    r->mtx = xSemaphoreCreateMutex();
}
void recent_q_push(recent_q_t *r, const detection_t *d) {
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    r->entries[r->head] = *d;
    r->head = (r->head + 1) % RECENT_SIZE;
    if (r->count < RECENT_SIZE) r->count++;
    xSemaphoreGive(r->mtx);
}
int recent_q_snapshot(const recent_q_t *r, detection_t *out, int max) {
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    int n = r->count < max ? r->count : max;
    int start = (r->head - n + RECENT_SIZE) % RECENT_SIZE;
    for (int i = 0; i < n; ++i) out[i] = r->entries[(start + i) % RECENT_SIZE];
    xSemaphoreGive(r->mtx);
    return n;
}

esp_err_t wlogger_writer_init(QueueHandle_t q, stats_t *st, recent_q_t *r) {
    s_q = q; s_stats = st; s_recent = r;
    return ESP_OK;
}

void wlogger_writer_request_rotate(void) { s_rotate_req = true; }

static void writer_task(void *_) {
    detection_t d;
    char buf[4096];
    size_t buf_len = 0;
    TickType_t last_flush = xTaskGetTickCount();

    if (wlogger_sd_open_new_file(&s_file, 0, "balanced") != ESP_OK) {
        ESP_LOGE(TAG, "initial file open failed; writer will idle");
    }

    for (;;) {
        if (xQueueReceive(s_q, &d, pdMS_TO_TICKS(200)) == pdTRUE) {
            int n = wlogger_csv_format(buf + buf_len, sizeof buf - buf_len, &d);
            if (n > 0) buf_len += n;
            stats_record_event(s_stats, &d);
            recent_q_push(s_recent, &d);
        }

        bool full  = buf_len > sizeof(buf) - 256;
        bool stale = (xTaskGetTickCount() - last_flush) > pdMS_TO_TICKS(1000);
        if ((full || stale) && s_file.fp && buf_len > 0) {
            size_t w = fwrite(buf, 1, buf_len, s_file.fp);
            fflush(s_file.fp);
            s_file.bytes_written += (uint32_t)w;
            buf_len = 0;
            last_flush = xTaskGetTickCount();

            // also update stats.current_file* visibly
            xSemaphoreTake(s_stats->mtx, portMAX_DELAY);
            strncpy(s_stats->current_file, s_file.path, sizeof s_stats->current_file - 1);
            s_stats->current_file_bytes = s_file.bytes_written;
            s_stats->sd_free_bytes = wlogger_sd_free_bytes();
            xSemaphoreGive(s_stats->mtx);
        }

        bool need = s_rotate_req || wlogger_sd_should_rotate(&s_file,
            (uint32_t)(esp_timer_get_time() / 1000000));
        if (need) {
            if (s_file.fp) wlogger_sd_close(&s_file);
            wlogger_sd_open_new_file(&s_file,
                (uint32_t)(esp_timer_get_time() / 1000000), "balanced");
            s_rotate_req = false;
        }
    }
}

esp_err_t wlogger_writer_start_task(void) {
    BaseType_t r = xTaskCreate(writer_task, "writer", 4096, NULL, 4, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 4: Build**

Run: `idf.py build`

- [ ] **Step 5: Commit**

```bash
git add components/wlogger_writer/
git commit -m "feat(writer): batch-flush task, recent_q, rotation, stats refresh"
```

---

### Task 4.2: UI page 1 — Stats

**Files:**
- Create: `components/wlogger_ui/CMakeLists.txt`
- Create: `components/wlogger_ui/include/wlogger_ui.h`
- Create: `components/wlogger_ui/wlogger_ui.c`
- Create: `components/wlogger_ui/page_stats.c`
- Create: `components/wlogger_ui/page_internal.h`

- [ ] **Step 1: Write `components/wlogger_ui/include/wlogger_ui.h`**

```c
#pragma once
#include "wlogger_stats.h"
#include "wlogger_writer.h"
#include "esp_err.h"

esp_err_t wlogger_ui_init(stats_t *stats, recent_q_t *recent);
esp_err_t wlogger_ui_start_task(void);
```

- [ ] **Step 2: Write `components/wlogger_ui/page_internal.h`**

```c
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
```

- [ ] **Step 3: Write `components/wlogger_ui/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "wlogger_ui.c" "page_stats.c" "page_feed.c" "page_bars.c" "page_status.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES wlogger_stats wlogger_writer wlogger_lcd wlogger_button wlogger_led wlogger_bloom lvgl
)
```

- [ ] **Step 4: Write `components/wlogger_ui/page_stats.c`**

```c
#include "page_internal.h"
#include "wlogger_bloom.h"
#include <stdio.h>

typedef struct {
    lv_obj_t *uptime, *wifi_n, *ble_n, *probe_n, *rate, *chart;
    lv_chart_series_t *series;
    int chart_pts;
} ctx_t;
static ctx_t s;

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    char tmp[32];
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(tmp, sizeof tmp, "%02u:%02u:%02u", up/3600, (up/60)%60, up%60);
    lv_label_set_text(s.uptime, tmp);

    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_wifi));
    lv_label_set_text(s.wifi_n, tmp);
    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_ble));
    lv_label_set_text(s.ble_n, tmp);
    snprintf(tmp, sizeof tmp, "%u", (unsigned)bloom_count_estimate(&st->bloom_probe));
    lv_label_set_text(s.probe_n, tmp);

    uint16_t now_rate = stats_rate_last_minute(st);
    snprintf(tmp, sizeof tmp, "%u/m", now_rate);
    lv_label_set_text(s.rate, tmp);

    // shift chart left, append latest
    if (s.chart_pts < 30) s.chart_pts++;
    for (int i = 0; i < s.chart_pts - 1; ++i) {
        int idx = (st->rate_head + 60 - s.chart_pts + 1 + i) % 60;
        lv_chart_set_value_by_id(s.chart, s.series, i, st->rate_per_min[idx]);
    }
    lv_chart_set_value_by_id(s.chart, s.series, s.chart_pts - 1, now_rate);
    lv_chart_refresh(s.chart);
}

ui_page_t page_stats_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *l;
    s.uptime = lv_label_create(p); lv_label_set_text(s.uptime, "00:00:00");

    l = lv_label_create(p); lv_label_set_text(l, "Wi-Fi APs");
    s.wifi_n = lv_label_create(p); lv_obj_set_style_text_font(s.wifi_n, &lv_font_montserrat_28, 0);

    l = lv_label_create(p); lv_label_set_text(l, "BLE devs");
    s.ble_n = lv_label_create(p); lv_obj_set_style_text_font(s.ble_n, &lv_font_montserrat_28, 0);

    l = lv_label_create(p); lv_label_set_text(l, "Probe MACs");
    s.probe_n = lv_label_create(p); lv_obj_set_style_text_font(s.probe_n, &lv_font_montserrat_20, 0);

    l = lv_label_create(p); lv_label_set_text(l, "Rate (60 s)");
    s.rate = lv_label_create(p); lv_obj_set_style_text_font(s.rate, &lv_font_montserrat_20, 0);

    s.chart = lv_chart_create(p);
    lv_obj_set_size(s.chart, LV_PCT(100), 60);
    lv_chart_set_type(s.chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s.chart, 30);
    lv_chart_set_range(s.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    s.series = lv_chart_add_series(s.chart, lv_color_hex(0xFFE27A), LV_CHART_AXIS_PRIMARY_Y);
    s.chart_pts = 0;

    return (ui_page_t){ .root = p, .update = update };
}
```

- [ ] **Step 5: Write minimal stubs for `page_feed.c`, `page_bars.c`, `page_status.c`** so the project links.

```c
// page_feed.c
#include "page_internal.h"
ui_page_t page_feed_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent); lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_label_set_text(lv_label_create(p), "Live feed (TBD)");
    return (ui_page_t){ .root = p, .update = NULL };
}
// page_bars.c
#include "page_internal.h"
ui_page_t page_bars_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent); lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_label_set_text(lv_label_create(p), "Strength bars (TBD)");
    return (ui_page_t){ .root = p, .update = NULL };
}
// page_status.c
#include "page_internal.h"
ui_page_t page_status_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent); lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_label_set_text(lv_label_create(p), "File / SD (TBD)");
    return (ui_page_t){ .root = p, .update = NULL };
}
```

(These will be fully implemented in Tasks 4.3 / 4.4 / 4.5; stubs let the project link now.)

- [ ] **Step 6: Write `components/wlogger_ui/wlogger_ui.c`**

```c
#include "wlogger_ui.h"
#include "page_internal.h"
#include "wlogger_lcd.h"
#include "wlogger_button.h"
#include "wlogger_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UI_TICK_MS 100

static stats_t      *s_stats;
static recent_q_t   *s_recent;
static lv_obj_t     *s_tabview;
static ui_page_t     s_pages[4];
static int           s_active = 0;

esp_err_t wlogger_ui_init(stats_t *stats, recent_q_t *recent) {
    s_stats = stats; s_recent = recent;
    s_tabview = lv_tabview_create(lv_screen_active());
    lv_tabview_set_tab_bar_size(s_tabview, 0);

    lv_obj_t *t1 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t2 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t3 = lv_tabview_add_tab(s_tabview, "");
    lv_obj_t *t4 = lv_tabview_add_tab(s_tabview, "");
    s_pages[0] = page_stats_create(t1);
    s_pages[1] = page_feed_create(t2);
    s_pages[2] = page_bars_create(t3);
    s_pages[3] = page_status_create(t4);
    return ESP_OK;
}

static void ui_task(void *_) {
    uint32_t bars_last = 0, status_last = 0;
    for (;;) {
        wlogger_lcd_tick_ms(UI_TICK_MS);
        lv_timer_handler();

        btn_event_t be = wlogger_button_poll(UI_TICK_MS);
        if (be == BTN_SHORT) {
            s_active = (s_active + 1) % 4;
            lv_tabview_set_active(s_tabview, s_active, LV_ANIM_OFF);
        } else if (be == BTN_LONG) {
            wlogger_writer_request_rotate();
            wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_ROTATED);
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        ui_page_t *p = &s_pages[s_active];
        bool render = false;
        if (s_active == 0) render = true;                              // every tick
        if (s_active == 1) render = true;                              // checked inside (stub)
        if (s_active == 2 && now - bars_last >= 500) { bars_last = now; render = true; }
        if (s_active == 3 && now - status_last >= 1000) { status_last = now; render = true; }
        if (render && p->update) p->update(s_stats, s_recent);

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

esp_err_t wlogger_ui_start_task(void) {
    BaseType_t r = xTaskCreate(ui_task, "ui", 6144, NULL, 3, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 7: Build**

Run: `idf.py build`

- [ ] **Step 8: Commit**

```bash
git add components/wlogger_ui/
git commit -m "feat(ui): tabview + stats page (uniques, rate, sparkline)"
```

---

### Task 4.3: UI page 2 — Live feed

**Files:**
- Modify: `components/wlogger_ui/page_feed.c`

- [ ] **Step 1: Replace stub with full implementation**

```c
#include "page_internal.h"
#include "wlogger_writer.h"
#include <stdio.h>

#define MAX_ROWS 14
static lv_obj_t *s_root;
static lv_obj_t *s_rows[MAX_ROWS];
static uint32_t  s_last_count = 0;

static const char *type_letter(det_type_t t) {
    return t == DET_WIFI_AP ? "W" : t == DET_WIFI_PROBE ? "P" : "B";
}

static void update(stats_t *st, recent_q_t *r) {
    (void)st;
    detection_t snap[MAX_ROWS];
    int n = recent_q_snapshot(r, snap, MAX_ROWS);
    if ((uint32_t)n == s_last_count && n > 0 && n == MAX_ROWS) {
        // No new entries since last full snapshot — skip redraw.
        // (Use total_events from stats if you want a stricter check.)
    }
    s_last_count = (uint32_t)n;
    for (int i = 0; i < MAX_ROWS; ++i) {
        if (i < n) {
            const detection_t *d = &snap[n - 1 - i];        // newest first
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%s %02x:%02x %4d  %s",
                type_letter(d->type), d->mac[4], d->mac[5], d->rssi, d->name);
            lv_label_set_text(s_rows[i], tmp);
        } else {
            lv_label_set_text(s_rows[i], "");
        }
    }
}

ui_page_t page_feed_create(lv_obj_t *parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    for (int i = 0; i < MAX_ROWS; ++i) {
        s_rows[i] = lv_label_create(s_root);
        lv_label_set_text(s_rows[i], "");
        lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_12, 0);
    }
    return (ui_page_t){ .root = s_root, .update = update };
}
```

- [ ] **Step 2: Build, commit**

```bash
idf.py build
git add components/wlogger_ui/page_feed.c
git commit -m "feat(ui): live-feed page rendering last 14 detections"
```

---

### Task 4.4: UI page 3 — Strength bars

**Files:**
- Modify: `components/wlogger_ui/page_bars.c`

- [ ] **Step 1: Replace stub**

```c
#include "page_internal.h"
#include <stdio.h>

#define N WLOG_TOPN
static lv_obj_t *s_bars[N], *s_labels[N];

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    // sort copy by RSSI descending
    top_entry_t copy[N];
    xSemaphoreTake(st->mtx, portMAX_DELAY);
    memcpy(copy, st->strongest, sizeof copy);
    xSemaphoreGive(st->mtx);
    for (int i = 0; i < N - 1; ++i)
        for (int j = i + 1; j < N; ++j)
            if (copy[j].rssi > copy[i].rssi) {
                top_entry_t t = copy[i]; copy[i] = copy[j]; copy[j] = t;
            }
    for (int i = 0; i < N; ++i) {
        if (copy[i].last_seen_t == 0) {
            lv_bar_set_value(s_bars[i], 0, LV_ANIM_OFF);
            lv_label_set_text(s_labels[i], "");
            continue;
        }
        // map -100..-30 → 0..100
        int pct = (copy[i].rssi + 100) * 100 / 70;
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        lv_bar_set_value(s_bars[i], pct, LV_ANIM_OFF);
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%-12.12s %d", copy[i].name[0] ? copy[i].name : "(noname)", copy[i].rssi);
        lv_label_set_text(s_labels[i], tmp);
    }
}

ui_page_t page_bars_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, 1, 0);
    for (int i = 0; i < N; ++i) {
        s_labels[i] = lv_label_create(p);
        lv_obj_set_style_text_font(s_labels[i], &lv_font_montserrat_12, 0);
        s_bars[i] = lv_bar_create(p);
        lv_obj_set_size(s_bars[i], LV_PCT(95), 5);
        lv_bar_set_range(s_bars[i], 0, 100);
    }
    return (ui_page_t){ .root = p, .update = update };
}
```

- [ ] **Step 2: Build, commit**

```bash
idf.py build
git add components/wlogger_ui/page_bars.c
git commit -m "feat(ui): strength-bars page rendering top-8 by RSSI"
```

---

### Task 4.5: UI page 4 — File / SD status

**Files:**
- Modify: `components/wlogger_ui/page_status.c`

- [ ] **Step 1: Replace stub**

```c
#include "page_internal.h"
#include <stdio.h>

static lv_obj_t *s_file, *s_size, *s_free, *s_drops, *s_flags;

static void update(stats_t *st, recent_q_t *r) {
    (void)r;
    char tmp[64];
    xSemaphoreTake(st->mtx, portMAX_DELAY);
    snprintf(tmp, sizeof tmp, "%s", st->current_file[0] ? st->current_file : "(none)");
    lv_label_set_text(s_file, tmp);
    snprintf(tmp, sizeof tmp, "Size: %lu B", (unsigned long)st->current_file_bytes);
    lv_label_set_text(s_size, tmp);
    double mb_free = (double)st->sd_free_bytes / (1024.0 * 1024.0);
    snprintf(tmp, sizeof tmp, "Free: %.1f MB", mb_free);
    lv_label_set_text(s_free, tmp);
    snprintf(tmp, sizeof tmp, "Drops: %llu", (unsigned long long)st->dropped_events);
    lv_label_set_text(s_drops, tmp);
    snprintf(tmp, sizeof tmp, "WIFI:%s BLE:%s SD:%s",
        st->wifi_ok ? "Y" : "N", st->ble_ok ? "Y" : "N", st->sd_ok ? "Y" : "N");
    lv_label_set_text(s_flags, tmp);
    xSemaphoreGive(st->mtx);
}

ui_page_t page_status_create(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    s_file  = lv_label_create(p);
    s_size  = lv_label_create(p);
    s_free  = lv_label_create(p);
    s_drops = lv_label_create(p);
    s_flags = lv_label_create(p);
    return (ui_page_t){ .root = p, .update = update };
}
```

- [ ] **Step 2: Build, commit**

```bash
idf.py build
git add components/wlogger_ui/page_status.c
git commit -m "feat(ui): file/SD status page (path, size, free, drops, ok flags)"
```

---

## Phase 5 — Wire it all up

### Task 5.1: Subsystem orchestrator

**Files:**
- Create: `main/wlogger_init.c`
- Create: `main/wlogger_init.h`
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write `main/wlogger_init.h`**

```c
#pragma once
#include "esp_err.h"
esp_err_t wlogger_bringup(void);
```

- [ ] **Step 2: Write `main/wlogger_init.c`**

```c
#include "wlogger_init.h"
#include "wlogger_sd.h"
#include "wlogger_lcd.h"
#include "wlogger_led.h"
#include "wlogger_button.h"
#include "wlogger_stats.h"
#include "wlogger_writer.h"
#include "wlogger_scan.h"
#include "wlogger_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "init";
static stats_t      g_stats;
static recent_q_t   g_recent;
static QueueHandle_t g_detect_q;

esp_err_t wlogger_bringup(void) {
    stats_init(&g_stats);
    recent_q_init(&g_recent);
    g_detect_q = xQueueCreate(256, sizeof(detection_t));
    if (!g_detect_q) return ESP_ERR_NO_MEM;

    bool led_ok = (wlogger_led_init() == ESP_OK);
    (void)led_ok;
    if (wlogger_lcd_init() != ESP_OK) ESP_LOGW(TAG, "LCD init failed; running headless");
    wlogger_button_init();

    if (wlogger_sd_mount() != ESP_OK) {
        g_stats.sd_ok = false;
        wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_FAULT);
        // Continue boot to render the NO SD screen on the UI page; do not start scan/writer.
        wlogger_ui_init(&g_stats, &g_recent);
        wlogger_ui_start_task();
        return ESP_ERR_NOT_FOUND;
    }
    g_stats.sd_ok = true;

    wlogger_writer_init(g_detect_q, &g_stats, &g_recent);
    wlogger_writer_start_task();

    if (wlogger_scan_init(g_detect_q) != ESP_OK) {
        ESP_LOGE(TAG, "all radios failed");
        wlogger_led_set(LED_PHASE_WIFI, 0, LED_EVT_FAULT);
    } else {
        g_stats.wifi_ok = wlogger_scan_wifi_ok();
        g_stats.ble_ok  = wlogger_scan_ble_ok();
        wlogger_scan_start_task();
    }

    wlogger_ui_init(&g_stats, &g_recent);
    wlogger_ui_start_task();
    return ESP_OK;
}
```

- [ ] **Step 3: Replace `main/app_main.c`**

```c
#include "wlogger_init.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "wlogger boot");
    wlogger_bringup();
}
```

- [ ] **Step 4: Update `main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS "app_main.c" "wlogger_init.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event nvs_flash
             wlogger_types wlogger_stats wlogger_writer wlogger_scan
             wlogger_ui wlogger_lcd wlogger_led wlogger_button wlogger_sd
)
```

- [ ] **Step 5: Build**

Run: `idf.py build`
Expected: builds successfully.

- [ ] **Step 6: Commit**

```bash
git add main/
git commit -m "feat(init): subsystem orchestration with graceful-degradation paths"
```

---

### Task 5.2: WS2812 status loop

**Files:**
- Modify: `components/wlogger_ui/wlogger_ui.c` (LED update inside ui_task)

- [ ] **Step 1: Add LED update in `ui_task` (replace the inner loop body's tail with):**

After the `vTaskDelay` is the wrong place — extend instead, just before `vTaskDelay`:

```c
        // ~1 Hz LED refresh
        static uint32_t led_last = 0;
        if (now - led_last >= 1000) {
            led_last = now;
            led_phase_t ph = (((now / 1000) % 9) < 7) ? LED_PHASE_WIFI : LED_PHASE_BLE;
            uint16_t rate = stats_rate_last_minute(s_stats);
            led_event_t evt = LED_EVT_NONE;
            if (!s_stats->sd_ok) evt = LED_EVT_FAULT;
            else if (s_stats->dropped_events > 0 &&
                     (s_stats->dropped_events % 10) == 0)  // sparse
                evt = LED_EVT_DROP;
            wlogger_led_set(ph, rate, evt);
        }
```

(The phase is approximated from the ~9 s cycle; if the scan task exposes the actual phase later, replace this with that signal.)

- [ ] **Step 2: Build, commit**

```bash
idf.py build
git add components/wlogger_ui/wlogger_ui.c
git commit -m "feat(led): WS2812 1 Hz refresh from ui_task"
```

---

### Task 5.3: NO-SD splash screen

**Files:**
- Modify: `components/wlogger_ui/wlogger_ui.c`

- [ ] **Step 1: Add a "no-SD" override path** — if `stats.sd_ok == false` at init time, render a single full-screen label instead of the tabview. Insert near the top of `wlogger_ui_init`:

```c
    if (!stats->sd_ok) {
        lv_obj_t *bg = lv_obj_create(lv_screen_active());
        lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x600000), 0);
        lv_obj_t *l = lv_label_create(bg);
        lv_label_set_text(l, "NO SD CARD\n\nINSERT AND\nREBOOT");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
        return ESP_OK;
    }
```

- [ ] **Step 2: Build, commit**

```bash
idf.py build
git add components/wlogger_ui/wlogger_ui.c
git commit -m "feat(ui): NO SD splash screen overrides tabview when SD missing"
```

---

## Phase 6 — Hardware verification

### Task 6.1: Smoke test on hardware (no SD)

**Files:** *(none)*

- [ ] **Step 1: Pull SD card, flash board**

Run: `./build-and-flash.sh`
Expected (in monitor): `wlogger boot` → `mount: ESP_FAIL`. LCD shows red `NO SD CARD INSERT AND REBOOT`. WS2812 solid red.

- [ ] **Step 2: Document outcome in `README.md` testing section** (manual; one paragraph noting the verified behavior)

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: verified no-SD smoke behavior on hardware"
```

---

### Task 6.2: Wi-Fi + BLE detection verification

**Files:** *(none)*

- [ ] **Step 1: Insert formatted SD card, flash, place near a known Wi-Fi AP and an active BLE advertiser (phone in pairing mode is easiest)**

- [ ] **Step 2: Wait 30 s. Power off. Pull SD card; on a workstation:**

```bash
head -20 /path/to/sd/wlogger/log_0001_0001.csv
grep ',W,' /path/to/sd/wlogger/log_0001_0001.csv | head
grep ',B,' /path/to/sd/wlogger/log_0001_0001.csv | head
```

Expected: header line, ≥ 1 `W,` row matching the known AP's BSSID, ≥ 1 `B,` row from the test advertiser.

- [ ] **Step 3: Re-flash with sniff-mode device near board, watch live-feed page** for matching MACs.

- [ ] **Step 4: Press button — page changes; hold 2 s — yellow LED breath, then file index increments to `log_0001_0002.csv` next time you copy off.**

- [ ] **Step 5: If any check fails, capture `idf.py monitor` output, file an issue, fix, regression-test.**

---

### Task 6.3: 24-hour soak test

**Files:** *(none)*

- [ ] **Step 1: Leave the board powered in a normal room for 24 hours.**

- [ ] **Step 2: Capture `idf.py monitor` log (redirect to file).**

- [ ] **Step 3: Power off, pull SD, count files:**

```bash
ls /path/to/sd/wlogger/log_*.csv | wc -l   # expect ≥ 24
```

- [ ] **Step 4: `grep -c "ERROR\|FATAL" monitor.log` — expect 0.**

- [ ] **Step 5: Optionally compute crude rate stats:**

```bash
for f in /path/to/sd/wlogger/log_0001_*.csv; do
    echo "$f $(wc -l < "$f") rows"
done
```

- [ ] **Step 6: Commit any tuning you discover (e.g., adjust `WLOGGER_BLE_WINDOW_MS` if BLE is starved).**

---

## Self-Review

Spec coverage:

- §3.1 Wi-Fi capture → Tasks 3.1, 3.4 ✓
- §3.2 BLE capture → Tasks 3.2, 3.4 ✓
- §3.3 CSV persistence → Tasks 1.3, 4.1 ✓
- §3.4 Rotation → Task 2.1 (`wlogger_sd_should_rotate`), 4.1 (force-rotate), 5.2 ✓
- §3.5 Four-page UI → Tasks 4.2–4.5 ✓
- §3.6 WS2812 status → Tasks 2.3, 5.2 ✓
- §3.7 Graceful degradation → Task 5.1 (orchestrator), 5.3 (no-SD splash) ✓
- §6 data model → Tasks 1.2 (types), 1.3 (CSV), 2.1 (file naming/header) ✓
- §7 scan engine state machine → Tasks 3.1, 3.2, 3.4 ✓
- §8 LCD + LVGL + button + LED → Tasks 2.2, 2.3, 2.4, 4.2 ✓
- §9 error handling → Task 5.1 + manual verification in 6.x ✓
- §10 testing → Tasks 1.3–1.7 (host) + 6.1–6.3 (hardware) ✓

Placeholder scan: stub pages in Task 4.2 are explicitly transitional (deleted in 4.3/4.4/4.5); no other "TBD" / "TODO" entries.

Type/name consistency: `detection_t`, `stats_t`, `recent_q_t`, `wlogger_file_t`, `bloom_t`, `top_entry_t` are referenced consistently across tasks. Function names (`wlogger_sd_*`, `wlogger_csv_format`, `bloom_*`, `stats_*`, `wlogger_button_poll`, `wlogger_led_set`, `wlogger_ui_init`) match between definition tasks and call sites.
