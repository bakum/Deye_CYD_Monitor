#ifndef UI_H
#define UI_H

#include <lvgl.h>

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
