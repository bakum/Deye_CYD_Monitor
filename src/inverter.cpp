#include "inverter.h"
#include "config.h"
#include "settings.h"
#include <WiFiClient.h>

static WiFiClient client;
static uint8_t req_seq = 0;
static uint32_t lastUpdate = 0;
static bool isRequestSent = false;
static uint32_t requestTimestamp = 0;
static uint32_t lastSuccessTimestamp = 0;

static InverterShowLoaderFn s_showLoader = nullptr;
static InverterUpdateUiFn s_updateUi = nullptr;

// Данные инвертора
static uint16_t battSOC = 0;
static float battVolts = 0.0f;
static float battTemp = 0.0f;
static float battCurrent = 0.0f;
static int16_t battPower = 0;
static int16_t gridPower = 0;
static float gridVolts = 0.0f;
static uint16_t loadPower = 0;

static uint8_t calculateChecksum(uint8_t* buf, int len) {
    uint8_t checksum = 0;
    for (int i = 1; i < len - 2; i++) checksum += buf[i];
    return checksum;
}

static uint16_t calculateCRC16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint16_t getReg(uint8_t* buffer, int start_offset, int reg_addr) {
    int index = start_offset + (reg_addr - REG_BLOCK_START) * 2;
    return (uint16_t)((buffer[index] << 8) | buffer[index + 1]);
}

static int16_t getRegSigned(uint8_t* buffer, int start_offset, int reg_addr) {
    return (int16_t)getReg(buffer, start_offset, reg_addr);
}

void inverterSetCallbacks(InverterShowLoaderFn showLoader, InverterUpdateUiFn updateUi) {
    s_showLoader = showLoader;
    s_updateUi = updateUi;
}

void inverterRequestData() {
    if (!client.connected()) {
        Serial.println("Connecting to inverter...");
        if (!client.connect(INVERTER_IP, INVERTER_PORT)) {
            Serial.println("Connection failed");
            return;
        }
        delay(200);
        while (client.available()) { client.read(); delay(1); }
    }

    req_seq++;

    uint8_t modbus_frame[8];
    modbus_frame[0] = INVERTER_SLAVE_ID;
    modbus_frame[1] = 0x03;
    modbus_frame[2] = (uint8_t)(REG_BLOCK_START >> 8);
    modbus_frame[3] = (uint8_t)(REG_BLOCK_START & 0xFF);
    modbus_frame[4] = (uint8_t)(REG_BLOCK_LEN >> 8);
    modbus_frame[5] = (uint8_t)(REG_BLOCK_LEN & 0xFF);
    uint16_t crc = calculateCRC16(modbus_frame, 6);
    modbus_frame[6] = (uint8_t)(crc & 0xFF);
    modbus_frame[7] = (uint8_t)(crc >> 8);

    const uint8_t MODBUS_FRAME_LEN = 8;
    uint16_t v5_length = 15 + MODBUS_FRAME_LEN;

    uint8_t frame[64];
    int idx = 0;
    frame[idx++] = 0xA5;
    frame[idx++] = (uint8_t)(v5_length & 0xFF);
    frame[idx++] = (uint8_t)(v5_length >> 8);
    frame[idx++] = 0x10;
    frame[idx++] = 0x45;
    frame[idx++] = (uint8_t)(req_seq & 0xFF);
    frame[idx++] = (uint8_t)((req_seq >> 8) & 0xFF);
    frame[idx++] = (uint8_t)(INVERTER_SN & 0xFF);
    frame[idx++] = (uint8_t)((INVERTER_SN >> 8) & 0xFF);
    frame[idx++] = (uint8_t)((INVERTER_SN >> 16) & 0xFF);
    frame[idx++] = (uint8_t)((INVERTER_SN >> 24) & 0xFF);
    frame[idx++] = 0x02;
    for (int i = 0; i < 14; i++) frame[idx++] = 0x00;
    memcpy(&frame[idx], modbus_frame, MODBUS_FRAME_LEN);
    idx += MODBUS_FRAME_LEN;
    frame[idx++] = 0x00;
    frame[idx++] = 0x15;

    uint8_t chk = 0;
    for (int i = 1; i < idx - 2; i++) chk += frame[i];
    frame[idx - 2] = chk;

    Serial.printf("TX V5 (%d bytes): ", idx);
    for (int i = 0; i < idx; i++) Serial.printf("%02X ", frame[i]);
    Serial.println();

    client.write(frame, idx);
    isRequestSent = true;
    requestTimestamp = millis();
    if (s_showLoader) s_showLoader(true);
}

