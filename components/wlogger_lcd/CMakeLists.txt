idf_component_register(
    SRCS "wlogger_lcd.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver lvgl esp_driver_spi esp_driver_gpio esp_driver_ledc
)
