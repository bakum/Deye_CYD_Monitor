#ifndef TOUCH_H
#define TOUCH_H

/** Инициализация SPI и XPT2046. Вызвать до lvglInit. */
void touchInit();

/** Зарегистрировать драйвер тачскрина в LVGL (read_cb). Вызвать после lvglInit. */
void touchRegisterLvglIndev();

#endif
