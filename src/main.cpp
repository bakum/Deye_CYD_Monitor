/**
 * Deye CYD Monitor — точка входа.
 * Инициализация модулей и главный цикл (BOOT-кнопка, таймаут экрана, опрос инвертора).
 */
#include <Arduino.h>
#include "config.h"
#include "settings.h"  // SCREEN_TIMEOUT_MIN, lastTouchTime, settingsIsScreenOn, ...
#include "display.h"
#include "touch.h"
#include "ui.h"
#include "network.h"
#include "inverter.h"
#include <lvgl.h>

const int BOOT_BTN_PIN = 0;
const uint32_t BOOT_LONG_PRESS_MS = 800;
const uint32_t REQUEST_TIMEOUT_MS = 4000;
const uint32_t POLL_INTERVAL_MS = 5000;

static uint32_t bootPressStart = 0;
static bool bootLongPressTriggered = false;
static uint32_t lastTimeoutLog = 0;  // не спамить Serial при постоянных таймаутах
const uint32_t TIMEOUT_LOG_INTERVAL_MS = 60000;  // выводить "Modbus timeout" не чаще раза в минуту
const uint32_t STARTUP_TIMEOUT_LOG_INTERVAL_MS = 5000;  // на старте логировать чаще для диагностики

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    Serial.println("BOOT btn: GPIO0, hold ~0.8s -> Settings");

    displayInit();
    backlightInit();
    touchInit();
    lvglInit();
    touchRegisterLvglIndev();

    settingsLoad();
    backlightSet(settingsGetBrightness());

    uiBuild();
    uiSetStatusText("WiFi: connecting... (or connect to Deye_Monitor_ESP32_IoT)");
    for (int i = 0; i < 20; i++) { lv_timer_handler(); delay(50); }

    inverterSetCallbacks(uiShowLoader, uiUpdate);
    networkSetup(uiSetStatusText);
    settingsTouchReset();

    uiSetStatusText("WiFi Connected!");
}

void loop() {
    lv_timer_handler();

    // BOOT: долгое нажатие -> вкладка Settings
    bool bootPressed = (digitalRead(BOOT_BTN_PIN) == LOW);
    if (bootPressed) {
        if (bootPressStart == 0)
            bootPressStart = millis();
        else if (!bootLongPressTriggered && (millis() - bootPressStart > BOOT_LONG_PRESS_MS)) {
            bootLongPressTriggered = true;
            if (!settingsIsScreenOn()) {
                settingsSetScreenOn(true);
                settingsTouchReset();
                backlightSet(settingsGetBrightness());
                inverterSetLastUpdate(0);
            }
            lv_tabview_set_act(uiGetTabview(), 2, LV_ANIM_ON);
            Serial.println("BOOT: Settings");
        }
    } else {
        bootPressStart = 0;
        bootLongPressTriggered = false;
    }

    // Таймаут экрана: при отсутствии касаний N минут — гасим подсветку (экран «выключен»).
    // Пробуждение: касание обновляет lastTouchTime в touch.cpp и включает подсветку.
    if (SCREEN_TIMEOUT_MIN > 0 && settingsIsScreenOn()) {
        if (millis() - lastTouchTime > (uint32_t)(SCREEN_TIMEOUT_MIN * 60 * 1000)) {
            settingsSetScreenOn(false);
            backlightSet(0);
        }
    }

    inverterHandleResponse();
    networkCheckReconnect();

    if (inverterIsRequestSent() && (millis() - inverterGetRequestTimestamp() > REQUEST_TIMEOUT_MS)) {
        uiShowLoader(false);
        uint32_t logInterval = (inverterGetLastSuccessTimestamp() == 0)
                                   ? STARTUP_TIMEOUT_LOG_INTERVAL_MS
                                   : TIMEOUT_LOG_INTERVAL_MS;
        if (millis() - lastTimeoutLog >= logInterval) {
            Serial.printf("[%lu] Error: Modbus response timeout (no reply from inverter)\n", (unsigned long)millis());
            lastTimeoutLog = millis();
        }
        inverterStopClient();
        inverterSetLastUpdate(millis());
    }

    // Опрос Modbus только при включённом экране и не на вкладке Settings.
    if (settingsIsScreenOn() && lv_tabview_get_tab_act(uiGetTabview()) != 2 &&
        (millis() - inverterGetLastUpdate() > POLL_INTERVAL_MS)) {
        inverterSetLastUpdate(millis());
        inverterRequestData();
    }

    delay(5);
}
