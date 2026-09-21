/*
 * 05 - Motion sensor, and 06 - Motion graph.
 *
 * Both read the same service; the first shows the current vector, the
 * second plots the rolling history. They share a file because they share
 * the axis colours and the "no IMU" path.
 */
#include "watch_ui/ui.h"
#include "watch_ui/ui_screens.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_sensors.h"

#include <stdio.h>
#include "bsp/esp-bsp.h"

#define AXIS_X_COLOR lv_color_hex(0xEF4444)
#define AXIS_Y_COLOR lv_color_hex(0x22C55E)
#define AXIS_Z_COLOR lv_color_hex(0x3B82F6)

/* ===================================================== 05 motion sensor == */

static lv_obj_t *s_acc[3];
static lv_obj_t *s_gyr[3];
static lv_obj_t *s_temp;
static lv_obj_t *s_steps;

static void sensor_tick(lv_timer_t *t)
{
    (void)t;
    const svc_sensors_sample_t *s = svc_sensors_latest();

    lv_label_set_text_fmt(s_acc[0], "%+.2f", (double)s->ax);
    lv_label_set_text_fmt(s_acc[1], "%+.2f", (double)s->ay);
    lv_label_set_text_fmt(s_acc[2], "%+.2f", (double)s->az);

    lv_label_set_text_fmt(s_gyr[0], "%+.2f", (double)s->gx);
    lv_label_set_text_fmt(s_gyr[1], "%+.2f", (double)s->gy);
    lv_label_set_text_fmt(s_gyr[2], "%+.2f", (double)s->gz);

    lv_label_set_text_fmt(s_temp, "%.1f\xC2\xB0""C", (double)s->temp_c);
    lv_label_set_text_fmt(s_steps, "%lu", (unsigned long)s->steps);
}

/* One "X  +0.12" line with a colour-coded axis letter. */
static void axis_row(lv_obj_t *parent, const char *letter, lv_color_t color,
                     lv_obj_t **out_value, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, letter);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, "+0.00");
    lv_obj_set_style_text_color(v, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(v, ui_font(UI_FONT_BODY), LV_PART_MAIN);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);

    if (out_value != NULL) {
        *out_value = v;
    }
}

static void go_graph(lv_event_t *e)
{
    (void)e;
    ui_nav_go(UI_SCR_MOTION_GRAPH);
}

lv_obj_t *scr_sensor_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_MOTION_SENSOR, &body);

    if (!svc_sensors_available()) {
        ui_empty_state(body, LV_SYMBOL_WARNING, i18n(STR_SENSOR_MISSING), NULL);
        return scr;
    }

    /* ---- accelerometer ---- */
    lv_obj_t *acc = ui_card(body, 128);
    lv_obj_t *acc_title = lv_label_create(acc);
    lv_label_set_text_fmt(acc_title, "%s (g)", i18n(STR_ACCELEROMETER));
    lv_obj_set_style_text_color(acc_title, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(acc_title, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(acc_title, LV_ALIGN_TOP_LEFT, 0, 0);

    axis_row(acc, "X", AXIS_X_COLOR, &s_acc[0], 26);
    axis_row(acc, "Y", AXIS_Y_COLOR, &s_acc[1], 54);
    axis_row(acc, "Z", AXIS_Z_COLOR, &s_acc[2], 82);

    /* ---- gyroscope ---- */
    lv_obj_t *gyr = ui_card(body, 128);
    lv_obj_t *gyr_title = lv_label_create(gyr);
    lv_label_set_text_fmt(gyr_title, "%s (\xC2\xB0/s)", i18n(STR_GYROSCOPE));
    lv_obj_set_style_text_color(gyr_title, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(gyr_title, ui_font_text(UI_FONT_SMALL), LV_PART_MAIN);
    lv_obj_align(gyr_title, LV_ALIGN_TOP_LEFT, 0, 0);

    axis_row(gyr, "X", AXIS_X_COLOR, &s_gyr[0], 26);
    axis_row(gyr, "Y", AXIS_Y_COLOR, &s_gyr[1], 54);
    axis_row(gyr, "Z", AXIS_Z_COLOR, &s_gyr[2], 82);

    /* ---- temperature and steps ---- */
    lv_obj_t *misc = ui_card(body, 0);
    ui_kv_row(misc, i18n(STR_TEMPERATURE), "--", &s_temp);
    ui_kv_row(misc, i18n(STR_STEPS), "0", &s_steps);
    lv_obj_set_flex_flow(misc, LV_FLEX_FLOW_COLUMN);

    ui_menu_row(body, LV_SYMBOL_BARS, UI_COLOR_PURPLE, i18n(STR_MOTION_GRAPH),
                go_graph, NULL);

    sensor_tick(NULL);
    ui_screen_add_timer(scr, sensor_tick, 200);
    return scr;
}

/* ====================================================== 06 motion graph == */

static lv_obj_t          *s_chart;
static lv_chart_series_t *s_ser[3];
static bool               s_show_gyro;
static lv_obj_t          *s_btn_accel;
static lv_obj_t          *s_btn_gyro;

static void graph_refill(void)
{
    static float bx[SVC_SENSORS_HISTORY_LEN];
    static float by[SVC_SENSORS_HISTORY_LEN];
    static float bz[SVC_SENSORS_HISTORY_LEN];

    size_t n;
    if (s_show_gyro) {
        n = svc_sensors_get_history(NULL, NULL, NULL, bx, by, bz);
        /* Gyro is plotted in degrees per second, clipped to the range the
         * chart shows; the numbers themselves live on screen 05. */
        lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, -250, 250);
    } else {
        n = svc_sensors_get_history(bx, by, bz, NULL, NULL, NULL);
        /* Accelerometer in hundredths of g, so +-2 g fills the plot. */
        lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, -200, 200);
    }

    for (size_t i = 0; i < SVC_SENSORS_HISTORY_LEN; i++) {
        if (i < n) {
            const float scale = s_show_gyro ? 1.0f : 100.0f;
            lv_chart_set_series_value_by_id(s_chart, s_ser[0], (uint32_t)i, (int32_t)(bx[i] * scale));
            lv_chart_set_series_value_by_id(s_chart, s_ser[1], (uint32_t)i, (int32_t)(by[i] * scale));
            lv_chart_set_series_value_by_id(s_chart, s_ser[2], (uint32_t)i, (int32_t)(bz[i] * scale));
        } else {
            /* Nothing recorded yet for this slot - leave a gap rather than
             * drawing a line down to zero. */
            lv_chart_set_series_value_by_id(s_chart, s_ser[0], (uint32_t)i, LV_CHART_POINT_NONE);
            lv_chart_set_series_value_by_id(s_chart, s_ser[1], (uint32_t)i, LV_CHART_POINT_NONE);
            lv_chart_set_series_value_by_id(s_chart, s_ser[2], (uint32_t)i, LV_CHART_POINT_NONE);
        }
    }
    lv_chart_refresh(s_chart);
}

