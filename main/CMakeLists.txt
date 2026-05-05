idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event nvs_flash wlogger_led wlogger_button wlogger_lcd
)
