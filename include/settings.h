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

// --- Настройки экрана и состояние ---
extern int SCREEN_TIMEOUT_MIN;
extern int LCD_BRIGHTNESS;
extern uint32_t lastTouchTime;
extern bool isScreenOn;
extern bool shouldSaveConfig;

/** Загрузить настройки из NVS (Preferences). Вызывать до WiFi. */
void settingsLoad();

/** Сохранить параметры после успешного сохранения в портале WiFiManager. */
void settingsSaveAfterWifi(const char* ip, int port, uint32_t sn, uint8_t slaveId, int tz, bool dst);

/** Сохранить таймаут экрана в NVS (вызывается из UI). */
void settingsSaveScreenTimeout(int min);

/** Сохранить часовой пояс в NVS и применить (вызывается из UI). */
void settingsSaveTimezone(int tzHour);

/** Сбросить таймер последнего касания (touch driver). */
void settingsTouchReset();

void settingsSetScreenOn(bool on);
bool settingsIsScreenOn();
void settingsSetBrightness(int pct);
int settingsGetBrightness();

#endif
