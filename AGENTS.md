# Deye CYD Monitor — правила для AI-агентов

Общий файл для Cursor и Claude Code (`CLAUDE.md` импортирует его). Правила проекта меняем **только здесь**.

## Проект

Прошивка для **ESP32-2432S028 (CYD, «Cheap Yellow Display»)**: по Wi‑Fi опрашивает гибридный инвертор **Deye** через логгер Solarman (протокол **Solarman V5**, внутри Modbus RTU, TCP-порт 8899) и показывает данные батареи, сети, нагрузки и солнечных панелей на экране 320×240 с тачем.

- Железо: дисплей ILI9341 (TFT_eSPI, пины в `platformio.ini`, rotation 1, подсветка на GPIO21 через PWM), тач XPT2046 (отдельная шина HSPI, пины в `include/config.h`), кнопка BOOT (GPIO0: долгое нажатие открывает Settings).
- Стек: PlatformIO, framework Arduino, `lvgl@^8.3` (API v8, **не v9**), `TFT_eSPI`, `WiFiManager`, `XPT2046_Touchscreen` (из git).
- Конфигурация TFT/LVGL задаётся только через `build_flags` в `platformio.ini` (`USER_SETUP_LOADED`, `LV_CONF_SKIP`), отдельных `User_Setup.h` и `lv_conf.h` нет. Шрифты Montserrat включаются там же флагами `LV_FONT_MONTSERRAT_*`.
- Разметка флеш-памяти `min_spiffs.csv` (LVGL занимает много места).

## Карта кода

| Модуль | Ответственность | Ключевые функции |
|---|---|---|
| `src/main.cpp` | `setup()` и `loop()`: BOOT, таймаут экрана, таймаут запроса, интервал опроса | константы `REQUEST_TIMEOUT_MS=4000`, `POLL_INTERVAL_MS=5000` |
| `src/display.cpp` | TFT, LVGL draw buffer и flush, подсветка | `displayInit`, `lvglInit`, `backlightSet` |
| `src/touch.cpp` | XPT2046 → LVGL indev, пробуждение экрана касанием | `touchInit`, `touchRegisterLvglIndev` |
| `src/network.cpp` | WiFiManager (портал `Deye_Monitor_ESP32_IoT` с полями IP/порт/SN/SlaveID/TZ/DST), ретраи, NTP, reconnect | `networkSetup`, `networkCheckReconnect`, `networkApplyTimeConfig` |
| `src/inverter.cpp` | кадр Solarman V5, разбор ответа, данные инвертора | `inverterRequestData`, `inverterHandleResponse`, геттеры `inverterGet*` |
| `src/settings.cpp` | NVS (Preferences), глобальные настройки (`INVERTER_IP`, `INVERTER_SN`, `INVERTER_SLAVE_ID`, `TIMEZONE_HOUR`, `DST_ENABLED`, `SCREEN_TIMEOUT_MIN`…) | `settingsLoad`, `settingsSave*` |
| `src/ui.cpp` | LVGL TabView: Battery, Grid, Solar, Settings (скрыта в таббаре), статус-бар | `uiBuild`, `uiUpdate`, `uiUpdateStatusBar` |
| `include/config.h` | пины CYD, дефолты инвертора, **карта регистров** (`REG_BLOCK_START/LEN`, `ADDR_*`) | — |

Документация: [docs/behavior_scenarios.md](docs/behavior_scenarios.md) описывает поведение при первом старте, пропадании света, Reboot и потере сети. [docs/DECOMPOSITION.md](docs/DECOMPOSITION.md) содержит архитектуру, но частично ссылается на `main.cpp` в том виде, каким он был до разбиения на модули. Сверяйтесь с таблицей выше.

## Сборка

`pio` не добавлен в PATH, вызывать по полному пути:

```
~/.platformio/penv/Scripts/pio.exe run -e cyd                 # сборка
~/.platformio/penv/Scripts/pio.exe run -e cyd -t upload       # прошивка (устройство подключено к USB)
~/.platformio/penv/Scripts/pio.exe device monitor -p COM3 -b 115200 -f esp32_exception_decoder
```

Подробности в skill `pio-build-flash` (`.claude/skills/`).

## Инварианты (не ломать)

1. **Битый ответ не интерпретируется.** В `inverterHandleResponse()` кадр отбрасывается целиком (`dropFrame()`), соединение закрывается, старые значения на экране остаются, если: ответ короткий, нет `A5`, не сошлась контрольная сумма V5, не найден заголовок Modbus, данных меньше `REG_BLOCK_LEN*2`, не сошёлся Modbus CRC или значения неправдоподобны (`checkPlausibility()`: диапазоны плюс закон Ома для батареи). Значения сначала разбираются в `InverterSample` и применяются только после всех проверок. Новое поле добавлять туда же вместе с проверкой диапазона.
2. **`loop()` не блокировать.** Никаких длинных `delay()` и синхронных ожиданий. `lv_timer_handler()` вызывается каждый цикл. Ожидание ответа ограничено таймаутом `REQUEST_TIMEOUT_MS`. После таймаута обязательно вызывать `inverterStopClient()`, он сбрасывает `isRequestSent`, иначе опрос встанет.
3. **Опрос Modbus идёт только при включённом экране и не на вкладке Settings.** При пробуждении (касание или BOOT) вызывается `inverterSetLastUpdate(0)`, чтобы опрос возобновился сразу.
4. **Устройство может загрузиться раньше роутера** (сценарий после пропадания света). Wi‑Fi подключается с ретраями, затем идёт `ESP.restart()`. В работе потерю сети обрабатывает `networkCheckReconnect()`.
5. Настройки хранятся в NVS через модуль `settings`. Новая настройка добавляется по цепочке: дефолт в `config.h` → `settingsLoad`/`settingsSave*` → UI или поле WiFiManager.
6. Адреса регистров в `config.h` **десятичные** (как в документации Deye). Масштабы: напряжение батареи `/100`, ток батареи `/100` (signed), температура `(x-1000)/10`, сеть, PV и энергия `/10`, мощности без масштаба (батарея и сеть signed).
7. Время: `configTime(TZ*3600, DST ? 3600 : 0, …)`. DST — ручной флаг, не автоматический переход. Правки времени уже дважды ломали опрос Modbus, поэтому такие изменения делать отдельно и проверять изолированно.

## Порядок работы с пользователем

- Отвечать **по-русски**, комментарии в коде тоже по-русски (как в существующем коде).
- Для нетривиальных изменений сначала предложить план или сценарий, реализовывать после подтверждения («Примени»).
- Менять маленькими шагами: одно изменение, затем сборка. Пользователь часто просит откатить, поэтому смешанные правки не делать.
- Прошивку на железе проверяет **пользователь** и присылает serial-лог или фото экрана. Агент обязан убедиться, что проект **собирается** (`pio run -e cyd`). Прошивать и открывать монитор только по просьбе.
- Не коммитить без явной просьбы.
- Папка `build/` к проекту не относится (её создаёт расширение 1С), не трогать и не добавлять в git.
- Для документации библиотек (LVGL 8.3, TFT_eSPI, WiFiManager, ESP32 Arduino) использовать MCP **context7**.
