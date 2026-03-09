# Декомпозиция проекта Deye CYD Monitor

## 1. Назначение проекта

**Deye CYD Monitor** — прошивка для платы **ESP32-2432S028 (CYD)** с цветным дисплеем и тачскрином. Устройство подключается по Wi‑Fi к инвертору Deye (через Solarman V5 / Modbus RTU по TCP), читает данные батареи, сети и нагрузки и отображает их на экране в реальном времени.

---

## 2. Высокоуровневая архитектура

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Deye CYD Monitor                               │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │   Display   │  │   Input     │  │   Network   │  │   Inverter      │  │
│  │   (TFT+LVGL)│  │ (Touch+BOOT)│  │ (WiFi+TCP)  │  │ (Solarman/Modbus)│  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘  │
│         │                │                │                   │          │
│         └────────────────┴────────────────┴───────────────────┘          │
│                                    │                                      │
│                          ┌─────────▼─────────┐                            │
│                          │  Config / State   │                            │
│                          │  (Preferences +   │                            │
│                          │   globals)        │                            │
│                          └───────────────────┘                            │
└─────────────────────────────────────────────────────────────────────────┘
```

- **Display** — вывод на TFT (TFT_eSPI) через LVGL (экраны, виджеты, таймеры).
- **Input** — тачскрин (XPT2046) и кнопка BOOT (долгое нажатие → Settings).
- **Network** — Wi‑Fi (WiFiManager), TCP-клиент к инвертору, NTP, переподключение.
- **Inverter** — формирование запросов Solarman V5, разбор ответов, парсинг Modbus-регистров.
- **Config/State** — настройки в Preferences, глобальные переменные (IP, порт, SN, таймауты, данные инвертора).

---

## 3. Декомпозиция по функциональным блокам

### 3.1 Конфигурация и константы

| Что | Где | Описание |
|-----|-----|----------|
| Пины CYD, дефолты инвертора, карта регистров | `include/config.h` | Пины тача (XPT2046_*), LED, LDR; DEFAULT_INVERTER_IP/PORT/SN, TIMEZONE, DST; REG_BLOCK_*, ADDR_* для Modbus. |
| Флаги сборки TFT/LVGL/SPI | `platformio.ini` (build_flags) | ILI9341, размеры, пины TFT, подсветка, LV_CONF_*, шрифты. |

**Зависимости:** только Arduino.h. Остальной код зависит от config.h.

---

### 3.2 Железо и драйверы (Display + Touch + Backlight)

| Функция | Где в коде (main.cpp) | Описание |
|---------|----------------------|----------|
| TFT | Глобальный `TFT_eSPI tft`, `my_disp_flush()` | Инициализация в `setup()`, вывод буфера LVGL в `my_disp_flush`. |
| Touch | `SPIClass touchSpi`, `XPT2046_Touchscreen ts`, `my_touchpad_read()` | HSPI для тача, калибровка (map 200–3800 → 0–320/240), сброс таймера экрана, пробуждение экрана. |
| Подсветка | `ledcSetup`/`ledcAttachPin`/`ledcWrite`, `LCD_BRIGHTNESS` | PWM на пине 21 (TFT_BL), 0–100% через map(5, 255). |

**Входы:** LVGL (flush), события касания. **Выходы:** картинка на экране, события тача в LVGL, яркость.

---

### 3.3 LVGL: UI и логика интерфейса

| Функция | Где в коде | Описание |
|---------|------------|----------|
| Инициализация LVGL | `setup()` | `lv_init()`, draw_buf, disp_drv, indev_drv, `build_ui()`. |
| Построение экранов | `build_ui()` (~280 строк) | TabView: вкладки Battery, Grid/Home, Settings. Виджеты: arc (SOC), labels (V, A, W, temp), стрелки, слайдер яркости, dropdown (timeout, timezone), кнопки Reboot / Reset WiFi. Скрытие вкладки Settings в таббаре. |
| Обновление данных на экране | `update_ui()` | Заполнение виджетов из глобальных переменных (battSOC, battVolts, gridPower, loadPower и т.д.), цвета дуги/стрелок/температуры. |
| Статус-бар | `update_status_bar()`, таймер 1 с | Иконка батареи, SOC, заряд/разряд, время (NTP), уровень Wi‑Fi, SSID. |
| Обработчики настроек | `slider_event_cb`, `dropdown_timeout_event_cb`, lambda для timezone, `btn_reboot_event_cb`, `btn_reset_wifi_event_cb` | Яркость, таймаут экрана, часовой пояс, Reboot, сброс Wi‑Fi. |

**Входы:** глобальные переменные инвертора, время, статус Wi‑Fi. **Выходы:** отображение и сохранение настроек (Preferences при смене timeout/tz).

---

### 3.4 Настройки (Config) и хранение

| Что | Где | Описание |
|-----|-----|----------|
| Глобальные переменные настроек | Верх main.cpp | INVERTER_IP, INVERTER_PORT, INVERTER_SN, INVERTER_SLAVE_ID, TIMEZONE_HOUR, DST_ENABLED, SCREEN_TIMEOUT_MIN, LCD_BRIGHTNESS. |
| Состояние экрана/ввода | main.cpp | lastTouchTime, isScreenOn, bootPressStart, bootLongPressTriggered, shouldSaveConfig. |
| Хранение | `Preferences preferences`, namespace `"deye_config"` | Чтение в setup(): ip, port, sn, slave_id, tz_hour, dst, scr_timeout, lcd_bri. Запись при сохранении из WiFiManager и при смене timeout/tz в UI. |

**Зависимости:** от настроек зависят WiFiManager (custom parameters), NTP (configTime), опрос инвертора (IP/port/SN/slave_id), таймаут экрана и яркость.

---

### 3.5 Сеть: Wi‑Fi и TCP

| Функция | Где в коде | Описание |
|---------|------------|----------|
| Подключение Wi‑Fi | `setup()` | WiFiManager с таймаутами (90 с подключение, 180 с портал), до 3 циклов retry, кастомные поля (IP, port, SN, slave_id, tz, dst). При успехе — сохранение в Preferences при shouldSaveConfig. |
| TCP-клиент | `WiFiClient client` | Подключение к INVERTER_IP:INVERTER_PORT, отправка фреймов, приём ответов. |
| Переподключение | `loop()` | Каждые 10 с проверка WiFi.status(); при отключении — client.stop(), WiFi.reconnect(). |
| NTP | `setup()` после Wi‑Fi | configTime(TIMEZONE_HOUR * 3600, DST ? 3600 : 0, "pool.ntp.org", ...). |

**Зависимости:** Preferences для загрузки IP/port и т.д.; UI (label_status) для отображения статуса и ретраев.

---

### 3.6 Протокол инвертора (Solarman V5 + Modbus RTU)

| Функция | Где в коде | Описание |
|---------|------------|----------|
| CRC16 Modbus | `calculateCRC16()` | CRC для фрейма Modbus RTU (SlaveID, 0x03, Reg, Count). |
| Checksum V5 | `calculateChecksum()` | Сумма байт для обёртки Solarman V5. |
| Запрос | `requestInverterData(reg, count)` | Сборка Modbus RTU (8 байт) + обёртка V5 (A5, length, control, seq, SN, frame type, нули, Modbus, checksum, 0x15). Отправка через client.write(), выставляется isRequestSent, показ спиннера. |
| Чтение регистров из ответа | `getReg()`, `getRegSigned()` | Индекс по dataOffset и адресу регистра, big-endian 16 bit. |
| Разбор ответа | `handleInverterResponse()` | Чтение до 500 мс в буфер, поиск 0xA5, проверка checksum, поиск SlaveID+0x03 в пакете, извлечение данных в battSOC, battVolts, battTemp, battCurrent, battPower, gridPower, gridVolts, loadPower. Вызов update_ui(), сброс isRequestSent, скрытие спиннера. |

**Входы:** константы из config.h (REG_BLOCK_*, ADDR_*), INVERTER_SN, INVERTER_SLAVE_ID. **Выходы:** глобальные переменные данных инвертора и обновление UI.

---

### 3.7 Главный цикл и тайминги

| Действие | Где | Условие/период |
|----------|-----|----------------|
| LVGL | `loop()` | Каждый проход — lv_timer_handler(). |
| BOOT-кнопка | `loop()` | Долгое нажатие ~0.8 с → переход на вкладку Settings, при выключенном экране — включение. |
| Таймаут экрана | `loop()` | При SCREEN_TIMEOUT_MIN > 0 и отсутствии касаний lastTouchTime — выключение подсветки (isScreenOn = false). |
| Приём ответа инвертора | `loop()` | handleInverterResponse() при наличии данных в client. |
| Проверка Wi‑Fi | `loop()` | Каждые 10 с — переподключение при отключении. |
| Таймаут запроса | `loop()` | Если isRequestSent и прошло >4 с — сброс, client.stop(), lastUpdate = millis(). |
| Опрос инвертора | `loop()` | Каждые 5 с, только при isScreenOn и не на вкладке Settings — requestInverterData(REG_BLOCK_START, REG_BLOCK_LEN). |

---

## 4. Потоки данных

1. **Старт:** config.h + Preferences → загрузка настроек → Wi‑Fi → NTP → UI готов.
2. **Периодически:** запрос к инвертору (Solarman V5) → ответ → разбор Modbus → глобальные переменные → `update_ui()` и статус-бар.
3. **Пользователь:** тач (и BOOT) → LVGL → callbacks настроек → изменение переменных и Preferences, при необходимости Reboot/Reset WiFi.
4. **Экран:** lastTouchTime обновляется в my_touchpad_read; в loop() по таймауту гасится подсветка и при необходимости не идёт опрос инвертора.

---

## 5. Зависимости модулей (упрощённо)

```
config.h
   │
   ├──► Display/Touch/Backlight (пины, константы TFT в platformio)
   ├──► Inverter (ADDR_*, REG_*)
   ├──► Config/State (DEFAULT_*)
   └──► WiFi/Setup (DEFAULT_* для параметров)

