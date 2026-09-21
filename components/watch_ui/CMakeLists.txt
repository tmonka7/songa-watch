idf_component_register(
    SRCS
        "ui.c"
        "ui_theme.c"
        "ui_nav.c"
        "ui_widgets.c"
        "screens/scr_boot.c"
        "screens/scr_watchface.c"
        "screens/scr_home.c"
        "screens/scr_apps.c"
        "screens/scr_motion.c"
        "screens/scr_battery.c"
        "screens/scr_connectivity.c"
        "screens/scr_audio.c"
        "screens/scr_storage.c"
        "screens/scr_camera.c"
        "screens/scr_settings.c"
        "screens/scr_system.c"
        "screens/scr_obd2.c"
    INCLUDE_DIRS "include"
    REQUIRES
        watch_svc
        watch_hal
        lvgl
        esp32_s3_touch_amoled_2_06
        esp_lvgl_port
        esp_event
        esp_timer
        app_update
        # scr_system.c reads the running app descriptor and the flash size
        # directly. Both reach here transitively through app_update today; naming
        # them keeps that from being luck.
        esp_app_format
        spi_flash
)
