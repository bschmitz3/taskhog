idf_component_register(
    SRCS "power_mgr.c" "battery.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc board driver log esp_hw_support esp_timer storage rtc
)
