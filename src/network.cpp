#include "network.h"
#include "config.h"
#include "settings.h"
#include "inverter.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <lvgl.h>

static const uint32_t WIFI_CONNECT_TIMEOUT_SEC = 90;
static const uint32_t WIFI_PORTAL_TIMEOUT_SEC = 180;
static const int WIFI_MAX_RETRY_LOOPS = 3;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;

static uint32_t lastWifiCheck = 0;
static NetworkStatusTextFn s_statusTextFn = nullptr;
static WiFiManager* s_wm = nullptr;

static void saveConfigCallback() {
    shouldSaveConfig = true;
}

bool networkSetup(NetworkStatusTextFn statusTextCallback) {
    s_statusTextFn = statusTextCallback;
    static WiFiManager wm;
    s_wm = &wm;
    char portStr[8], snStr[16], slaveIdStr[4], tzStr[8], dstStr[4];
    sprintf(portStr, "%d", INVERTER_PORT);
    sprintf(snStr, "%u", INVERTER_SN);
    sprintf(slaveIdStr, "%d", INVERTER_SLAVE_ID);
    sprintf(tzStr, "%d", TIMEZONE_HOUR);
    sprintf(dstStr, "%d", DST_ENABLED ? 1 : 0);
    // Мощность в kW: целое без дроби (5, 10), иначе с одним знаком (3.6)
    char powerStr[8];
    if (INVERTER_POWER_W % 1000 == 0) sprintf(powerStr, "%u", (unsigned)(INVERTER_POWER_W / 1000));
    else sprintf(powerStr, "%.1f", INVERTER_POWER_W / 1000.0f);

    WiFiManagerParameter custom_inverter_ip("inverter_ip", "Inverter IP", INVERTER_IP, 16);
    WiFiManagerParameter custom_inverter_port("inverter_port", "Inverter Port", portStr, 6);
    WiFiManagerParameter custom_inverter_sn("inverter_sn", "Inverter SN", snStr, 12);
    WiFiManagerParameter custom_slave_id("slave_id", "Modbus Slave ID", slaveIdStr, 3);
    WiFiManagerParameter custom_tz("tz_hour", "Timezone (Offset from UTC)", tzStr, 4);
    WiFiManagerParameter custom_dst("dst", "DST Enabled (1=Yes, 0=No)", dstStr, 2);
    WiFiManagerParameter custom_power("inv_power", "Inverter rated power, kW (e.g. 3.6, 5, 10)", powerStr, 6);

    s_wm->addParameter(&custom_inverter_ip);
    s_wm->addParameter(&custom_inverter_port);
    s_wm->addParameter(&custom_inverter_sn);
    s_wm->addParameter(&custom_slave_id);
    s_wm->addParameter(&custom_tz);
    s_wm->addParameter(&custom_dst);
    s_wm->addParameter(&custom_power);
    s_wm->setSaveParamsCallback(saveConfigCallback);
    s_wm->setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
    s_wm->setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
    WiFi.setSleep(false);

    int wifiRetries = 0;
    while (wifiRetries < WIFI_MAX_RETRY_LOOPS) {
        if (s_wm->autoConnect("Deye_Monitor_ESP32_IoT")) break;
        wifiRetries++;
        Serial.printf("WiFi: no connection after portal timeout, retry %d/%d\n", wifiRetries, WIFI_MAX_RETRY_LOOPS);
        if (s_statusTextFn) {
            char retryMsg[48];
            snprintf(retryMsg, sizeof(retryMsg), "WiFi: retry %d/%d, connecting...", wifiRetries, WIFI_MAX_RETRY_LOOPS);
            s_statusTextFn(retryMsg);
        }
        for (int i = 0; i < 30; i++) { lv_timer_handler(); delay(50); }
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (s_statusTextFn) s_statusTextFn("WiFi failed. Restarting...");
        for (int i = 0; i < 40; i++) { lv_timer_handler(); delay(50); }
        Serial.println("WiFi: all retries failed, restarting...");
        ESP.restart();
    }

    if (shouldSaveConfig) {
        strncpy(INVERTER_IP, custom_inverter_ip.getValue(), 15);
        INVERTER_IP[15] = '\0';
        int port = atoi(custom_inverter_port.getValue());
        uint32_t sn = strtoul(custom_inverter_sn.getValue(), NULL, 10);
        int new_id = atoi(custom_slave_id.getValue());
        uint8_t slaveId = (new_id > 0 && new_id < 255) ? (uint8_t)new_id : INVERTER_SLAVE_ID;
        int tz = atoi(custom_tz.getValue());
        bool dst = atoi(custom_dst.getValue()) == 1;
        // Вне 1..50 kW или нечитаемое значение — оставляем прежнюю мощность.
        uint32_t powerW = (uint32_t)(atof(custom_power.getValue()) * 1000.0f + 0.5f);
        if (powerW < INVERTER_POWER_MIN_W || powerW > INVERTER_POWER_MAX_W) powerW = INVERTER_POWER_W;
        settingsSaveAfterWifi(INVERTER_IP, port, sn, slaveId, tz, dst, powerW);
    }

    configTime(TIMEZONE_HOUR * 3600, DST_ENABLED ? 3600 : 0, "pool.ntp.org", "time.nist.gov", "time.windows.com");
    lastWifiCheck = millis();
    return true;
}

void networkResetSettings() {
    if (s_wm) s_wm->resetSettings();
}

void networkApplyTimeConfig() {
    configTime(TIMEZONE_HOUR * 3600, DST_ENABLED ? 3600 : 0, "pool.ntp.org", "time.nist.gov", "time.windows.com");
}

void networkCheckReconnect() {
    if (millis() - lastWifiCheck < WIFI_RECONNECT_INTERVAL_MS) return;
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
        inverterStopClient();
        WiFi.reconnect();
    }
}
