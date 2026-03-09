#include "touch.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "inverter.h"
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

static SPIClass touchSpi(HSPI);
static XPT2046_Touchscreen ts(XPT2046_CS);

static void my_touchpad_read(lv_indev_drv_t* indev_driver, lv_indev_data_t* data) {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();

        if (p.z < 100 || p.x == 0 || p.y == 0 || p.x > 4000 || p.y > 4000) {
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        settingsTouchReset();

        if (!settingsIsScreenOn()) {
            settingsSetScreenOn(true);
            backlightSet(settingsGetBrightness());
            inverterSetLastUpdate(0);
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        int16_t x_raw = p.x;
        int16_t y_raw = p.y;
        data->point.x = map(x_raw, 200, 3800, 0, 320);
        data->point.y = map(y_raw, 240, 3800, 0, 240);

        if (data->point.x < 0) data->point.x = 0;
        if (data->point.x > 319) data->point.x = 319;
        if (data->point.y < 0) data->point.y = 0;
        if (data->point.y > 239) data->point.y = 239;

        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void touchInit() {
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSpi);
    ts.setRotation(1);
}

void touchRegisterLvglIndev() {
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
}
