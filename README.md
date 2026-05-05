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
