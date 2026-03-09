#ifndef DISPLAY_H
#define DISPLAY_H

/** Инициализация TFT (begin, setRotation). Вызвать до lvglInit. */
void displayInit();

/** Инициализация LVGL: lv_init, draw_buf, disp_drv, регистрация my_disp_flush. */
void lvglInit();

/** Инициализация подсветки (PWM на TFT_BL). Вызвать после displayInit. */
void backlightInit();

/** Установить яркость 0..100. */
void backlightSet(int percent);

#endif
