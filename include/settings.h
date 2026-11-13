#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// --- Настройки инвертора (extern — доступ из network, inverter) ---
extern char INVERTER_IP[16];
extern int INVERTER_PORT;
extern uint32_t INVERTER_SN;
extern uint8_t INVERTER_SLAVE_ID;
extern int TIMEZONE_HOUR;
extern bool DST_ENABLED;
extern uint32_t INVERTER_POWER_W;   // номинальная мощность, W (предел шкал Flow)

// --- Настройки экрана и состояние ---
extern int SCREEN_TIMEOUT_MIN;
extern int LCD_BRIGHTNESS;
extern uint32_t lastTouchTime;
extern bool isScreenOn;
extern bool shouldSaveConfig;
extern int NIGHT_FROM_HOUR;   // начало тихих часов 0..23, -1 — выключены
extern int NIGHT_TO_HOUR;     // конец тихих часов 0..23 (не включительно)

/** Загрузить настройки из NVS (Preferences). Вызывать до WiFi. */
void settingsLoad();

/** Сохранить параметры после успешного сохранения в портале WiFiManager. */
void settingsSaveAfterWifi(const char* ip, int port, uint32_t sn, uint8_t slaveId, int tz, bool dst,
                           uint32_t powerW);

/** Сохранить таймаут экрана в NVS (вызывается из UI). */
void settingsSaveScreenTimeout(int min);

/** Сохранить часовой пояс в NVS и применить (вызывается из UI). */
void settingsSaveTimezone(int tzHour);

/** Сохранить тихие часы в NVS (вызывается из UI). fromHour = -1 — выключены. */
void settingsSaveNightHours(int fromHour, int toHour);

/** Сбросить таймер последнего касания (touch driver). */
void settingsTouchReset();

void settingsSetScreenOn(bool on);
bool settingsIsScreenOn();
void settingsSetBrightness(int pct);
int settingsGetBrightness();

#endif
