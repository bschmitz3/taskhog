idf_component_register(
    SRCS "app_main.c" "state_machine.c" "rec_button.c" "rec_session.c" "rec_worker.c" "m0_bringup_gate.c" "m0_sdcard_gate.c"
         "m0_rtc_gate.c" "m0_battery_gate.c" "m0_buttons_gate.c" "m0_epaper_gate.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_common log board esp_timer
             config power audio storage net provisioning rtc sensors ui ota
)
