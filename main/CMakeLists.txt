idf_component_register(
    SRCS "app_main.c" "wlogger_init.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event nvs_flash
             wlogger_types wlogger_stats wlogger_writer wlogger_scan
             wlogger_ui wlogger_lcd wlogger_led wlogger_button wlogger_sd
)
