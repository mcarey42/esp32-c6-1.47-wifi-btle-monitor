idf_component_register(
    SRCS "wlogger_scan_wifi.c" "wlogger_scan_ble.c" "wlogger_scan_task.c"
         "wlogger_scan_fake.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES wlogger_types wlogger_parsers esp_wifi esp_event nvs_flash bt esp_timer
)
