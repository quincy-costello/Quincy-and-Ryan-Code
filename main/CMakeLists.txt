idf_component_register(
    SRCS
        "main.cpp"
        "SoundManager.cpp"
        "game_logic.cpp"        # <-- use .cpp here
        "ir/ir_tx.cpp"
        "ir/ir_rx_gpt.cpp"
        "ir/ir_test_main.cpp"

    INCLUDE_DIRS
        "."
        "ir"
    PRIV_REQUIRES
        M5GFX
        espressif__led_strip
        esp_timer
        esp_driver_dac
        freertos
        driver
        esp_wifi
    )












