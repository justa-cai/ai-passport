idf_component_register(
    SRCS "main.c"
         "ui_music.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash
)
