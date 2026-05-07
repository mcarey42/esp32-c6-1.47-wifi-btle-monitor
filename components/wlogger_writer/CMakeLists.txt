idf_component_register(
    SRCS "wlogger_writer.c"
    INCLUDE_DIRS "include"
    REQUIRES wlogger_types wlogger_csv wlogger_stats wlogger_sd wlogger_lcd esp_timer
)