void inverterHandleResponse() {
    if (!client.connected() || !client.available()) return;

    uint8_t buffer[512];
    int len = 0;
    uint32_t t = millis();
    while ((millis() - t < 500) && len < (int)sizeof(buffer)) {
        if (client.available()) {
            buffer[len++] = client.read();
            t = millis();
        } else {
            delay(10);
        }
    }

    if (len < 20) {
        Serial.printf("Response too short: %d bytes\n", len);
        client.stop();
        isRequestSent = false;
        if (s_showLoader) s_showLoader(false);
        return;
    }

    if (buffer[0] != 0xA5) {
        int startIdx = -1;
        for (int i = 0; i < len; i++) {
            if (buffer[i] == 0xA5) { startIdx = i; break; }
        }
        if (startIdx > 0) {
            for (int i = 0; i < len - startIdx; i++) buffer[i] = buffer[i + startIdx];
            len -= startIdx;
        } else {
            client.stop();
            isRequestSent = false;
            if (s_showLoader) s_showLoader(false);
            return;
        }
    }

    if (buffer[len - 2] != calculateChecksum(buffer, len)) {
        Serial.println("Checksum Error");
        client.stop();
        isRequestSent = false;
        if (s_showLoader) s_showLoader(false);
        return;
    }

    int dataOffset = -1;
    for (int i = 0; i < len - 2; i++) {
        if (buffer[i] == INVERTER_SLAVE_ID && buffer[i + 1] == 0x03) {
            if (buffer[i + 2] > 0 && buffer[i + 2] < 250) {
                dataOffset = i + 3;
                break;
            }
        }
    }
    if (dataOffset == -1) {
        Serial.println("Error: Modbus header not found in response!");
        client.stop();
        isRequestSent = false;
        if (s_showLoader) s_showLoader(false);
        return;
    }

    battSOC = getReg(buffer, dataOffset, ADDR_BATT_SOC);
    battVolts = getReg(buffer, dataOffset, ADDR_BATT_VOLTAGE) / 100.0f;
    battTemp = (getReg(buffer, dataOffset, ADDR_BATT_TEMP) - 1000) / 10.0f;
    battPower = getRegSigned(buffer, dataOffset, ADDR_BATT_POWER);
    battCurrent = getRegSigned(buffer, dataOffset, ADDR_BATT_CURRENT) / 100.0f;
    gridPower = getRegSigned(buffer, dataOffset, ADDR_GRID_POWER);
    gridVolts = getReg(buffer, dataOffset, ADDR_GRID_VOLTAGE) / 10.0f;
    loadPower = getReg(buffer, dataOffset, ADDR_LOAD_POWER);

    lastSuccessTimestamp = millis();
    Serial.printf("[%lu] Data: SOC=%d%%, V=%.2f, A=%.2f, P=%dW, T=%.1fC, Grid=%dW, Load=%dW\n",
        (unsigned long)millis(), battSOC, battVolts, battCurrent, battPower, battTemp, gridPower, loadPower);

    isRequestSent = false;
    if (s_showLoader) s_showLoader(false);
    if (s_updateUi) s_updateUi();
}

void inverterStopClient() {
    client.stop();
    isRequestSent = false;  // иначе после таймаута опрос не возобновится (main каждый раз обновляет lastUpdate)
}

bool inverterIsRequestSent() { return isRequestSent; }
uint32_t inverterGetRequestTimestamp() { return requestTimestamp; }
uint32_t inverterGetLastUpdate() { return lastUpdate; }
void inverterSetLastUpdate(uint32_t t) { lastUpdate = t; }
uint32_t inverterGetLastSuccessTimestamp() { return lastSuccessTimestamp; }

uint16_t inverterGetBattSOC() { return battSOC; }
float inverterGetBattVolts() { return battVolts; }
float inverterGetBattTemp() { return battTemp; }
float inverterGetBattCurrent() { return battCurrent; }
int16_t inverterGetBattPower() { return battPower; }
int16_t inverterGetGridPower() { return gridPower; }
float inverterGetGridVolts() { return gridVolts; }
uint16_t inverterGetLoadPower() { return loadPower; }
