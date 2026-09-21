idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        watch_ui
        watch_svc
        watch_hal
        esp32_s3_touch_amoled_2_06
        esp_lvgl_port
        nvs_flash
)
