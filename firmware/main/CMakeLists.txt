idf_component_register(
    SRCS "main.cpp" "board_stub.cpp"
    INCLUDE_DIRS "." "../core"
    REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash json
)
