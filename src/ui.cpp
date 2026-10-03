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
// Вкладка Flow: схема потоков энергии
static lv_obj_t* label_flow_pv;
static lv_obj_t* label_flow_grid;
static lv_obj_t* label_flow_grid_volts;
static lv_obj_t* label_flow_batt;
static lv_obj_t* label_flow_soc;
static lv_obj_t* label_flow_batt_temp;
static lv_obj_t* label_flow_load;
static lv_obj_t* label_flow_home;

// Иконки вкладки Flow (src/flow_icons.c, генерируются tools/gen_flow_icons.py)
LV_IMG_DECLARE(flow_icon_pv);
LV_IMG_DECLARE(flow_icon_grid);
LV_IMG_DECLARE(flow_icon_inverter);
LV_IMG_DECLARE(flow_icon_load);
LV_IMG_DECLARE(flow_icon_home);

/** Оранжевый для текста: темнее палитрового, чтобы читался на белом фоне. */
static lv_color_t colorOrangeText() {
    return lv_palette_darken(LV_PALETTE_ORANGE, 3);
}

// --- Вкладка Flow ---
// Координаты в пикселях вкладки (320×200, отступы 0). Сверху ~20 px занимает статус-бар.
// Инвертор в центре: x 140..180, y 85..121. Линии пунктирные, только горизонтальные
// и вертикальные отрезки (в LVGL 8 пунктир рисуется только для них).
static lv_point_t flow_pts_pv[]   = {{58, 38}, {100, 38}, {100, 95}, {140, 95}};
static lv_point_t flow_pts_grid[] = {{262, 38}, {220, 38}, {220, 95}, {180, 95}};
static lv_point_t flow_pts_batt[] = {{58, 149}, {100, 149}, {100, 111}, {140, 111}};
static lv_point_t flow_pts_load[] = {{160, 121}, {160, 132}, {210, 132}, {210, 142}};
static lv_point_t flow_pts_home[] = {{180, 111}, {287, 111}, {287, 142}};

/** Связь узла с инвертором: пунктир, стрелка и бегущая точка. */
struct FlowLink {
    lv_point_t* pts;
    uint16_t n;
    uint16_t len;      // длина пути в px (сумма отрезков)
    lv_obj_t* line;
    lv_obj_t* arrow;
    lv_obj_t* dot;
    int8_t dir;        // 0 — потока нет, 1 — по порядку точек, -1 — обратно
};
// Точки pv/grid/batt идут от узла к инвертору, load и home — от инвертора к нагрузке.
static FlowLink flow_pv   = {flow_pts_pv, 4};
static FlowLink flow_grid = {flow_pts_grid, 4};
static FlowLink flow_batt = {flow_pts_batt, 4};
static FlowLink flow_load = {flow_pts_load, 4};
static FlowLink flow_home = {flow_pts_home, 3};

static const uint32_t FLOW_DOT_MS_PER_PX = 20;  // скорость точки ~50 px/s

/** Цвет неактивной линии: обычный серый, светлее выцветает на CYD. */
static lv_color_t flowIdleColor() {
    return lv_palette_main(LV_PALETTE_GREY);
}

static void flowMakeLine(lv_obj_t* parent, FlowLink& link, lv_style_t* style) {
    link.line = lv_line_create(parent);
    lv_line_set_points(link.line, link.pts, link.n);
    lv_obj_add_style(link.line, style, 0);
    lv_obj_set_pos(link.line, 0, 0);
    link.len = 0;
    for (uint16_t i = 0; i + 1 < link.n; i++)
        link.len += abs(link.pts[i + 1].x - link.pts[i].x) + abs(link.pts[i + 1].y - link.pts[i].y);
}

