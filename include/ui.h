#ifndef UI_H
#define UI_H

#include <lvgl.h>

/** Индексы вкладок TabView. Settings скрыта в таббаре, открывается длинным BOOT. */
enum UiTabIndex {
    UI_TAB_BATTERY = 0,
    UI_TAB_GRID = 1,
    UI_TAB_SOLAR = 2,
    UI_TAB_SETTINGS = 3
};

/** Построить все экраны (TabView, вкладки, виджеты). Вызвать после lvglInit и touchRegisterLvglIndev. */
void uiBuild();

/** Обновить виджеты данными из inverter (вызывается после inverterHandleResponse или по таймеру). */
void uiUpdate();

/** Обновить строку статус-бара (батарея, время, Wi‑Fi). Вызывается таймером LVGL. */
void uiUpdateStatusBar();

/** Установить текст в статус-баре (для network при ретраях). */
void uiSetStatusText(const char* text);

/** Показать/скрыть спиннер загрузки (для inverter). */
void uiShowLoader(bool show);

/** Получить TabView для проверки текущей вкладки и переключения (main loop). */
lv_obj_t* uiGetTabview();

#endif
