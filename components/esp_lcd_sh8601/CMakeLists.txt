if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.5")
    set(PRIV_REQ esp_driver_gpio)
else()
    set(PRIV_REQ driver)
endif()

idf_component_register(SRCS "esp_lcd_sh8601.c" INCLUDE_DIRS "include" PRIV_REQUIRES ${PRIV_REQ} REQUIRES "esp_lcd")

# Upstream reads this from idf_component.yml via cu_pkg_define_version(). That
# manifest is not vendored - it only exists to be acted on by the component
# manager, which this project disables - so the version is pinned here instead.
# It feeds a log line and nothing else.
target_compile_options(${COMPONENT_LIB} PUBLIC
    -DESP_LCD_SH8601_VER_MAJOR=2 -DESP_LCD_SH8601_VER_MINOR=0 -DESP_LCD_SH8601_VER_PATCH=0)
