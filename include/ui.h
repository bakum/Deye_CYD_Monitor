#ifndef UI_H
#define UI_H

#include <lvgl.h>

/** Индексы вкладок TabView. Settings скрыта в таббаре, открывается длинным BOOT. */
enum UiTabIndex {
    UI_TAB_FLOW = 0,
    UI_TAB_BATTERY = 1,
    UI_TAB_GRID = 2,
    UI_TAB_SOLAR = 3,
    UI_TAB_SETTINGS = 4
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

/** Получить TabView для проверки текущей вкладки (main loop). */
lv_obj_t* uiGetTabview();

/** Переключить вкладку. Таббар скрыт, поэтому заодно показывает/прячет кнопку возврата на Flow. */
void uiSetTab(UiTabIndex idx);

#endif
