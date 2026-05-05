idf_component_register(
    SRCS "wlogger_sd.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs sdmmc esp_driver_sdspi esp_driver_spi vfs wlogger_types
)
