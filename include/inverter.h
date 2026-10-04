#ifndef INVERTER_H
#define INVERTER_H

#include <Arduino.h>
#include <WiFiClient.h>

/** Колбэк: показать/скрыть индикатор загрузки. */
typedef void (*InverterShowLoaderFn)(bool show);

/** Колбэк: обновить UI после получения данных. */
typedef void (*InverterUpdateUiFn)(void);

/** Установить колбэки (вызвать из main после создания UI). */
void inverterSetCallbacks(InverterShowLoaderFn showLoader, InverterUpdateUiFn updateUi);

/** Отправить запрос к инвертору (Solarman V5 + Modbus). */
void inverterRequestData();

/** Обработать входящие данные; при успешном разборе вызывает updateUi и showLoader(false). */
void inverterHandleResponse();

/** Закрыть TCP-соединение (при таймауте или переподключении Wi‑Fi). */
void inverterStopClient();

bool inverterIsRequestSent();
uint32_t inverterGetRequestTimestamp();
uint32_t inverterGetLastUpdate();
void inverterSetLastUpdate(uint32_t t);

/** Время последнего успешного разбора ответа (millis). 0 = ещё не было успешных ответов. */
uint32_t inverterGetLastSuccessTimestamp();

// --- Геттеры данных инвертора для UI ---
uint16_t inverterGetBattSOC();
float inverterGetBattVolts();
float inverterGetBattTemp();
float inverterGetBattCurrent();
int16_t inverterGetBattPower();
int16_t inverterGetGridPower();
/** Нагрузка на стороне сети (до инвертора), W: внешний CT − внутренний, не меньше 0. */
uint16_t inverterGetHomePower();
float inverterGetGridVolts();
uint16_t inverterGetLoadPower();
float inverterGetPv1Volts();
float inverterGetPv1Current();
uint16_t inverterGetPv1Power();
float inverterGetPv2Volts();
float inverterGetPv2Current();
uint16_t inverterGetPv2Power();
uint32_t inverterGetPvTotalPower();
float inverterGetDayPvEnergy();
/** Куплено из сети за сутки, kWh (регистр 76). */
float inverterGetDayGridBuy();
/** Потребление нагрузки за сутки, kWh (регистр 84). */
float inverterGetDayLoadEnergy();

#endif
