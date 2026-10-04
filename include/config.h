#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- ПИНЫ ПЛАТЫ CYD ---
// Тачскрин XPT2046
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// RGB Светодиод на задней стороне платы (инвертированная логика: LOW = горит)
#define CYD_LED_RED 4
#define CYD_LED_GREEN 16
#define CYD_LED_BLUE 17

// Датчик освещенности
#define CYD_LDR 34

// --- НАСТРОЙКИ DEYE (Значения по умолчанию) ---
#define DEFAULT_INVERTER_IP   "192.168.1.100"
#define DEFAULT_INVERTER_PORT 8899
#define DEFAULT_INVERTER_SN   12345678
#define DEFAULT_TIMEZONE_HOUR 4
#define DEFAULT_DST_ENABLED   false

// Номинальная мощность инвертора, W — предел шкал-дуг на вкладке Flow (как 5.0 kW на панели Deye).
// Задаётся в портале WiFiManager, допустимо 1..50 kW.
#define DEFAULT_INVERTER_POWER_W 5000
#define INVERTER_POWER_MIN_W     1000
#define INVERTER_POWER_MAX_W     50000

    // --- КАРТА РЕГИСТРОВ ---
    // Блок 70..191: с 70 начинаются суточные счётчики (заряд батареи, сеть, нагрузка).
    // 122 регистра — в пределах 125 на запрос Modbus, ответ ~275 байт в буфере 512.
    const uint16_t REG_BLOCK_START = 70;
    const uint16_t REG_BLOCK_LEN   = 122;
    const uint16_t ADDR_DAY_GRID_BUY    = 76;   // куплено из сети за сутки, /10 kWh
    const uint16_t ADDR_DAY_LOAD_ENERGY = 84;   // потребление нагрузки за сутки, /10 kWh

    const uint16_t ADDR_DAY_PV_ENERGY   = 108;
    const uint16_t ADDR_PV1_VOLTAGE     = 109;
    const uint16_t ADDR_PV1_CURRENT     = 110;
    const uint16_t ADDR_PV2_VOLTAGE     = 111;
    const uint16_t ADDR_PV2_CURRENT     = 112;
    const uint16_t ADDR_GRID_POWER      = 169;
    const uint16_t ADDR_EXT_CT_POWER    = 172;  // сеть по внешнему CT (ввод дома), signed
    const uint16_t ADDR_GRID_VOLTAGE    = 150;
    const uint16_t ADDR_LOAD_POWER      = 178;
    const uint16_t ADDR_BATT_TEMP       = 182;
    const uint16_t ADDR_BATT_VOLTAGE    = 183;
    const uint16_t ADDR_BATT_SOC        = 184;
    const uint16_t ADDR_PV1_POWER       = 186;
    const uint16_t ADDR_PV2_POWER       = 187;
    const uint16_t ADDR_BATT_POWER      = 190;
    const uint16_t ADDR_BATT_CURRENT    = 191;

#endif