#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences preferences;

// --- Определения глобальных переменных настроек ---
char INVERTER_IP[16] = DEFAULT_INVERTER_IP;
int INVERTER_PORT = DEFAULT_INVERTER_PORT;
uint32_t INVERTER_SN = DEFAULT_INVERTER_SN;
uint8_t INVERTER_SLAVE_ID = 1;
int TIMEZONE_HOUR = DEFAULT_TIMEZONE_HOUR;
bool DST_ENABLED = DEFAULT_DST_ENABLED;
uint32_t INVERTER_POWER_W = DEFAULT_INVERTER_POWER_W;

int SCREEN_TIMEOUT_MIN = 10;
int LCD_BRIGHTNESS = 100;
uint32_t lastTouchTime = 0;
bool isScreenOn = true;
bool shouldSaveConfig = false;

void settingsLoad() {
    preferences.begin("deye_config", false);

    String p_ip = preferences.getString("ip", DEFAULT_INVERTER_IP);
    int p_port = preferences.getInt("port", DEFAULT_INVERTER_PORT);
    uint32_t p_sn = preferences.getUInt("sn", DEFAULT_INVERTER_SN);
    INVERTER_SLAVE_ID = preferences.getUChar("slave_id", 1);
    TIMEZONE_HOUR = preferences.getInt("tz_hour", DEFAULT_TIMEZONE_HOUR);
    DST_ENABLED = preferences.getBool("dst", DEFAULT_DST_ENABLED);
    INVERTER_POWER_W = preferences.getUInt("inv_power", DEFAULT_INVERTER_POWER_W);
    if (INVERTER_POWER_W < INVERTER_POWER_MIN_W || INVERTER_POWER_W > INVERTER_POWER_MAX_W)
        INVERTER_POWER_W = DEFAULT_INVERTER_POWER_W;
    SCREEN_TIMEOUT_MIN = preferences.getInt("scr_timeout", 10);
    LCD_BRIGHTNESS = preferences.getInt("lcd_bri", 100);

    p_ip.toCharArray(INVERTER_IP, 16);
    INVERTER_PORT = p_port;
    INVERTER_SN = p_sn;

    preferences.end();
}

void settingsSaveAfterWifi(const char* ip, int port, uint32_t sn, uint8_t slaveId, int tz, bool dst,
                           uint32_t powerW) {
    preferences.begin("deye_config", false);
    strncpy(INVERTER_IP, ip, 15);
    INVERTER_IP[15] = '\0';
    INVERTER_PORT = port;
    INVERTER_SN = sn;
    INVERTER_SLAVE_ID = slaveId;
    TIMEZONE_HOUR = tz;
    DST_ENABLED = dst;
    INVERTER_POWER_W = powerW;

    preferences.putString("ip", INVERTER_IP);
    preferences.putInt("port", INVERTER_PORT);
    preferences.putUInt("sn", INVERTER_SN);
    preferences.putUChar("slave_id", INVERTER_SLAVE_ID);
    preferences.putInt("tz_hour", TIMEZONE_HOUR);
    preferences.putBool("dst", DST_ENABLED);
    preferences.putUInt("inv_power", INVERTER_POWER_W);
    preferences.putInt("lcd_bri", LCD_BRIGHTNESS);
    preferences.end();
}

void settingsSaveScreenTimeout(int min) {
    SCREEN_TIMEOUT_MIN = min;
    preferences.begin("deye_config", false);
    preferences.putInt("scr_timeout", SCREEN_TIMEOUT_MIN);
    preferences.end();
}

void settingsSaveTimezone(int tzHour) {
    TIMEZONE_HOUR = tzHour;
    preferences.begin("deye_config", false);
    preferences.putInt("tz_hour", TIMEZONE_HOUR);
    preferences.end();
}

void settingsTouchReset() {
    lastTouchTime = millis();
}

void settingsSetScreenOn(bool on) {
    isScreenOn = on;
}

bool settingsIsScreenOn() {
    return isScreenOn;
}

void settingsSetBrightness(int pct) {
    LCD_BRIGHTNESS = pct;
}

int settingsGetBrightness() {
    return LCD_BRIGHTNESS;
}
