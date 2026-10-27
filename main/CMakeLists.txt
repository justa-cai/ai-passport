idf_component_register(
    SRCS "main.c"
         "ui_music.c"
         "music_fonts.c"
         "music_font_16.c"
         "music_queue.c"
         "music_ui_nav.c"
         "music_net_sm.c"
         "music_wifi.c"
         "music_httpd.c"
         "music_http.c"
         "music_json.c"
         "music_kuwo.c"
         "music_player_sm.c"
         "music_player.c"
         "music_decoder.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash
             esp_wifi esp_netif esp_http_server esp_http_client
             espressif__esp_audio_codec
)
