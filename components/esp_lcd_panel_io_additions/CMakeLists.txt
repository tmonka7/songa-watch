idf_component_register(
    SRCS
        "esp_lcd_panel_io_3wire_spi.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        "driver"
    REQUIRES
        "esp_lcd"
        "esp_io_expander")

# Version pinned here rather than read from idf_component.yml: that manifest is
# not vendored (the component manager is disabled project-wide). Upstream is
# 1.0.1~1; the "~1" is a registry revision that never reached the C code anyway.
target_compile_options(${COMPONENT_LIB} PUBLIC
    -DESP_LCD_PANEL_IO_ADDITIONS_VER_MAJOR=1 -DESP_LCD_PANEL_IO_ADDITIONS_VER_MINOR=0
    -DESP_LCD_PANEL_IO_ADDITIONS_VER_PATCH=1)
