idf_component_register(
    SRCS "wlogger_ui.c" "page_stats.c" "page_feed.c" "page_bars.c" "page_status.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES wlogger_stats wlogger_writer wlogger_lcd wlogger_button wlogger_led wlogger_bloom wlogger_thermal esp_timer lvgl
)
