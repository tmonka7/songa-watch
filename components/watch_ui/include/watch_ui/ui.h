/*
 * UI entry point.
 *
 * Call ui_start() once the display and the services are up. It installs the
 * theme, wires the global handlers (language changes, display sleep, the
 * low-battery banner) and shows the boot screen.
 */
#pragma once

#include "esp_err.h"
#include "watch_ui/ui_nav.h"
#include "watch_ui/ui_theme.h"
#include "watch_ui/ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the UI and show the splash.
 *
 * Takes the LVGL lock internally; do not hold it when calling.
 */
esp_err_t ui_start(void);

/**
 * @brief Show a transient banner across the top of the current screen.
 *
 * Used for low battery, a lost adapter, a finished capture. Disappears by
 * itself after a few seconds.
 */
void ui_toast(const char *text, lv_color_t color);

#ifdef __cplusplus
}
#endif
