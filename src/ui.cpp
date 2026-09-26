#include "ui.h"
#include "settings.h"
#include "inverter.h"
#include "display.h"
#include "network.h"
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>

// --- Виджеты (статические, используются в update и callbacks) ---
static lv_obj_t* label_status;
static lv_obj_t* tabview;
static lv_obj_t* arc_soc;
static lv_obj_t* label_soc_val;
static lv_obj_t* label_batt_status;
static lv_obj_t* label_volts;
static lv_obj_t* label_amps;
static lv_obj_t* label_watts;
static lv_obj_t* label_temp_val;
static lv_obj_t* label_batt_pv;
static lv_obj_t* label_grid_val;
static lv_obj_t* label_grid_volts;
static lv_obj_t* label_load_val;
static lv_obj_t* label_arrow_grid;
static lv_obj_t* label_arrow_batt;
static lv_obj_t* label_batt_val_t2;
static lv_obj_t* label_source_val;
static lv_obj_t* label_pv_total;
static lv_obj_t* label_pv_day;
static lv_obj_t* label_pv1_volts;
static lv_obj_t* label_pv1_amps;
static lv_obj_t* label_pv1_watts;
static lv_obj_t* label_pv2_volts;
static lv_obj_t* label_pv2_amps;
static lv_obj_t* label_pv2_watts;
static lv_obj_t* arc_loader;

// --- Callbacks ---
static void slider_event_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int val = (int)lv_slider_get_value(slider);
    settingsSetBrightness(val);
    backlightSet(settingsGetBrightness());
}

static void dropdown_timeout_event_cb(lv_event_t* e) {
    lv_obj_t* dropdown = lv_event_get_target(e);
    int selected = lv_dropdown_get_selected(dropdown);
    switch (selected) {
        case 0: SCREEN_TIMEOUT_MIN = 0; break;
        case 1: SCREEN_TIMEOUT_MIN = 1; break;
        case 2: SCREEN_TIMEOUT_MIN = 5; break;
        case 3: SCREEN_TIMEOUT_MIN = 10; break;
        case 4: SCREEN_TIMEOUT_MIN = 30; break;
    }
    settingsSaveScreenTimeout(SCREEN_TIMEOUT_MIN);
    settingsTouchReset();
}

static void dropdown_timezone_cb(lv_event_t* e) {
    lv_obj_t* dropdown = lv_event_get_target(e);
    int selected = lv_dropdown_get_selected(dropdown);
    TIMEZONE_HOUR = selected - 12;
    settingsSaveTimezone(TIMEZONE_HOUR);
    networkApplyTimeConfig();
    settingsTouchReset();
}

static void btn_reboot_event_cb(lv_event_t* e) {
    (void)e;
    ESP.restart();
}

static void btn_reset_wifi_event_cb(lv_event_t* e) {
    (void)e;
    networkResetSettings();
    ESP.restart();
}

/** Компактная карточка без прокрутки — иначе на 320×240 контент не влезает. */
static void styleCompactCard(lv_obj_t* card) {
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 4, 0);
}

