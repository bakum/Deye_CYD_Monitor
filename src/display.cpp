#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

static TFT_eSPI tft;

static void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void displayInit() {
    tft.begin();
    tft.setRotation(1);
}

void lvglInit() {
    lv_init();
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[240 * 10];
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

void backlightInit() {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
}

void backlightSet(int percent) {
    if (percent <= 0) {
        ledcWrite(0, 0);  // полное выключение подсветки (таймаут экрана)
        return;
    }
    if (percent > 100) percent = 100;
    ledcWrite(0, map(percent, 0, 100, 5, 255));  // минимум 5 при включённом экране
}
