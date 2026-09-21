# Upstream declares esp_lcd_touch in idf_component.yml, which only the component
# manager acts on. No manifest is vendored here, so the dependency is stated
# directly - correct whether or not the manager is enabled.
idf_component_register(SRCS "esp_lcd_touch_ft5x06.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_lcd" "esp_lcd_touch")