void uiBuild() {
    static lv_style_t style_big_num;
    lv_style_init(&style_big_num);
    lv_style_set_text_font(&style_big_num, &lv_font_montserrat_18);

    static lv_style_t style_label_gray;
    lv_style_init(&style_label_gray);
    lv_style_set_text_color(&style_label_gray, lv_palette_main(LV_PALETTE_GREY));

    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 40);
    lv_obj_t* tab_batt = lv_tabview_add_tab(tabview, "Battery");
    lv_obj_t* tab_grid = lv_tabview_add_tab(tabview, "Grid/Home");
    lv_obj_t* tab_solar = lv_tabview_add_tab(tabview, "Solar");
    lv_obj_clear_flag(tab_solar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* tab_settings = lv_tabview_add_tab(tabview, "Settings");

    // Settings: brightness
    lv_obj_t* label_bri = lv_label_create(tab_settings);
    lv_label_set_text(label_bri, "Brightness");
    lv_obj_align(label_bri, LV_ALIGN_TOP_LEFT, 5, 20);
    lv_obj_t* slider = lv_slider_create(tab_settings);
    lv_obj_set_width(slider, 160);
    lv_slider_set_value(slider, LCD_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_RIGHT, -10, 20);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Settings: timeout
    lv_obj_t* label_to = lv_label_create(tab_settings);
    lv_label_set_text(label_to, "Timeout");
    lv_obj_align(label_to, LV_ALIGN_TOP_LEFT, 5, 65);
    lv_obj_t* dd_timeout = lv_dropdown_create(tab_settings);
    lv_dropdown_set_options(dd_timeout, "Always On\n1 min\n5 min\n10 min\n30 min");
    lv_obj_set_width(dd_timeout, 160);
    lv_obj_align(dd_timeout, LV_ALIGN_TOP_RIGHT, -10, 55);
    lv_obj_add_event_cb(dd_timeout, dropdown_timeout_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    int sel_idx = 3;
    if (SCREEN_TIMEOUT_MIN == 0) sel_idx = 0;
    else if (SCREEN_TIMEOUT_MIN == 1) sel_idx = 1;
    else if (SCREEN_TIMEOUT_MIN == 5) sel_idx = 2;
    else if (SCREEN_TIMEOUT_MIN == 10) sel_idx = 3;
    else if (SCREEN_TIMEOUT_MIN == 30) sel_idx = 4;
    lv_dropdown_set_selected(dd_timeout, sel_idx);

    // Settings: timezone
    lv_obj_t* label_tz = lv_label_create(tab_settings);
    lv_label_set_text(label_tz, "Timezone");
    lv_obj_align(label_tz, LV_ALIGN_TOP_LEFT, 5, 105);
    lv_obj_t* dd_tz = lv_dropdown_create(tab_settings);
    lv_dropdown_set_options(dd_tz,
        "UTC-12\nUTC-11\nUTC-10\nUTC-9\nUTC-8\nUTC-7\nUTC-6\nUTC-5\nUTC-4\nUTC-3\nUTC-2\nUTC-1\n"
        "UTC\n"
        "UTC+1\nUTC+2\nUTC+3\nUTC+4\nUTC+5\nUTC+6\nUTC+7\nUTC+8\nUTC+9\nUTC+10\nUTC+11\nUTC+12\nUTC+13\nUTC+14");
    lv_obj_set_width(dd_tz, 160);
    lv_obj_align(dd_tz, LV_ALIGN_TOP_RIGHT, -10, 95);
    int tz_index = TIMEZONE_HOUR + 12;
    if (tz_index < 0) tz_index = 0;
    if (tz_index > 26) tz_index = 26;
    lv_dropdown_set_selected(dd_tz, tz_index);
    lv_obj_add_event_cb(dd_tz, dropdown_timezone_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Reboot / Reset WiFi
    lv_obj_t* btn_reboot = lv_btn_create(tab_settings);
    lv_obj_set_size(btn_reboot, 100, 35);
    lv_obj_align(btn_reboot, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_add_event_cb(btn_reboot, btn_reboot_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* label_btn_r = lv_label_create(btn_reboot);
    lv_label_set_text(label_btn_r, "Reboot");
    lv_obj_center(label_btn_r);

    lv_obj_t* btn_reset = lv_btn_create(tab_settings);
    lv_obj_set_size(btn_reset, 100, 35);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_set_style_bg_color(btn_reset, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_reset, btn_reset_wifi_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* label_btn_w = lv_label_create(btn_reset);
    lv_label_set_text(label_btn_w, "Reset WiFi");
    lv_obj_center(label_btn_w);

    // Tab Battery
    arc_soc = lv_arc_create(tab_batt);
    lv_obj_set_size(arc_soc, 140, 140);
    lv_arc_set_rotation(arc_soc, 135);
    lv_arc_set_bg_angles(arc_soc, 0, 270);
    lv_arc_set_value(arc_soc, 0);
    lv_obj_align(arc_soc, LV_ALIGN_LEFT_MID, 0, 15);
    lv_obj_remove_style(arc_soc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_soc, LV_OBJ_FLAG_CLICKABLE);

    label_batt_status = lv_label_create(tab_batt);
    lv_label_set_text(label_batt_status, "");
    lv_obj_set_style_text_font(label_batt_status, &lv_font_montserrat_14, 0);
    lv_obj_align_to(label_batt_status, arc_soc, LV_ALIGN_CENTER, -20, -35);

    label_soc_val = lv_label_create(tab_batt);
    lv_label_set_text(label_soc_val, "0%");
    lv_obj_add_style(label_soc_val, &style_big_num, 0);
    lv_obj_align_to(label_soc_val, arc_soc, LV_ALIGN_CENTER, -5, -10);

    label_temp_val = lv_label_create(tab_batt);
    lv_label_set_text(label_temp_val, "-- °C");
    lv_obj_set_style_text_font(label_temp_val, &lv_font_montserrat_16, 0);
    lv_obj_align_to(label_temp_val, arc_soc, LV_ALIGN_CENTER, -5, 15);

    label_batt_pv = lv_label_create(tab_batt);
    lv_label_set_text(label_batt_pv, "PV 0.00kW");
    lv_obj_set_style_text_font(label_batt_pv, &lv_font_montserrat_14, 0);
    lv_obj_set_width(label_batt_pv, 90);
    lv_obj_set_style_text_align(label_batt_pv, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label_batt_pv, LV_LABEL_LONG_CLIP);
    lv_obj_align_to(label_batt_pv, arc_soc, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t* col_params = lv_obj_create(tab_batt);
    lv_obj_set_size(col_params, 140, 170);
    lv_obj_align(col_params, LV_ALIGN_RIGHT_MID, 0, 7);
    lv_obj_set_scrollbar_mode(col_params, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(col_params, 0, 0);
    lv_obj_set_flex_flow(col_params, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_params, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(col_params, 5, 0);

    lv_obj_t* l1 = lv_label_create(col_params);
    lv_label_set_text(l1, "Voltage:");
    lv_obj_add_style(l1, &style_label_gray, 0);
    label_volts = lv_label_create(col_params);
    lv_label_set_text(label_volts, "-- V");
    lv_obj_set_style_text_font(label_volts, &lv_font_montserrat_18, 0);

    lv_obj_t* l2 = lv_label_create(col_params);
    lv_label_set_text(l2, "Current:");
    lv_obj_add_style(l2, &style_label_gray, 0);
    label_amps = lv_label_create(col_params);
    lv_label_set_text(label_amps, "-- A");
    lv_obj_set_style_text_font(label_amps, &lv_font_montserrat_18, 0);

    lv_obj_t* l3 = lv_label_create(col_params);
    lv_label_set_text(l3, "Power:");
    lv_obj_add_style(l3, &style_label_gray, 0);
    label_watts = lv_label_create(col_params);
    lv_label_set_text(label_watts, "-- W");
    lv_obj_set_style_text_font(label_watts, &lv_font_montserrat_18, 0);

    // Tab Grid: 2×2 карточки, без прокрутки вкладки.
    lv_obj_clear_flag(tab_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tab_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(tab_grid, 2, 0);

    lv_obj_t* cont_g = lv_obj_create(tab_grid);
    lv_obj_set_size(cont_g, 128, 68);
    lv_obj_align(cont_g, LV_ALIGN_TOP_LEFT, 4, 28);
    styleCompactCard(cont_g);
    lv_obj_t* lbl_g_t = lv_label_create(cont_g);
    lv_label_set_text(lbl_g_t, "GRID");
    lv_obj_align(lbl_g_t, LV_ALIGN_TOP_MID, 0, 0);
    label_grid_val = lv_label_create(cont_g);
    lv_label_set_text(label_grid_val, "-- W");
    lv_obj_set_style_text_font(label_grid_val, &lv_font_montserrat_16, 0);
    lv_obj_align(label_grid_val, LV_ALIGN_CENTER, 0, 2);
    label_grid_volts = lv_label_create(cont_g);
    lv_label_set_text(label_grid_volts, "-- V");
    lv_obj_align(label_grid_volts, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* cont_l = lv_obj_create(tab_grid);
    lv_obj_set_size(cont_l, 128, 68);
    lv_obj_align(cont_l, LV_ALIGN_TOP_RIGHT, -4, 28);
    styleCompactCard(cont_l);
    lv_obj_t* lbl_l_t = lv_label_create(cont_l);
    lv_label_set_text(lbl_l_t, "HOUSE");
    lv_obj_align(lbl_l_t, LV_ALIGN_TOP_MID, 0, 0);
    label_load_val = lv_label_create(cont_l);
    lv_label_set_text(label_load_val, "-- W");
    lv_obj_add_style(label_load_val, &style_big_num, 0);
    lv_obj_align(label_load_val, LV_ALIGN_CENTER, 0, 6);

    label_arrow_grid = lv_label_create(tab_grid);
    lv_obj_set_style_text_font(label_arrow_grid, &lv_font_montserrat_16, 0);
    lv_label_set_text(label_arrow_grid, LV_SYMBOL_RIGHT);
    lv_obj_align(label_arrow_grid, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t* cont_src = lv_obj_create(tab_grid);
    lv_obj_set_size(cont_src, 128, 68);
    lv_obj_align(cont_src, LV_ALIGN_TOP_LEFT, 4, 102);
    styleCompactCard(cont_src);
    lv_obj_t* lbl_src_t = lv_label_create(cont_src);
    lv_label_set_text(lbl_src_t, "SOURCE");
    lv_obj_align(lbl_src_t, LV_ALIGN_TOP_MID, 0, 0);
    label_source_val = lv_label_create(cont_src);
    lv_label_set_text(label_source_val, "--");
    lv_obj_set_style_text_font(label_source_val, &lv_font_montserrat_16, 0);
    lv_obj_align(label_source_val, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t* cont_b_t2 = lv_obj_create(tab_grid);
    lv_obj_set_size(cont_b_t2, 128, 68);
    lv_obj_align(cont_b_t2, LV_ALIGN_TOP_RIGHT, -4, 102);
    styleCompactCard(cont_b_t2);
    lv_obj_t* lbl_b_t_t2 = lv_label_create(cont_b_t2);
    lv_label_set_text(lbl_b_t_t2, "BATTERY");
    lv_obj_align(lbl_b_t_t2, LV_ALIGN_TOP_MID, 0, 0);
    label_batt_val_t2 = lv_label_create(cont_b_t2);
    lv_label_set_text(label_batt_val_t2, "-- W");
    lv_obj_add_style(label_batt_val_t2, &style_big_num, 0);
    lv_obj_align(label_batt_val_t2, LV_ALIGN_CENTER, 0, 6);

    label_arrow_batt = lv_label_create(tab_grid);
    lv_obj_set_style_text_font(label_arrow_batt, &lv_font_montserrat_16, 0);
    lv_label_set_text(label_arrow_batt, LV_SYMBOL_UP);
    lv_obj_align(label_arrow_batt, LV_ALIGN_TOP_RIGHT, -58, 86);

    // Tab Solar: суммарная мощность, выработка за день, стринги PV1/PV2
    lv_obj_t* cont_pv_total = lv_obj_create(tab_solar);
    lv_obj_set_size(cont_pv_total, 145, 70);
    lv_obj_align(cont_pv_total, LV_ALIGN_TOP_LEFT, 5, 15);
    lv_obj_t* lbl_pv_t = lv_label_create(cont_pv_total);
    lv_label_set_text(lbl_pv_t, "PV Power");
    lv_obj_align(lbl_pv_t, LV_ALIGN_TOP_MID, 0, -5);
    label_pv_total = lv_label_create(cont_pv_total);
    lv_label_set_text(label_pv_total, "-- W");
    lv_obj_set_style_text_font(label_pv_total, &lv_font_montserrat_22, 0);
    lv_obj_align(label_pv_total, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t* cont_pv_day = lv_obj_create(tab_solar);
    lv_obj_set_size(cont_pv_day, 145, 70);
    lv_obj_align(cont_pv_day, LV_ALIGN_TOP_RIGHT, -5, 15);
    lv_obj_t* lbl_pv_d = lv_label_create(cont_pv_day);
    lv_label_set_text(lbl_pv_d, "Today");
    lv_obj_align(lbl_pv_d, LV_ALIGN_TOP_MID, 0, -5);
    label_pv_day = lv_label_create(cont_pv_day);
    lv_label_set_text(label_pv_day, "-- kWh");
    lv_obj_add_style(label_pv_day, &style_big_num, 0);
    lv_obj_align(label_pv_day, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t* cont_pv1 = lv_obj_create(tab_solar);
    lv_obj_set_size(cont_pv1, 145, 90);
    lv_obj_align(cont_pv1, LV_ALIGN_TOP_LEFT, 5, 95);
    lv_obj_t* lbl_pv1 = lv_label_create(cont_pv1);
    lv_label_set_text(lbl_pv1, "PV1");
    lv_obj_align(lbl_pv1, LV_ALIGN_TOP_MID, 0, -5);
    label_pv1_watts = lv_label_create(cont_pv1);
    lv_label_set_text(label_pv1_watts, "-- W");
    lv_obj_add_style(label_pv1_watts, &style_big_num, 0);
    lv_obj_align(label_pv1_watts, LV_ALIGN_CENTER, 0, -2);
    label_pv1_volts = lv_label_create(cont_pv1);
    lv_label_set_text(label_pv1_volts, "-- V");
    lv_obj_align(label_pv1_volts, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    label_pv1_amps = lv_label_create(cont_pv1);
    lv_label_set_text(label_pv1_amps, "-- A");
    lv_obj_align(label_pv1_amps, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t* cont_pv2 = lv_obj_create(tab_solar);
    lv_obj_set_size(cont_pv2, 145, 90);
    lv_obj_align(cont_pv2, LV_ALIGN_TOP_RIGHT, -5, 95);
    lv_obj_t* lbl_pv2 = lv_label_create(cont_pv2);
    lv_label_set_text(lbl_pv2, "PV2");
    lv_obj_align(lbl_pv2, LV_ALIGN_TOP_MID, 0, -5);
    label_pv2_watts = lv_label_create(cont_pv2);
    lv_label_set_text(label_pv2_watts, "-- W");
    lv_obj_add_style(label_pv2_watts, &style_big_num, 0);
    lv_obj_align(label_pv2_watts, LV_ALIGN_CENTER, 0, -2);
    label_pv2_volts = lv_label_create(cont_pv2);
    lv_label_set_text(label_pv2_volts, "-- V");
    lv_obj_align(label_pv2_volts, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    label_pv2_amps = lv_label_create(cont_pv2);
    lv_label_set_text(label_pv2_amps, "-- A");
    lv_obj_align(label_pv2_amps, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // Top layer: loader + status
    lv_obj_t* top_layer = lv_layer_top();
    arc_loader = lv_spinner_create(top_layer, 1000, 60);
    lv_obj_set_size(arc_loader, 20, 20);
    lv_obj_align(arc_loader, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_flag(arc_loader, LV_OBJ_FLAG_HIDDEN);

    label_status = lv_label_create(top_layer);
    lv_label_set_text(label_status, "Init...");
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, 0);
    lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 5, 5);

    lv_timer_create([](lv_timer_t* t) { uiUpdateStatusBar(); }, 1000, NULL);

    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_btnmatrix_set_btn_ctrl(tab_btns, UI_TAB_SETTINGS, LV_BTNMATRIX_CTRL_HIDDEN);
}

void uiUpdate() {
    uint16_t soc = inverterGetBattSOC();
    int16_t battPwr = inverterGetBattPower();
    float v = inverterGetBattVolts();
    float a = inverterGetBattCurrent();
    float temp = inverterGetBattTemp();
    int16_t gridPwr = inverterGetGridPower();
    float gridV = inverterGetGridVolts();
    uint16_t loadPwr = inverterGetLoadPower();
    uint32_t pvTotal = inverterGetPvTotalPower();
    float pvDay = inverterGetDayPvEnergy();
    float pv1V = inverterGetPv1Volts();
    float pv1A = inverterGetPv1Current();
    uint16_t pv1W = inverterGetPv1Power();
    float pv2V = inverterGetPv2Volts();
    float pv2A = inverterGetPv2Current();
    uint16_t pv2W = inverterGetPv2Power();

    lv_arc_set_value(arc_soc, soc);
    lv_label_set_text(label_soc_val, (String(soc) + "%").c_str());

    if (soc < 20) lv_obj_set_style_arc_color(arc_soc, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    else if (soc < 50) lv_obj_set_style_arc_color(arc_soc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    else lv_obj_set_style_arc_color(arc_soc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);

    if (battPwr < -10) {
        lv_label_set_text(label_batt_status, LV_SYMBOL_CHARGE " CHG");
        lv_obj_set_style_text_color(label_batt_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else if (battPwr > 10) {
        lv_label_set_text(label_batt_status, LV_SYMBOL_DOWN " DCH");
        lv_obj_set_style_text_color(label_batt_status, lv_palette_main(LV_PALETTE_RED), 0);
    } else {
        lv_label_set_text(label_batt_status, "Idle");
        lv_obj_set_style_text_color(label_batt_status, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    lv_label_set_text(label_volts, (String(v, 2) + " V").c_str());
    lv_label_set_text(label_amps, (String(a, 2) + " A").c_str());
    lv_label_set_text(label_watts, (String(battPwr) + " W").c_str());
    lv_label_set_text(label_temp_val, (String(temp, 1) + " °C").c_str());
    if (temp < 5.0f || temp > 45.0f)
        lv_obj_set_style_text_color(label_temp_val, lv_palette_main(LV_PALETTE_RED), 0);
    else
        lv_obj_set_style_text_color(label_temp_val, lv_color_black(), 0);

    lv_label_set_text(label_batt_pv, (String("PV ") + String(pvTotal / 1000.0f, 2) + "kW").c_str());
    lv_obj_set_width(label_batt_pv, 90);
    if (pvTotal > 10)
        lv_obj_set_style_text_color(label_batt_pv, lv_palette_main(LV_PALETTE_ORANGE), 0);
    else
        lv_obj_set_style_text_color(label_batt_pv, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_label_set_text(label_grid_val, (String(gridPwr) + " W").c_str());
    lv_label_set_text(label_grid_volts, (String(gridV, 1) + " V").c_str());
    lv_label_set_text(label_load_val, (String(loadPwr) + " W").c_str());
    lv_label_set_text(label_batt_val_t2, (String(battPwr) + " W").c_str());

    if (gridPwr > 10) {
        lv_label_set_text(label_arrow_grid, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(label_arrow_grid, lv_palette_main(LV_PALETTE_RED), 0);
    } else if (gridPwr < -10) {
        lv_label_set_text(label_arrow_grid, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(label_arrow_grid, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(label_arrow_grid, LV_SYMBOL_MINUS);
        lv_obj_set_style_text_color(label_arrow_grid, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    if (battPwr > 10) {
        lv_label_set_text(label_arrow_batt, LV_SYMBOL_UP);
        lv_obj_set_style_text_color(label_arrow_batt, lv_palette_main(LV_PALETTE_ORANGE), 0);
    } else if (battPwr < -10) {
        lv_label_set_text(label_arrow_batt, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_color(label_arrow_batt, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(label_arrow_batt, LV_SYMBOL_MINUS);
        lv_obj_set_style_text_color(label_arrow_batt, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    // Источники, которые сейчас кормят дом (не заряд батареи и не экспорт в сеть).
    const bool fromSolar = pvTotal > 10;
    const bool fromBatt = battPwr > 10;
    const bool fromGrid = gridPwr > 10;
    char srcText[28] = "Idle";
    if (fromSolar || fromBatt || fromGrid) {
        int n = 0;
        if (fromSolar) n += snprintf(srcText + n, sizeof(srcText) - (size_t)n, "%sPV", n ? "+" : "");
        if (fromBatt) n += snprintf(srcText + n, sizeof(srcText) - (size_t)n, "%sBat", n ? "+" : "");
        if (fromGrid) snprintf(srcText + n, sizeof(srcText) - (size_t)n, "%sGrid", n ? "+" : "");
    }
    lv_label_set_text(label_source_val, srcText);
    if (fromGrid)
        lv_obj_set_style_text_color(label_source_val, lv_palette_main(LV_PALETTE_RED), 0);
    else if (fromSolar)
        lv_obj_set_style_text_color(label_source_val, lv_palette_main(LV_PALETTE_ORANGE), 0);
    else if (fromBatt)
        lv_obj_set_style_text_color(label_source_val, lv_palette_main(LV_PALETTE_ORANGE), 0);
    else
        lv_obj_set_style_text_color(label_source_val, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_label_set_text(label_pv_total, (String(pvTotal) + " W").c_str());
    if (pvTotal > 10)
        lv_obj_set_style_text_color(label_pv_total, lv_palette_main(LV_PALETTE_ORANGE), 0);
    else
        lv_obj_set_style_text_color(label_pv_total, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(label_pv_day, (String(pvDay, 1) + " kWh").c_str());
    lv_label_set_text(label_pv1_watts, (String(pv1W) + " W").c_str());
    lv_label_set_text(label_pv1_volts, (String(pv1V, 1) + " V").c_str());
    lv_label_set_text(label_pv1_amps, (String(pv1A, 1) + " A").c_str());
    lv_label_set_text(label_pv2_watts, (String(pv2W) + " W").c_str());
    lv_label_set_text(label_pv2_volts, (String(pv2V, 1) + " V").c_str());
    lv_label_set_text(label_pv2_amps, (String(pv2A, 1) + " A").c_str());
}

void uiUpdateStatusBar() {
    uint16_t soc = inverterGetBattSOC();
    int16_t battPwr = inverterGetBattPower();
    const char* bat_symbol = LV_SYMBOL_BATTERY_FULL;
    if (soc < 5) bat_symbol = LV_SYMBOL_BATTERY_EMPTY;
    else if (soc < 25) bat_symbol = LV_SYMBOL_BATTERY_1;
    else if (soc < 50) bat_symbol = LV_SYMBOL_BATTERY_2;
    else if (soc < 75) bat_symbol = LV_SYMBOL_BATTERY_3;

    const char* pwr_symbol = "";
    if (battPwr < -10) pwr_symbol = LV_SYMBOL_CHARGE;
    else if (battPwr > 10) pwr_symbol = LV_SYMBOL_DOWN;

    char timeStr[20] = "--:--";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10))
        sprintf(timeStr, "%02d.%02d %02d:%02d",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_hour, timeinfo.tm_min);
    else {
        static uint32_t lastTimeErr = 0;
        if (millis() - lastTimeErr > 5000) { lastTimeErr = millis(); Serial.println("Time sync failed yet..."); }
    }

    const char* wifi_display;
    String ssidPart = "";
    if (WiFi.status() != WL_CONNECTED) {
        wifi_display = "No WiFi";
    } else {
        int rssi = WiFi.RSSI();
        if (rssi > -60) wifi_display = "IIII";
        else if (rssi > -70) wifi_display = "III";
        else if (rssi > -80) wifi_display = "II";
        else wifi_display = "I";
        String ssid = WiFi.SSID();
        if (ssid.length() > 0) {
            const int SSID_MAX_LEN = 12;
            if (ssid.length() > SSID_MAX_LEN) ssid = ssid.substring(0, SSID_MAX_LEN - 1) + "…";
            ssidPart = " " + ssid;
        }
    }
    uint32_t invLastOk = inverterGetLastSuccessTimestamp();
    String invPart;
    if (invLastOk == 0) {
        invPart = "INV?";   // инвертор ещё не ответил
    } else {
        uint32_t ageS = (millis() - invLastOk) / 1000;
        if (ageS <= 60)      invPart = "INV";   // свежие данные
        else                 invPart = "INV!";  // давно не обновлялось
    }

    // Строка: батарея, мощность, дата+время, статус инвертора, уровень Wi‑Fi.
    String statusStr = String(bat_symbol) + " " + String(soc) + "% " + String(pwr_symbol) + "  " +
                      String(timeStr) + "  " + invPart + "  " +
                      String(LV_SYMBOL_WIFI) + ssidPart + " " + String(wifi_display);
    lv_label_set_text(label_status, statusStr.c_str());
}

void uiSetStatusText(const char* text) {
    lv_label_set_text(label_status, text);
}

void uiShowLoader(bool show) {
    if (show)
        lv_obj_clear_flag(arc_loader, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(arc_loader, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* uiGetTabview() {
    return tabview;
}
