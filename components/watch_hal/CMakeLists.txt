idf_component_register(
    SRCS
        "axp2101.c"
        "qmi8658.c"
        "pcf85063.c"
        "cam_mega.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