/** Точка-кружок, которая бежит по линии. Создаётся до стрелок, чтобы стрелка была поверх. */
static void flowMakeDot(lv_obj_t* parent, FlowLink& link) {
    link.dot = lv_obj_create(parent);
    lv_obj_remove_style_all(link.dot);
    lv_obj_set_size(link.dot, 6, 6);
    lv_obj_set_style_radius(link.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(link.dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(link.dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(link.dot, LV_OBJ_FLAG_HIDDEN);
}

/** Анимация: v — пройденное расстояние по пути в px. Отрезки только горизонтальные/вертикальные. */
static void flowDotAnimCb(void* var, int32_t v) {
    FlowLink* link = (FlowLink*)var;
    int32_t d = link->dir < 0 ? (int32_t)link->len - v : v;
    lv_coord_t x = link->pts[0].x, y = link->pts[0].y;
    for (uint16_t i = 0; i + 1 < link->n; i++) {
        const lv_point_t& a = link->pts[i];
        const lv_point_t& b = link->pts[i + 1];
        int32_t seg = abs(b.x - a.x) + abs(b.y - a.y);
        int32_t step = d < seg ? d : seg;
        x = a.x + (b.x > a.x ? step : (b.x < a.x ? -step : 0));
        y = a.y + (b.y > a.y ? step : (b.y < a.y ? -step : 0));
        if (d <= seg) break;
        d -= seg;
    }
    lv_obj_set_pos(link->dot, x - 3, y - 3);
}

/** Пустая плашка-узел 50×28 с рамкой. */
static lv_obj_t* flowMakeBox(lv_obj_t* parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, 50, 28);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    // Рамка обычного серого: светлее теряется у края экрана CYD, темнее спорит с линиями.
    lv_obj_set_style_border_color(box, lv_palette_main(LV_PALETTE_GREY), 0);
    return box;
}

/** Плашка-узел с текстом по центру. Возвращает метку внутри. */
static lv_obj_t* flowMakeNode(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, const char* text) {
    lv_obj_t* label = lv_label_create(flowMakeBox(parent, x, y));
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return label;
}

/** Плашка-узел с иконкой 24×24 (альфа-маска, цвет через img_recolor). Возвращает плашку. */
static lv_obj_t* flowMakeIconNode(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                  const lv_img_dsc_t* icon, lv_color_t color) {
    lv_obj_t* box = flowMakeBox(parent, x, y);
    lv_obj_t* img = lv_img_create(box);
    lv_img_set_src(img, icon);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    return box;
}

/** Подпись значения под узлом: ширина 100, выравнивание текста задаётся. */
static lv_obj_t* flowMakeValue(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_text_align_t align) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, 100);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_label_set_text(label, "-- W");
    return label;
}

