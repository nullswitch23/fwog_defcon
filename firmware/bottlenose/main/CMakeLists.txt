idf_component_register(
    SRCS "main.c" "hid.c" "wifiproof.c" "ferry.c"
         "battlebridge.c" "battlebridge_game.c" "pass_nvs.c"
    INCLUDE_DIRS "."
    EMBED_FILES "cover.jpg"
    REQUIRES driver esp_wifi esp_event esp_netif esp_http_server esp_timer nvs_flash bt esp_coex
)
