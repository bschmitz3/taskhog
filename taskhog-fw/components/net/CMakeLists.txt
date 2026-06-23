idf_component_register(
    SRCS
        "wifi_sta.c"
        "sync_engine.c"
        "http_uploader.c"
        "time_sync.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls
        nvs_flash esp_timer freertos
        config storage power ui
)