static void graph_tick(lv_timer_t *t)
{
    (void)t;
    graph_refill();
}

static void set_mode(bool gyro)
{
    s_show_gyro = gyro;
    lv_obj_set_style_bg_color(s_btn_accel, gyro ? lv_color_hex(0x1B2636) : UI_COLOR_ACCENT,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_gyro, gyro ? UI_COLOR_ACCENT : lv_color_hex(0x1B2636),
                              LV_PART_MAIN);
    graph_refill();
}

static void on_accel(lv_event_t *e) { (void)e; set_mode(false); }
static void on_gyro(lv_event_t *e)  { (void)e; set_mode(true); }

lv_obj_t *scr_motion_graph_create(void)
{
    lv_obj_t *body;
    lv_obj_t *scr = ui_screen_base(STR_MOTION_GRAPH, &body);

    if (!svc_sensors_available()) {
        ui_empty_state(body, LV_SYMBOL_WARNING, i18n(STR_SENSOR_MISSING), NULL);
        return scr;
    }

    /* ---- mode toggle ---- */
    lv_obj_t *tabs = lv_obj_create(body);
    ui_style_plain(tabs);
    lv_obj_set_size(tabs, LV_PCT(100), 38);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_btn_accel = ui_button(tabs, i18n(STR_ACCEL), UI_COLOR_ACCENT, on_accel, NULL);
    lv_obj_set_size(s_btn_accel, (UI_CONTENT_W - 8) / 2, 34);
    s_btn_gyro = ui_button(tabs, i18n(STR_GYRO), lv_color_hex(0x1B2636), on_gyro, NULL);
    lv_obj_set_size(s_btn_gyro, (UI_CONTENT_W - 8) / 2, 34);

    /* ---- chart ---- */
    lv_obj_t *card = ui_card(body, 300);
    s_chart = lv_chart_create(card);
    lv_obj_set_size(s_chart, LV_PCT(100), 250);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 26);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, SVC_SENSORS_HISTORY_LEN);
    lv_chart_set_div_line_count(s_chart, 5, 6);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);

    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x1E2A3A), LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);   /* no point markers */
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);

    s_ser[0] = lv_chart_add_series(s_chart, AXIS_X_COLOR, LV_CHART_AXIS_PRIMARY_Y);
    s_ser[1] = lv_chart_add_series(s_chart, AXIS_Y_COLOR, LV_CHART_AXIS_PRIMARY_Y);
    s_ser[2] = lv_chart_add_series(s_chart, AXIS_Z_COLOR, LV_CHART_AXIS_PRIMARY_Y);

    /* ---- legend ---- */
    lv_obj_t *legend = lv_obj_create(card);
    ui_style_plain(legend);
    lv_obj_set_size(legend, LV_PCT(100), 20);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, 0, 0);

    static const char *const k_axis[3] = { "X", "Y", "Z" };
    const lv_color_t k_color[3] = { AXIS_X_COLOR, AXIS_Y_COLOR, AXIS_Z_COLOR };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *dot = lv_label_create(legend);
        lv_label_set_text_fmt(dot, "%s %s", LV_SYMBOL_MINUS, k_axis[i]);
        lv_obj_set_style_text_color(dot, k_color[i], LV_PART_MAIN);
        lv_obj_set_style_text_font(dot, ui_font(UI_FONT_TINY), LV_PART_MAIN);
        lv_obj_align(dot, LV_ALIGN_RIGHT_MID, (lv_coord_t)(-46 * (2 - i)), 0);
    }

    s_show_gyro = false;
    set_mode(false);
    ui_screen_add_timer(scr, graph_tick, 250);
    return scr;
}
