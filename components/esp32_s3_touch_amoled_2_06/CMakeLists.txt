if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.5")
    set(REQ esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdmmc)
    set(PRIV_REQ esp_driver_ledc)
else()
    set(REQ driver)
    set(PRIV_REQ "")
endif()

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND PRIV_REQ usb)
endif()

# Upstream declared these in idf_component.yml, so the component manager turned
# them into REQUIRES automatically. This project has no manifests and no
# component manager, so they are stated here. Public vs private follows the
# headers: include/bsp/*.h pulls in esp_codec_dev, lvgl, esp_lvgl_port,
# esp_lcd_touch and esp_lcd_types; the two panel/touch drivers are used only
# inside the .c file.
idf_component_register(
    SRCS "esp32_s3_touch_amoled_2_06.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES ${REQ} esp_lcd esp_lcd_panel_io_additions esp_lcd_touch
             esp_codec_dev lvgl esp_lvgl_port
    PRIV_REQUIRES ${PRIV_REQ} esp_timer spiffs esp_psram fatfs
                  esp_lcd_sh8601 esp_lcd_touch_ft5x06
)
