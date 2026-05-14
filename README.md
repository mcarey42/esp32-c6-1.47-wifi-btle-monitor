# wlogger — ESP32-C6 Wi-Fi + BLE metadata logger

A long-term wireless-presence logger for the **Waveshare ESP32-C6-LCD-1.47** board.
Runs continuously, captures every Wi-Fi management frame (beacons + probe
requests) and BLE advertisement it can hear, and writes each observation as
a CSV row to a microSD card. The 172 × 320 LCD shows live counters, a recent-
detections feed, a strongest-signals bar chart, and file/SD status across four
button-cycled pages. The on-board WS2812 LED encodes the current scan phase
(hue) and recent detection rate (brightness).

## Documentation

- **Design spec** —
  [`docs/superpowers/specs/2026-05-04-wireless-metadata-logger-design.md`](docs/superpowers/specs/2026-05-04-wireless-metadata-logger-design.md)
  — full architecture, data model, CSV format, error-handling policy.
- **Implementation plan** —
  [`docs/superpowers/plans/2026-05-04-wireless-metadata-logger.md`](docs/superpowers/plans/2026-05-04-wireless-metadata-logger.md)
  — task-by-task build sequence with exact files, code, and verify commands.

The spec is the source of truth for *what* the system does; the plan documents
*how* it was built. Both are checked-in artefacts of the original
design-then-implement session.

## Features

- **Wi-Fi promiscuous capture** across all 13 channels, 500 ms dwell, sees
  beacons and probe-requests (i.e. devices searching for known SSIDs, not
  just visible APs).
- **BLE 5.0 active scan** (NimBLE host stack) for advertising packets,
  manufacturer-data parsed for company IDs.
- **CSV logging** to microSD with hourly + 5 MB rotation, session-numbered
  filenames for unambiguous timelines across reboots.
- **LVGL 9 UI** on the ST7789 panel — four pages, IO9 button cycles them.
- **Thermal throttling** — the chip's internal temp sensor banded into
  COOL / WARM / HOT / CRITICAL, automatically extends rest periods between
  scan phases as the board heats up.
- **Graceful degradation** — LCD failure leaves logging running headlessly;
  Wi-Fi or BLE failure leaves the other radio active; SD absence shows a
  splash screen instead of crashing.

## Build & flash

```sh
./build-and-flash.sh           # build + flash + monitor
./build-and-flash.sh --build-only
```

The script auto-sources ESP-IDF v6.0 from `~/.espressif/`. Target chip is
`esp32c6`. The board's USB-Serial-JTAG appears as `/dev/ttyACM0` on Linux.

## Host-side tests

Pure-data modules (CSV formatter, 802.11/BLE parsers, bloom filter, stats
helpers) are testable on the host with plain CMake + Unity:

```sh
cd test_host && ./run.sh
```

## Pin map

| GPIO | Function    |
|------|-------------|
| 4    | SD CS       |
| 5    | SD MISO     |
| 6    | SD MOSI     |
| 7    | SD SCLK     |
| 8    | WS2812 data |
| 9    | Button (active high; also strapping pin — runtime use only) |
| 14   | LCD CS      |
| 15   | LCD DC      |
| 21   | LCD RST     |
| 22   | LCD BL (LEDC PWM) |

SD card and LCD share **SPI2**. The LCD owns the bus initialisation with a
16 KB max-transfer config; the SD attaches as a second device on the same
bus. Bus arbitration is enforced with an application-level mutex —
see `components/wlogger_lcd/wlogger_lcd.c` for the locking pattern.

## Tech stack

- ESP-IDF v6.0 (RISC-V, FreeRTOS)
- LVGL 9 over `esp_lcd` (ST7789 panel driver)
- NimBLE host stack, observer role
- FATFS over SPI (`esp_vfs_fat_sdspi_mount`)
- `led_strip` (RMT-backed WS2812)
- Internal temperature sensor (`esp_driver_tsens`)

## License

MIT — see [`LICENSE`](LICENSE).
