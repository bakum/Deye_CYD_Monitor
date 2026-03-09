#ifndef NETWORK_H
#define NETWORK_H

/** Тип функции для обновления текста статуса на экране (при ретраях Wi‑Fi). */
typedef void (*NetworkStatusTextFn)(const char* text);

/**
 * Настроить и запустить Wi‑Fi (WiFiManager с кастомными полями),
 * при успехе сохранить параметры через settings, запустить NTP.
 * statusTextCallback вызывается для вывода сообщений (например, "WiFi: retry 1/3...").
 * Возвращает true при успешном подключении.
 */
bool networkSetup(NetworkStatusTextFn statusTextCallback);

/** Периодическая проверка и переподключение при потере Wi‑Fi (вызывать из loop). */
void networkCheckReconnect();

/** Сброс сохранённых настроек Wi‑Fi (перед ESP.restart). */
void networkResetSettings();

/** Применить текущие TIMEZONE_HOUR и DST к NTP (после смены таймзоны в UI). */
void networkApplyTimeConfig();

#endif