/** Стрелка направления на линии: белый фон закрывает пунктир под ней. (x, y) — центр. */
static lv_obj_t* flowMakeArrow(lv_obj_t* parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* arrow = lv_label_create(parent);
    lv_obj_set_size(arrow, 16, 16);
    lv_obj_set_pos(arrow, x - 8, y - 8);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(arrow, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(arrow, LV_OPA_COVER, 0);
    lv_label_set_text(arrow, "");
    lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
    return arrow;
}

/** Активная линия красится в цвет потока, показывает стрелку и бегущую точку; неактивная — серая.
 *  Анимация перезапускается только при смене направления, иначе точка дёргалась бы каждый опрос. */
static void flowSetLink(FlowLink& link, int8_t dir, const char* symbol, lv_color_t color) {
    if (dir != 0) {
        lv_obj_set_style_line_color(link.line, color, 0);
        lv_label_set_text(link.arrow, symbol);
        lv_obj_set_style_text_color(link.arrow, color, 0);
        lv_obj_clear_flag(link.arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(link.dot, color, 0);
        lv_obj_clear_flag(link.dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_line_color(link.line, flowIdleColor(), 0);
        lv_obj_add_flag(link.arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(link.dot, LV_OBJ_FLAG_HIDDEN);
    }
    if (dir == link.dir) return;
    link.dir = dir;
    lv_anim_del(&link, flowDotAnimCb);
    if (dir == 0) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &link);
    lv_anim_set_exec_cb(&a, flowDotAnimCb);
    lv_anim_set_values(&a, 0, link.len);
    lv_anim_set_time(&a, link.len * FLOW_DOT_MS_PER_PX);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/** Мощность для схемы: до 1000 W в ваттах, дальше в kW с двумя знаками. */
static String flowFormatPower(int32_t w) {
    if (w < 0) w = -w;
    if (w < 1000) return String(w) + " W";
    return String(w / 1000.0f, 2) + " kW";
}

/** Тап по узлу или его подписи открывает вкладку с подробностями (индекс в user_data). */
static void flow_node_click_cb(lv_event_t* e) {
    lv_tabview_set_act(tabview, (uint32_t)(uintptr_t)lv_event_get_user_data(e), LV_ANIM_ON);
}

/** Сделать объект кнопкой перехода на вкладку. Дети (иконка, текст) передают нажатие родителю. */
static void flowMakeTapTarget(lv_obj_t* obj, UiTabIndex tab_idx) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, flow_node_click_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)tab_idx);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

/** Плашка-узел как кнопка: зона нажатия шире на 10 px, при нажатии фон темнеет. */
static void flowMakeTapNode(lv_obj_t* box, UiTabIndex tab_idx) {
    flowMakeTapTarget(box, tab_idx);
    lv_obj_set_ext_click_area(box, 10);
    lv_obj_set_style_bg_color(box, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_STATE_PRESSED);
}

static void buildFlowTab(lv_obj_t* tab) {
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(tab, 0, 0);

    static lv_style_t style_flow_line;
    lv_style_init(&style_flow_line);
    lv_style_set_line_width(&style_flow_line, 2);
    lv_style_set_line_dash_width(&style_flow_line, 4);
    lv_style_set_line_dash_gap(&style_flow_line, 3);
    lv_style_set_line_color(&style_flow_line, flowIdleColor());

    // Линии первыми, чтобы узлы и стрелки рисовались поверх.
    flowMakeLine(tab, flow_pv, &style_flow_line);
    flowMakeLine(tab, flow_grid, &style_flow_line);
    flowMakeLine(tab, flow_batt, &style_flow_line);
    flowMakeLine(tab, flow_load, &style_flow_line);
    flowMakeLine(tab, flow_home, &style_flow_line);

    lv_color_t icon_dark = lv_palette_darken(LV_PALETTE_GREY, 4);
    flowMakeTapNode(flowMakeIconNode(tab, 8, 24, &flow_icon_pv, colorOrangeText()), UI_TAB_SOLAR);
    flowMakeTapNode(flowMakeIconNode(tab, 262, 24, &flow_icon_grid, icon_dark), UI_TAB_GRID);
    label_flow_soc = flowMakeNode(tab, 8, 135, "--%");
    lv_obj_set_style_text_font(label_flow_soc, &lv_font_montserrat_16, 0);
    flowMakeTapNode(lv_obj_get_parent(label_flow_soc), UI_TAB_BATTERY);
    flowMakeTapNode(flowMakeIconNode(tab, 185, 142, &flow_icon_load, icon_dark), UI_TAB_GRID);
    flowMakeTapNode(flowMakeIconNode(tab, 262, 142, &flow_icon_home, icon_dark), UI_TAB_GRID);

    // Инвертор чуть крупнее остальных узлов.
    lv_obj_t* box_inv = flowMakeIconNode(tab, 140, 85, &flow_icon_inverter, lv_palette_main(LV_PALETTE_BLUE));
    lv_obj_set_size(box_inv, 40, 36);
    lv_obj_center(lv_obj_get_child(box_inv, 0));
    lv_obj_set_style_border_color(box_inv, lv_palette_main(LV_PALETTE_BLUE), 0);

    label_flow_pv = flowMakeValue(tab, 8, 54, LV_TEXT_ALIGN_LEFT);
    label_flow_grid = flowMakeValue(tab, 212, 54, LV_TEXT_ALIGN_RIGHT);
    label_flow_grid_volts = flowMakeValue(tab, 212, 72, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_text_font(label_flow_grid_volts, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_flow_grid_volts, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(label_flow_grid_volts, "-- V");
    label_flow_batt = flowMakeValue(tab, 8, 165, LV_TEXT_ALIGN_LEFT);
    // Температура батареи над её плашкой (ниже места нет — таббар).
    label_flow_batt_temp = flowMakeValue(tab, 8, 115, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(label_flow_batt_temp, 80);
    lv_obj_set_style_text_font(label_flow_batt_temp, &lv_font_montserrat_14, 0);
    lv_label_set_text(label_flow_batt_temp, "-- °C");
    label_flow_load = flowMakeValue(tab, 160, 172, LV_TEXT_ALIGN_CENTER);
    label_flow_home = flowMakeValue(tab, 212, 172, LV_TEXT_ALIGN_RIGHT);
    flowMakeTapTarget(label_flow_pv, UI_TAB_SOLAR);
    flowMakeTapTarget(label_flow_grid, UI_TAB_GRID);
    flowMakeTapTarget(label_flow_grid_volts, UI_TAB_GRID);
    flowMakeTapTarget(label_flow_batt, UI_TAB_BATTERY);
    flowMakeTapTarget(label_flow_batt_temp, UI_TAB_BATTERY);
    flowMakeTapTarget(label_flow_load, UI_TAB_GRID);
    flowMakeTapTarget(label_flow_home, UI_TAB_GRID);

    flowMakeDot(tab, flow_pv);
    flowMakeDot(tab, flow_grid);
    flowMakeDot(tab, flow_batt);
    flowMakeDot(tab, flow_load);
    flowMakeDot(tab, flow_home);

    flow_pv.arrow = flowMakeArrow(tab, 120, 95);
    flow_grid.arrow = flowMakeArrow(tab, 200, 95);
    flow_batt.arrow = flowMakeArrow(tab, 120, 111);
    flow_load.arrow = flowMakeArrow(tab, 185, 132);
    flow_home.arrow = flowMakeArrow(tab, 240, 111);
}

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
    lv_obj_t* tab_flow = lv_tabview_add_tab(tabview, "Flow");
    lv_obj_t* tab_batt = lv_tabview_add_tab(tabview, "Battery");
    lv_obj_t* tab_grid = lv_tabview_add_tab(tabview, "Grid");
    lv_obj_t* tab_solar = lv_tabview_add_tab(tabview, "Solar");
    lv_obj_clear_flag(tab_solar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* tab_settings = lv_tabview_add_tab(tabview, "Settings");

    buildFlowTab(tab_flow);

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
    lv_label_set_text(label_batt_pv, "PV --kWh --kW");
    lv_obj_set_style_text_font(label_batt_pv, &lv_font_montserrat_14, 0);
    lv_obj_set_width(label_batt_pv, 140);
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
    uint16_t homePwr = inverterGetHomePower();
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

    lv_label_set_text(label_batt_pv, (String("PV ") + String(pvDay, 1) + "kWh " + String(pvTotal / 1000.0f, 1) + "kW").c_str());
    lv_obj_set_width(label_batt_pv, 140);
    if (pvTotal > 10)
        lv_obj_set_style_text_color(label_batt_pv, colorOrangeText(), 0);
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
        lv_obj_set_style_text_color(label_arrow_batt, colorOrangeText(), 0);
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
        lv_obj_set_style_text_color(label_source_val, colorOrangeText(), 0);
    else if (fromBatt)
        lv_obj_set_style_text_color(label_source_val, colorOrangeText(), 0);
    else
        lv_obj_set_style_text_color(label_source_val, lv_palette_main(LV_PALETTE_GREY), 0);

    // Flow: стрелки показывают направление потока относительно инвертора.
    lv_label_set_text(label_flow_pv, flowFormatPower(pvTotal).c_str());
    flowSetLink(flow_pv, pvTotal > 10 ? 1 : 0, LV_SYMBOL_RIGHT, colorOrangeText());

    lv_label_set_text(label_flow_grid, flowFormatPower(gridPwr).c_str());
    lv_label_set_text(label_flow_grid_volts, (String(gridV, 1) + " V").c_str());
    if (gridPwr > 10)
        flowSetLink(flow_grid, 1, LV_SYMBOL_LEFT, lv_palette_main(LV_PALETTE_RED));
    else if (gridPwr < -10)
        flowSetLink(flow_grid, -1, LV_SYMBOL_RIGHT, lv_palette_main(LV_PALETTE_GREEN));
    else
        flowSetLink(flow_grid, 0, "", lv_color_black());

    lv_label_set_text(label_flow_soc, (String(soc) + "%").c_str());
    if (soc < 20) lv_obj_set_style_text_color(label_flow_soc, lv_palette_main(LV_PALETTE_RED), 0);
    else if (soc < 50) lv_obj_set_style_text_color(label_flow_soc, colorOrangeText(), 0);
    else lv_obj_set_style_text_color(label_flow_soc, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(label_flow_batt, flowFormatPower(battPwr).c_str());
    lv_label_set_text(label_flow_batt_temp, (String(temp, 1) + " °C").c_str());
    if (temp < 5.0f || temp > 45.0f)
        lv_obj_set_style_text_color(label_flow_batt_temp, lv_palette_main(LV_PALETTE_RED), 0);
    else
        lv_obj_set_style_text_color(label_flow_batt_temp, lv_palette_main(LV_PALETTE_GREY), 0);
    if (battPwr > 10)
        flowSetLink(flow_batt, 1, LV_SYMBOL_RIGHT, colorOrangeText());
    else if (battPwr < -10)
        flowSetLink(flow_batt, -1, LV_SYMBOL_LEFT, lv_palette_main(LV_PALETTE_GREEN));
    else
        flowSetLink(flow_batt, 0, "", lv_color_black());

    lv_label_set_text(label_flow_load, flowFormatPower(loadPwr).c_str());
    flowSetLink(flow_load, loadPwr > 10 ? 1 : 0, LV_SYMBOL_RIGHT, lv_palette_main(LV_PALETTE_BLUE));

    // Home = внешний CT - внутренний; порог 20 W, т.к. CT даёт смещение ~9 W без нагрузки.
    lv_label_set_text(label_flow_home, flowFormatPower(homePwr).c_str());
    flowSetLink(flow_home, homePwr > 20 ? 1 : 0, LV_SYMBOL_RIGHT, lv_palette_main(LV_PALETTE_TEAL));

    lv_label_set_text(label_pv_total, (String(pvTotal) + " W").c_str());
    if (pvTotal > 10)
        lv_obj_set_style_text_color(label_pv_total, colorOrangeText(), 0);
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
