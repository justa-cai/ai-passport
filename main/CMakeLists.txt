idf_component_register(
    SRCS
        "main.c"
        "app_ui.c"
        "app_fonts.c"
        "app_home.c"
        "app_cards.c"
        "app_quiz.c"
        "audio_player.c"
        "app_progress.c"
        "pinyin_data.c"
        "pinyin_blend.c"
        "quiz_engine.c"
        "progress_codec.c"
        "adpcm_decoder.c"
        "pinyin_audio_index.c"
        "screen_dump.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash esp_timer)

# 生成的字体子集与发音 blob（tools/gen_app_fonts.py 与 tools/gen_pinyin_audio.py 产物）
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pinyin_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pinyin_font_22.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pinyin_font_44.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pinyin_font_56.c")

target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../assets/audio/pinyin_audio_ima.bin" BINARY)