Preferences ◄── WiFiManager (save), UI callbacks (timeout, tz, lcd_bri)
         │
         └──► Setup: загрузка IP, port, SN, slave_id, tz, dst, scr_timeout, lcd_bri

Inverter (request/response) ──► глобальные batt*/grid*/load* ──► update_ui(), update_status_bar()
```

---

## 6. Декомпозиция по файлам (применена)

Реализованы следующие модули:

| Модуль | Файлы | Содержимое |
|--------|-------|------------|
| **Config** | `include/config.h` (оставить), опционально `src/config.cpp` | Константы, пины, дефолты, карта регистров. |
| **Display** | `src/display.cpp`, `include/display.h` | Инициализация TFT, my_disp_flush, инициализация LVGL (draw_buf, disp_drv). |
| **Touch** | `src/touch.cpp`, `include/touch.h` | Инициализация SPI/XPT2046, my_touchpad_read, калибровка. |
| **Backlight** | можно в `display` или отдельно `src/backlight.cpp` | ledcSetup/Attach/Write, установка яркости по процентам. |
| **UI** | `src/ui.cpp`, `include/ui.h` | build_ui(), update_ui(), update_status_bar(), все LVGL-виджеты и стили, callbacks (slider, dropdown, reboot, reset wifi). |
| **Settings** | `src/settings.cpp`, `include/settings.h` | Загрузка/сохранение Preferences, глобальные переменные настроек (или доступ через get/set). |
| **Network** | `src/network.cpp`, `include/network.h` | WiFiManager (autoConnect, custom params), сохранение при shouldSaveConfig, NTP (configTime), проверка/переподключение Wi‑Fi. |
| **Inverter** | `src/inverter.cpp`, `include/inverter.h` | calculateCRC16, calculateChecksum, requestInverterData, getReg/getRegSigned, handleInverterResponse, константы REG_/ADDR_ (или оставить в config.h). |
| **Main** | `src/main.cpp` | setup() (последовательность init: display, touch, LVGL, build_ui, load settings, network, NTP), loop() (lv_timer_handler, BOOT, screen timeout, inverterHandleResponse, networkCheckReconnect, request timeout, inverterRequestData). |

**Структура файлов после рефакторинга:**
- `include/config.h` — пины, дефолты, карта регистров (без изменений).
- `include/settings.h`, `src/settings.cpp` — настройки и NVS.
- `include/display.h`, `src/display.cpp` — TFT, LVGL init, подсветка.
- `include/touch.h`, `src/touch.cpp` — XPT2046, драйвер ввода LVGL.
- `include/inverter.h`, `src/inverter.cpp` — Solarman V5, Modbus, данные инвертора.
- `include/network.h`, `src/network.cpp` — Wi‑Fi, NTP, переподключение.
- `include/ui.h`, `src/ui.cpp` — экраны LVGL, обновление, коллбэки.
- `src/main.cpp` — только setup/loop и BOOT-кнопка.

Такой разрез даёт:

- Чёткое разделение: железо (display/touch/backlight), протокол (inverter), сеть (network), настройки (settings), интерфейс (ui).
- Возможность тестировать inverter/network отдельно (например, на десктопе с заглушками дисплея).
- Единую точку входа в main.cpp и явные зависимости через заголовки.

---

## 7. Краткое резюме

- **После рефакторинга** логика разнесена по модулям; в `main.cpp` остаются только инициализация и loop (~100 строк).
- **Модули:** `settings`, `display`, `touch`, `inverter`, `network`, `ui` (см. раздел 6). Зависимости: main → все модули; ui → settings, inverter, display, network; network → settings, inverter; inverter → settings, config.
- **Рефакторинг применён:** см. структуру файлов в корне и в `src/`, `include/`.
