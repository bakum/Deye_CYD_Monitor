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
const uint32_t SLEEP_POLL_INTERVAL_MS = 30000;  // экран погашен: редкий опрос, только чтобы следить за сетью

// Наличие внешней сети по последнему успешному ответу. UNKNOWN — ответов ещё не было.
enum GridState { GRID_UNKNOWN, GRID_ON, GRID_OFF };
static GridState gridState = GRID_UNKNOWN;
static uint32_t gridCheckedSuccessTs = 0;  // какой успешный ответ уже учтён

// Тревога: смена состояния сети, пока экран погашен. Экран формально остаётся «выключенным»
// (settingsIsScreenOn() == false), горит только подсветка на минимуме — поэтому касание
// идёт по обычному пути пробуждения в touch.cpp: обычная яркость, касание не уходит в LVGL.
enum AlarmMode { ALARM_NONE, ALARM_GRID_LOST, ALARM_GRID_RESTORED };
static AlarmMode alarmMode = ALARM_NONE;
static uint32_t alarmStart = 0;
const int ALARM_BRIGHTNESS_SHARE_PCT = 20;           // подсветка тревоги — 20% от рабочей яркости
const uint32_t ALARM_RESTORED_SHOW_MS = 2 * 60000;   // «сеть вернулась» гаснет сама через 2 мин

/** Включить тревогу: минимальная подсветка и вкладка Flow. Повторная смена сети перезапускает её. */
static void alarmStartMode(AlarmMode mode) {
    alarmMode = mode;
    alarmStart = millis();
    int pct = settingsGetBrightness() * ALARM_BRIGHTNESS_SHARE_PCT / 100;
    if (pct < 1) pct = 1;  // 0 в backlightSet — полное выключение
    backlightSet(pct);
    uiSetTab(UI_TAB_FLOW);
    Serial.printf("[%lu] Alarm: %s, backlight %d%%\n", (unsigned long)millis(),
                  mode == ALARM_GRID_LOST ? "grid lost" : "grid restored", pct);
}

/** Сопровождение тревоги в loop(): касание/BOOT — тревога увидена; «сеть вернулась» — гасим по времени. */
static void alarmCheck() {
    if (alarmMode == ALARM_NONE) return;
    if (settingsIsScreenOn()) {
        // Разбудили касанием или BOOT: обычный рабочий режим, яркость уже выставил touch/BOOT.
        alarmMode = ALARM_NONE;
        Serial.printf("[%lu] Alarm: acknowledged\n", (unsigned long)millis());
        return;
    }
    if (alarmMode == ALARM_GRID_RESTORED && millis() - alarmStart > ALARM_RESTORED_SHOW_MS) {
        alarmMode = ALARM_NONE;
        backlightSet(0);
        Serial.printf("[%lu] Alarm: done, backlight off\n", (unsigned long)millis());
    }
}

static uint32_t bootPressStart = 0;
static bool bootLongPressTriggered = false;
static uint32_t lastTimeoutLog = 0;  // не спамить Serial при постоянных таймаутах
const uint32_t TIMEOUT_LOG_INTERVAL_MS = 60000;  // выводить "Modbus timeout" не чаще раза в минуту
const uint32_t STARTUP_TIMEOUT_LOG_INTERVAL_MS = 5000;  // на старте логировать чаще для диагностики

/**
 * Следит за сетью по каждому новому успешному ответу инвертора (и при погашенном экране).
 * Таймаут и битый кадр не обновляют lastSuccessTimestamp, поэтому состояние не меняют:
 * молчание инвертора не значит, что света нет.
 */
static void gridCheckState() {
    uint32_t ts = inverterGetLastSuccessTimestamp();
    if (ts == 0 || ts == gridCheckedSuccessTs) return;
    gridCheckedSuccessTs = ts;

    float v = inverterGetGridVolts();
    GridState now = (v < GRID_OFF_VOLTS) ? GRID_OFF : GRID_ON;
    if (now == gridState) return;

    if (gridState == GRID_UNKNOWN) {
        // Первый ответ после загрузки только запоминает состояние, это не событие.
        Serial.printf("[%lu] Grid: initial %s (%.1fV)\n", (unsigned long)millis(),
                      now == GRID_ON ? "ON" : "OFF", v);
    } else {
        Serial.printf("[%lu] Grid: %s (%.1fV), screen %s\n", (unsigned long)millis(),
                      now == GRID_ON ? "RESTORED" : "LOST", v, settingsIsScreenOn() ? "on" : "off");
        // При включённом экране хватает вышки на Flow; тревога — только если экран погашен
        // (или уже идёт тревога: тогда она перезапускается с новым состоянием).
        if (!settingsIsScreenOn())
            alarmStartMode(now == GRID_OFF ? ALARM_GRID_LOST : ALARM_GRID_RESTORED);
    }
    gridState = now;
}

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
    // Коротко: строка статуса ~266 px, имя точки доступа должно влезть целиком.
    uiSetStatusText("WiFi AP: Deye_Monitor_ESP32_IoT");
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
            uiSetTab(UI_TAB_SETTINGS);
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
    gridCheckState();
    alarmCheck();
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

    // Опрос Modbus не на вкладке Settings. Экран включён или идёт тревога — раз в 5 с;
    // погашен — раз в 30 с и только основной блок (нужно напряжение сети для gridCheckState).
    const bool fastPoll = settingsIsScreenOn() || alarmMode != ALARM_NONE;
    const uint32_t pollInterval = fastPoll ? POLL_INTERVAL_MS : SLEEP_POLL_INTERVAL_MS;
    if (lv_tabview_get_tab_act(uiGetTabview()) != UI_TAB_SETTINGS &&
        (millis() - inverterGetLastUpdate() > pollInterval)) {
        inverterSetLastUpdate(millis());
        inverterRequestData(!fastPoll);
    }

    delay(5);
}
