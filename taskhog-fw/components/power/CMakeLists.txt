idf_component_register(
    SRCS "power_mgr.c" "battery.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc board driver log
)
