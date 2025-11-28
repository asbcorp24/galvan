// main.cpp — GalvaControl ESP32
// - ESP32-WROOM
// - Hall (магниты по ваннам)
// - X: реле вперёд / назад
// - Z: реле вверх / вниз + концевики
// - RTC DS1302 (Makuna/RTC)
// - OLED SSD1306 I2C
// - WiFi AP + WebServer + JSON API
// - Рецепт (паттерн) в NVS, бинарный Step[] + выбор шаблона на OLED

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RtcDS1302.h>
#include <ArduinoJson.h>
#include <vector>
#include "web_ui.h"
#include "web_monitor.h"
#include "web_routes.h"
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_PN532.h>
// ---------------- DEBUG МАКРОСЫ ----------------

// Включение/выключение глобального debug-лога
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
  #define DBG_PRINT(x)        Serial.print(x)
  #define DBG_PRINTLN(x)      Serial.println(x)
  #define DBG_PRINTF(...)     Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(...)
#endif

// ---------------- ПИНОВКА ----------------

// OLED
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_ADDR    0x3C

// I2C
#define I2C_SDA 21
#define I2C_SCL 22

// DS1302 (Makuna/RTC — ThreeWire)
#define RTC_CE    17
#define RTC_SCLK  16
#define RTC_IO    4

// Реле (ACTIVE-HIGH)
#define REL_X_FWD   19
#define REL_X_REV   18
#define REL_Z_UP    5
#define REL_Z_DOWN  23
#define REL_SPIN    15
#define REL_FAN     2

// Концевики по Z
#define SW_Z_TOP     14
#define SW_Z_BOTTOM  13

#define PN532_IRQ   14   // можно не использовать
#define PN532_RESET 27
// Аварийный стоп
#define PIN_ESTOP  26

// Кнопка START (дубль веб-старта)
#define BTN_START  33

// Энкодер
#define ENC_A      34
#define ENC_B      35
#define ENC_SW     32

// ---------------- КОНСТАНТЫ ----------------

#define ON  HIGH
#define OFF LOW
int16_t g_activeRoute = -1;   // -1 = не выбран
const char* WIFI_SSID = "GalvaControl";
const char* WIFI_PASS = "12345678";

static const uint8_t ROUTE_MAX_STEPS = 50;
static const char*  NVS_NS          = "galva";

// ----------- NVS STORAGE FOR BATH TAGS ---------------
// Ключи в NVS
static const char* NVS_KEY_BATH_COUNT = "bath_cnt";
static const char* NVS_KEY_BATH_TAGS  = "bath_tags";

// --- ДИНАМИЧЕСКОЕ КОЛИЧЕСТВО ВАНН ---

static const int MIN_BATHS = 5;          // минимальное количество ванн
static const int MAX_BATHS_LIMIT = 20;   // максимальный возможный предел

int dynamicBathCount = MIN_BATHS;        // текущее количество ванн

static const int HIST_LEN  = 120;    // длина истории (120 точек = 6 мин при шаге ~3 сек)

// Структура записи истории ванны (температура, pH, ORP, ток)
struct BathHistRec {
    float temp;
    float ph;
    float orp;
    float current;
};

// История ванн
BathHistRec hist[MAX_BATHS_LIMIT][HIST_LEN];
uint16_t    histPtr[MAX_BATHS_LIMIT] = {0};

// Описание одной ванны с RFID
struct BathInfo {
    uint16_t bathNumber;   // логический номер ванны (для маршрутов)
    uint8_t  uidLen;       // длина UID
    uint8_t  uid[7];       // сам UID (до 7 байт)
    bool     isStart;      // флаг "начальная ванна"
    bool     isEnd;        // флаг "конечная ванна"
};

// Динамический список ванн
std::vector<BathInfo> g_baths;


// слоты шаблонов
static const uint8_t ROUTE_SLOTS = 5;
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ---------------- ТИПЫ ----------------

// Описание одного шага (одной ванны) в рецепте
#pragma pack(push,1)
struct Step {
    uint16_t bath;
    uint16_t hold_s;
    uint16_t z_down_s;
    uint16_t z_up_s;
    uint16_t dry_s;
    uint16_t flags; // bit0=spin, bit1=fan
};
#pragma pack(pop)

// Состояния конечного автомата процесса
enum ProcState : uint8_t {
    PS_IDLE = 0,
    PS_HOMING,
    PS_MOVE_X,
    PS_LOWER_Z,
    PS_HOLD,
    PS_RAISE_Z,
    PS_DRY,
    PS_NEXT_STEP,
    PS_RETURN_HOME,
    PS_ERROR
};

// ---------------- ГЛОБАЛЬНЫЕ ----------------

// I2C/OLED
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// RTC
ThreeWire rtcWire(RTC_IO, RTC_SCLK, RTC_CE);                   // IO, SCLK, CE
RtcDS1302<ThreeWire> rtc(rtcWire);

// WiFi + HTTP
WebServer server(80);

// NVS
Preferences prefs;

// Рецепт (активный в ОЗУ)
Step     g_route[ROUTE_MAX_STEPS];
uint16_t g_routeSteps = 0;

bool oledReady = false;

// Состояние процесса
volatile ProcState g_state       = PS_IDLE;
volatile int16_t   g_stepIdx     = -1;
volatile int16_t   g_currentBath = 0;

// Команды
volatile bool g_startCommand = false;
volatile bool g_stopCommand  = false;


// Позиция моста по X (номер ванны 0..MAX_BATHS-1)
volatile int16_t bathIndex = 0;

// Направление движения по X (true = вправо, false = влево)
volatile bool    dirRight  = true;


// Используем только I2C (SDA=21, SCL=22), IRQ/RESET НЕ НУЖНЫ ДЛЯ I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);



// Меню выбора шаблона
volatile int16_t g_selectedRoute = 0;   // 0..ROUTE_SLOTS-1
volatile bool    g_inSelectMenu  = false;

// Энкодер
volatile int g_encDelta = 0;


// ------------------------------------------------------
//    SAVE/LOAD g_baths (BathInfo) и dynamicBathCount в NVS
// ------------------------------------------------------

// Структура для сохранения в NVS
struct BathRecordNVS {
    uint16_t bathNumber;
    uint8_t  uidLen;
    uint8_t  uid[7];
    uint8_t  flags;   // bit0 = isStart, bit1 = isEnd
};

// Сохранение bathList в NVS
void saveBathListToNVS() {
    prefs.begin(NVS_NS, false);

    // обновим dynamicBathCount
    dynamicBathCount = (int)g_baths.size();
    if (dynamicBathCount > MAX_BATHS_LIMIT) {
        dynamicBathCount = MAX_BATHS_LIMIT;
    }

    // сохраняем количество ванн
    prefs.putUShort(NVS_KEY_BATH_COUNT, (uint16_t)dynamicBathCount);

    if (dynamicBathCount == 0) {
        prefs.end();
        Serial.println("[NVS] Saved 0 baths");
        return;
    }

    BathRecordNVS records[MAX_BATHS_LIMIT];

    for (int i = 0; i < dynamicBathCount; ++i) {
        const BathInfo &b = g_baths[i];
        records[i].bathNumber = b.bathNumber;
        records[i].uidLen     = b.uidLen;

        memset(records[i].uid, 0, sizeof(records[i].uid));
        if (b.uidLen > 0 && b.uidLen <= 7) {
            memcpy(records[i].uid, b.uid, b.uidLen);
        }

        uint8_t f = 0;
        if (b.isStart) f |= 0x01;
        if (b.isEnd)   f |= 0x02;
        records[i].flags = f;
    }

    prefs.putBytes(NVS_KEY_BATH_TAGS,
                   records,
                   sizeof(BathRecordNVS) * dynamicBathCount);

    prefs.end();

    Serial.printf("[NVS] Saved %d baths\n", dynamicBathCount);
}

// Загрузка bathList из NVS
void loadBathListFromNVS() {
    prefs.begin(NVS_NS, true);

    uint16_t cnt = prefs.getUShort(NVS_KEY_BATH_COUNT, 0);

    if (cnt == 0 || cnt > MAX_BATHS_LIMIT) {
        prefs.end();
        Serial.println("[NVS] No bath list saved (or invalid count)");
        g_baths.clear();
        dynamicBathCount = 0;
        return;
    }

    size_t need = sizeof(BathRecordNVS) * cnt;
    size_t have = prefs.getBytesLength(NVS_KEY_BATH_TAGS);

    if (have < need) {
        prefs.end();
        Serial.println("[NVS] bath_list corrupted or incomplete");
        g_baths.clear();
        dynamicBathCount = 0;
        return;
    }

    BathRecordNVS records[MAX_BATHS_LIMIT];
    prefs.getBytes(NVS_KEY_BATH_TAGS, records, need);
    prefs.end();

    g_baths.clear();
    g_baths.reserve(cnt);

    for (int i = 0; i < cnt; ++i) {
        BathInfo b;
        b.bathNumber = records[i].bathNumber;
        b.uidLen     = records[i].uidLen;

        if (b.uidLen > 7) b.uidLen = 7;
        memset(b.uid, 0, sizeof(b.uid));
        if (b.uidLen > 0) {
            memcpy(b.uid, records[i].uid, b.uidLen);
        }

        b.isStart = records[i].flags & 0x01;
        b.isEnd   = records[i].flags & 0x02;

        g_baths.push_back(b);
    }

    dynamicBathCount = (int)g_baths.size();

    Serial.printf("[NVS] Loaded %d baths\n", dynamicBathCount);

    
}

// ------------------------------------------------------
//   ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ РАБОТЫ С ДИНАМИЧЕСКИМ СПИСКОМ ВАНН
// ------------------------------------------------------

// Поиск индекса ванны по UID (возвращает -1, если не нашли)
int findBathIndexByUID(const uint8_t uid[], uint8_t uidLen) {
    if (uidLen == 0 || uidLen > 7) return -1;

    for (int i = 0; i < (int)g_baths.size(); ++i) {
        const BathInfo &b = g_baths[i];
        if (b.uidLen != uidLen) continue;

        bool same = true;
        for (uint8_t j = 0; j < uidLen; ++j)
            if (b.uid[j] != uid[j]) { same = false; break; }

        if (same) return i;
    }
    return -1;
}

// Добавить новую ванну или обновить существующую
void addOrUpdateBathByUID(const uint8_t uid[], uint8_t uidLen) {
    int idx = findBathIndexByUID(uid, uidLen);

    if (idx >= 0) {
        BathInfo &b = g_baths[idx];
        b.uidLen = uidLen;
        memcpy(b.uid, uid, uidLen);
        saveBathListToNVS();
        return;
    }

    if ((int)g_baths.size() >= MAX_BATHS_LIMIT) return;

    BathInfo b{};
    b.bathNumber = (uint16_t)g_baths.size();
    b.uidLen = uidLen;
    memcpy(b.uid, uid, uidLen);

    g_baths.push_back(b);
    dynamicBathCount = g_baths.size();
    saveBathListToNVS();
}




// ---------------- ПРОТОТИП ФУНКЦИИ СМЕНЫ СОСТОЯНИЯ ----------------

void setState(ProcState newState, const char* reason);

// Прототип функции привязки RFID-метки к новой ванне
void assignNewBathTag(uint8_t uid[], uint8_t uidLen);

// ---------------- БИБЛИОТЕКА РЕЦЕПТОВ В NVS ----------------
// Индекс: ключ "routes" (строка CSV вида "1,2,5")
// совместимость: "активный" рецепт — слот 0

// Сохранить id активного рецепта (для web-интерфейса)
void saveActiveRoute(int id) {
    DBG_PRINTF("[NVS] saveActiveRoute: %d\r\n", id);
    prefs.begin("galva", false);
    prefs.putShort("active_route", id);
    prefs.end();
    g_activeRoute = id;
}

// Сохранение шаблона в слот (0..ROUTE_SLOTS-1)
bool saveRouteSlot(uint8_t slot, const Step* steps, uint16_t count) {
    DBG_PRINTF("[NVS] saveRouteSlot: slot=%u, count=%u\r\n", slot, count);
    if (slot >= ROUTE_SLOTS || count == 0 || count > ROUTE_MAX_STEPS) {
        DBG_PRINTLN(F("[NVS] saveRouteSlot: invalid params"));
        return false;
    }

    char keySteps[16];
    char keyData[16];
    snprintf(keySteps, sizeof(keySteps), "r%u_steps", slot);
    snprintf(keyData,  sizeof(keyData),  "r%u_data",  slot);

    prefs.begin(NVS_NS, false);
    prefs.putUShort(keySteps, count);
    prefs.putBytes(keyData, steps, count * sizeof(Step));
    prefs.end();
    return true;
}

// Загрузка шаблона из слота
bool loadRouteSlot(uint8_t slot) {
    DBG_PRINTF("[NVS] loadRouteSlot: slot=%u\r\n", slot);
    if (slot >= ROUTE_SLOTS) return false;

    char keySteps[16];
    char keyData[16];
    snprintf(keySteps, sizeof(keySteps), "r%u_steps", slot);
    snprintf(keyData,  sizeof(keyData),  "r%u_data",  slot);

    prefs.begin(NVS_NS, true);
    uint16_t n = prefs.getUShort(keySteps, 0);
    DBG_PRINTF("[NVS]  steps=%u\r\n", n);
    if (n == 0 || n > ROUTE_MAX_STEPS) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  invalid steps count"));
        return false;
    }
    size_t need = n * sizeof(Step);
    size_t have = prefs.getBytesLength(keyData);
    DBG_PRINTF("[NVS]  need=%u, have=%u\r\n", (unsigned)need, (unsigned)have);
    if (have < need) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  not enough data"));
        return false;
    }
    prefs.getBytes(keyData, g_route, need);
    prefs.end();

    g_routeSteps = n;
    DBG_PRINTF("[NVS]  route loaded, g_routeSteps=%u\r\n", g_routeSteps);
    return true;
}

bool saveRoute(const Step* steps, uint16_t count) {
    DBG_PRINTF("[NVS] saveRoute (active slot 0), count=%u\r\n", count);
    return saveRouteSlot(0, steps, count);
}

bool loadRoute() {
    DBG_PRINTLN(F("[NVS] loadRoute (active slot 0)"));
    return loadRouteSlot(0);
}

// Парсим CSV "1,2,5" в массив uint16
std::vector<uint16_t> parseRouteIds(const String &csv) {
    DBG_PRINTF("[NVS] parseRouteIds: \"%s\"\r\n", csv.c_str());
    std::vector<uint16_t> ids;
    int start = 0;
    while (start < (int)csv.length()) {
        int comma = csv.indexOf(',', start);
        if (comma < 0) comma = csv.length();
        String token = csv.substring(start, comma);
        token.trim();
        if (token.length() > 0) {
            uint16_t id = (uint16_t)token.toInt();
            ids.push_back(id);
        }
        start = comma + 1;
    }
    DBG_PRINTF("[NVS]  parsed %u ids\r\n", (unsigned)ids.size());
    return ids;
}

String buildRouteIdsCsv(const std::vector<uint16_t> &ids) {
    String out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) out += ",";
        out += String(ids[i]);
    }
    DBG_PRINTF("[NVS] buildRouteIdsCsv: \"%s\"\r\n", out.c_str());
    return out;
}

// Загрузить список ID из NVS
std::vector<uint16_t> getRouteIds() {
    DBG_PRINTLN(F("[NVS] getRouteIds"));
    prefs.begin(NVS_NS, true);
    String csv = prefs.getString("routes", "");
    prefs.end();
    return parseRouteIds(csv);
}

// Сохранить список ID в NVS
void setRouteIds(const std::vector<uint16_t> &ids) {
    DBG_PRINTLN(F("[NVS] setRouteIds"));
    String csv = buildRouteIdsCsv(ids);
    prefs.begin(NVS_NS, false);
    prefs.putString("routes", csv);
    prefs.end();
}

// Проверить, есть ли id в списке
bool containsId(const std::vector<uint16_t> &ids, uint16_t id) {
    for (auto v : ids) if (v == id) return true;
    return false;
}

// Сохранить рецепт в библиотеку: route_<id>_steps / _data + обновить индекс
bool saveLibRoute(uint16_t id, const Step* steps, uint16_t count) {
    DBG_PRINTF("[NVS] saveLibRoute: id=%u, count=%u\r\n", id, count);
    if (count == 0 || count > ROUTE_MAX_STEPS) {
        DBG_PRINTLN(F("[NVS]  invalid count"));
        return false;
    }

    char keySteps[32];
    char keyData[32];
    snprintf(keySteps, sizeof(keySteps), "route_%u_steps", id);
    snprintf(keyData,  sizeof(keyData),  "route_%u_data",  id);

    prefs.begin(NVS_NS, false);
    prefs.putUShort(keySteps, count);
    prefs.putBytes(keyData, steps, count * sizeof(Step));
    prefs.end();

    // обновляем индекс
    auto ids = getRouteIds();
    if (!containsId(ids, id)) {
        ids.push_back(id);
        setRouteIds(ids);
    }
    return true;
}

// Загрузить рецепт из библиотеки в буфер dst[]
bool loadLibRoute(uint16_t id, Step* dst, uint16_t &countOut) {
    DBG_PRINTF("[NVS] loadLibRoute: id=%u\r\n", id);
    char keySteps[32];
    char keyData[32];
    snprintf(keySteps, sizeof(keySteps), "route_%u_steps", id);
    snprintf(keyData,  sizeof(keyData),  "route_%u_data",  id);

    prefs.begin(NVS_NS, true);
    uint16_t n = prefs.getUShort(keySteps, 0);
    if (n == 0 || n > ROUTE_MAX_STEPS) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  no such route or invalid count"));
        return false;
    }
    size_t need = n * sizeof(Step);
    size_t have = prefs.getBytesLength(keyData);
    if (have < need) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  not enough bytes in NVS"));
        return false;
    }
    prefs.getBytes(keyData, dst, need);
    prefs.end();
    countOut = n;
    DBG_PRINTF("[NVS]  loaded %u steps\r\n", countOut);
    return true;
}

// Удалить рецепт из библиотеки
bool deleteLibRoute(uint16_t id) {
    DBG_PRINTF("[NVS] deleteLibRoute: id=%u\r\n", id);
    char keySteps[32];
    char keyData[32];
    snprintf(keySteps, sizeof(keySteps), "route_%u_steps", id);
    snprintf(keyData,  sizeof(keyData),  "route_%u_data",  id);

    prefs.begin(NVS_NS, false);
    prefs.remove(keySteps);
    prefs.remove(keyData);
    prefs.end();

    auto ids = getRouteIds();
    std::vector<uint16_t> out;
    out.reserve(ids.size());
    for (auto v : ids) if (v != id) out.push_back(v);
    setRouteIds(out);
    return true;
}

// Выбрать активный рецепт (тот, который крутит кран)
// Копируем рецепт из библиотеки в “активный” (saveRoute)
bool applyLibRouteAsActive(uint16_t id) {
    DBG_PRINTF("[NVS] applyLibRouteAsActive: id=%u\r\n", id);
    Step tmp[ROUTE_MAX_STEPS];
    uint16_t n = 0;
    if (!loadLibRoute(id, tmp, n)) return false;
    // сохраняем как активный
    saveRoute(tmp, n);
    // обновляем ОЗУ
    memcpy(g_route, tmp, n * sizeof(Step));
    g_routeSteps = n;

    // запоминаем активный id
    prefs.begin(NVS_NS, false);
    prefs.putUShort("active_route_id", id);
    prefs.end();

    DBG_PRINTF("[NVS]  active route set, steps=%u\r\n", g_routeSteps);
    return true;
}

// Получить id активного рецепта (если нет — 0xFFFF)
uint16_t getActiveRouteId() {
    prefs.begin(NVS_NS, true);
    uint16_t id = prefs.getUShort("active_route_id", 0xFFFF);
    prefs.end();
    return id;
}

// ---------------- RTC HELPERS ----------------
String rtcTimeString() {
    RtcDateTime now = rtc.GetDateTime();

    char buf[20];
    sprintf(buf, "%02u:%02u:%02u",
            now.Hour(),
            now.Minute(),
            now.Second());

    return String(buf);
}

String rtcDateString() {
    RtcDateTime now = rtc.GetDateTime();

    char buf[20];
    sprintf(buf, "%02u.%02u.%04u",
            now.Day(),
            now.Month(),
            now.Year());

    return String(buf);
}

// ---------------- PN532 INIT ----------------

void nfcInit() {
    DBG_PRINTLN(F("[PN532] init start"));

    //Wire.begin(I2C_SDA, I2C_SCL);

    nfc.begin();


    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        DBG_PRINTLN(F("[PN532] Didn't find PN53x board"));
        return;
    }

    DBG_PRINTF("[PN532] Chip: 0x%02X, Ver: %u.%u\r\n",
               (uint8_t)(versiondata >> 24),
               (uint8_t)((versiondata >> 16) & 0xFF),
               (uint8_t)((versiondata >> 8)  & 0xFF));

    // Конфигурируем PN532 в режиме чтения пассивных карт
    nfc.SAMConfig();

    DBG_PRINTLN(F("[PN532] init done"));
}

// Чтение RFID-метки.
// Возвращает true, если метка найдена и сопоставлена с какой-то ванной.
// detectedBath — индекс ванны (0..dynamicBathCount-1)
bool readTag(int &detectedBath)
{
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                           uid, &uidLength);
    if (!success) return false;

    // выводим UID
    DBG_PRINT("[PN532] UID = ");
    for (uint8_t i = 0; i < uidLength; i++) {
        DBG_PRINTF("%02X ", uid[i]);
    }
    DBG_PRINTLN("");

    // регистрируем новую ванну, если её ещё не было
    assignNewBathTag(uid, uidLength);

    // ищем индекс ванны по UID
    int idx = findBathIndexByUID(uid, uidLength);
    if (idx >= 0) {
        detectedBath = idx;
        return true;
    }

    return false;
    Serial.println("DEBUG: nfcInit() finished!");

}

// Привязать RFID-метку к ванне (динамический список g_baths)
void assignNewBathTag(uint8_t uid[], uint8_t uidLen) {
    if (uidLen == 0 || uidLen > 7) return;

    // пробуем найти по UID — если есть, просто ничего не делаем
    int idx = findBathIndexByUID(uid, uidLen);
    if (idx >= 0) {
        Serial.printf("[BATH] UID already assigned to bath index=%d, bathNumber=%u\n",
                      idx, g_baths[idx].bathNumber);
        return;
    }

    // иначе добавляем новую ванну
    addOrUpdateBathByUID(uid, uidLen);
}

void nfcTestLoop() {
    int bath = -1;
    int detectedBath = -1;
    uint8_t uid[7];
    uint8_t uidLength;

    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.println("NFC TEST MODE");
    oled.println("Поднесите метку...");
    oled.display();

    while (true) {
        uint8_t uid[7] = {0};
        uint8_t uidLength = 0;

        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                               uid, &uidLength);
        if (success) {
            Serial.print("[NFC] UID: ");
            for (uint8_t i = 0; i < uidLength; i++) {
                Serial.printf("%02X ", uid[i]);
            }
            Serial.println();

            oled.clearDisplay();
            oled.setCursor(0, 0);
            oled.println("NFC UID FOUND:");
            for (uint8_t i = 0; i < uidLength; i++) {
                oled.printf("%02X ", uid[i]);
            }
            oled.display();

            delay(1500);

            oled.clearDisplay();
            oled.setCursor(0, 0);
            oled.println("Поднесите снова");
            oled.display();
        }

        delay(80);
    }
}

// ---------------- OLED ----------------

void oledInit() {
    DBG_PRINTLN(F("[OLED] init start"));

    // проверяем шину I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // тест: пин сканер
    Wire.beginTransmission(OLED_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[OLED] NOT FOUND at 0x3C");
        return;
    }

    // запускаем SSD1306
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[OLED] begin() FAIL");
        return;
    }

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("OLED OK"));
    oled.display();

    oledReady = true;
    Serial.println("[OLED] init done");
}

void initBathTables() {
    // сброс истории ванн
    for (int b = 0; b < MAX_BATHS_LIMIT; ++b) {
        histPtr[b] = 0;
        for (int i = 0; i < HIST_LEN; ++i) {
            hist[b][i].temp    = 0;
            hist[b][i].ph      = 0;
            hist[b][i].orp     = 0;
            hist[b][i].current = 0;
        }
    }

    // сбрасываем список ванн
    g_baths.clear();
    dynamicBathCount = 0;
}


void oledShowIdle() {
    DBG_PRINTLN(F("[OLED] show IDLE screen"));
    oled.clearDisplay();
    oled.setCursor(0,0);
    oled.println(rtcTimeString());
    oled.println(rtcDateString());
    oled.print(F("State: IDLE\nBath: "));
    oled.println(g_currentBath);
    oled.print(F("Steps: "));
    oled.println(g_routeSteps);
    oled.println(F("START=запуск"));
    oled.println(F("ENC=выбор шаблона"));
    oled.display();
}

void oledShowRun() {
    DBG_PRINTF("[OLED] show RUN screen, state=%d, stepIdx=%d\r\n", (int)g_state, (int)g_stepIdx);
    oled.clearDisplay();
    oled.setCursor(0,0);
    oled.println(rtcTimeString());

    oled.print(F("Bath: "));
    oled.println(g_currentBath);

    oled.print(F("Step: "));
    oled.print(g_stepIdx + 1);
    oled.print(F("/"));
    oled.println(g_routeSteps);

    oled.print(F("State: "));
    switch (g_state) {
        case PS_HOMING:      oled.println(F("HOMING")); break;
        case PS_MOVE_X:      oled.println(F("MOVE_X")); break;
        case PS_LOWER_Z:     oled.println(F("LOWER_Z")); break;
        case PS_HOLD:        oled.println(F("HOLD")); break;
        case PS_RAISE_Z:     oled.println(F("RAISE_Z")); break;
        case PS_DRY:         oled.println(F("DRY")); break;
        case PS_RETURN_HOME: oled.println(F("RETURN")); break;
        case PS_ERROR:       oled.println(F("ERROR")); break;
        default:             oled.println(F("RUN")); break;
    }

    oled.display();
}

void oledShowSelectMenu() {
    DBG_PRINTF("[OLED] show select menu, selected=%d\r\n", (int)g_selectedRoute);
    oled.clearDisplay();
    oled.setCursor(0,0);
    oled.println(F("Выбор шаблона"));
    oled.println(F("----------------"));

    for (int i = 0; i < ROUTE_SLOTS; i++) {
        if (i == g_selectedRoute) oled.print(F("> "));
        else                      oled.print(F("  "));
        oled.print(F("Шаблон "));
        oled.println(i);
    }
    oled.display();
}

// ---------------- РЕЛЕ ----------------

void relOffAll() {
    DBG_PRINTLN(F("[RELAY] OFF ALL"));
    digitalWrite(REL_X_FWD, OFF);
    digitalWrite(REL_X_REV, OFF);
    digitalWrite(REL_Z_UP, OFF);
    digitalWrite(REL_Z_DOWN, OFF);
    digitalWrite(REL_SPIN, OFF);
    digitalWrite(REL_FAN, OFF);
}

void xFwd(bool en) {
    DBG_PRINTF("[RELAY] X_FWD=%d\r\n", (int)en);
    digitalWrite(REL_X_REV, OFF);
    digitalWrite(REL_X_FWD, en ? ON : OFF);
}

void xRev(bool en) {
    DBG_PRINTF("[RELAY] X_REV=%d\r\n", (int)en);
    digitalWrite(REL_X_FWD, OFF);
    digitalWrite(REL_X_REV, en ? ON : OFF);
}

void zUpRelay(bool en) {
    DBG_PRINTF("[RELAY] Z_UP=%d\r\n", (int)en);
    digitalWrite(REL_Z_DOWN, OFF);
    digitalWrite(REL_Z_UP, en ? ON : OFF);
}

void zDownRelay(bool en) {
    DBG_PRINTF("[RELAY] Z_DOWN=%d\r\n", (int)en);
    digitalWrite(REL_Z_UP, OFF);
    digitalWrite(REL_Z_DOWN, en ? ON : OFF);
}

// ---------------- ЭНКОДЕР + КНОПКА ----------------
void IRAM_ATTR encISR() {
    bool a = digitalRead(ENC_A);
    bool b = digitalRead(ENC_B);
    if (a == b) g_encDelta++;
    else        g_encDelta--;
}

// Считать и обнулить дельту энкодера
int getEncDelta() {
    int d = g_encDelta;
    g_encDelta = 0;
    if (d != 0) {
        DBG_PRINTF("[ENC] delta=%d\r\n", d);
    }
    return d;
}

// Обработка кнопки START с антидребезгом
bool startButtonPressed() {
    static uint32_t last = 0;
    if (digitalRead(BTN_START) == LOW) {
        uint32_t now = millis();
        if (now - last > 200) {
            last = now;
            DBG_PRINTLN(F("[BTN] START pressed"));
            return true;
        }
    }
    return false;
}

// ---------------- ДВИЖЕНИЕ X — RFID версия ----------------
// Движение моста к нужной ванне по RFID (PN532)
// Возвращает true — если дошёл до нужной ванны
// Возвращает false — если таймаут или авария

bool moveToBath(int targetBath, uint32_t timeoutMs)
{
    DBG_PRINTF("[MOVE_X] moveToBath target=%d current=%d\n",
               targetBath, bathIndex);

    uint32_t t0 = millis();

    if (targetBath == bathIndex) {
        xFwd(false);
        xRev(false);
        g_currentBath = bathIndex;
        return true;
    }

    dirRight = (targetBath > bathIndex);

    if (dirRight) { xRev(false); xFwd(true); }
    else          { xFwd(false); xRev(true); }

    while (millis() - t0 < timeoutMs)
    {
        int detected = -1;

        if (readTag(detected)) {
            bathIndex = detected;
            g_currentBath = detected;

            if (detected == targetBath) {
                xFwd(false);
                xRev(false);
                return true;
            }
        }

        if (digitalRead(PIN_ESTOP) == LOW) break;

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    xFwd(false);
    xRev(false);
    return false;
}

// ---------------- ДВИЖЕНИЕ Z ----------------
bool zDown_for(uint16_t sec) {
    DBG_PRINTF("[MOVE_Z] zDown_for sec=%u\r\n", sec);
    uint32_t t0 = millis();
    uint32_t timeout = sec * 1500UL;

    zUpRelay(false);
    zDownRelay(true);

    while (millis() - t0 < (uint32_t)sec * 1000UL) {
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] ESTOP while moving down"));
            break;
        }

        // вниз нельзя, если верхний концевик нажат (мы уже вверху)
        if (digitalRead(SW_Z_TOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] SW_Z_TOP active while going down, error"));
            zDownRelay(false);
            return false;
        }
        // нижний концевик — дошли раньше времени
        if (digitalRead(SW_Z_BOTTOM) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] reached bottom switch earlier"));
            zDownRelay(false);
            return true;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    zDownRelay(false);
    bool ok = (millis() - t0 < timeout);
    DBG_PRINTF("[MOVE_Z] zDown_for done, ok=%d\r\n", (int)ok);
    return ok;
}

bool zUp_for(uint16_t sec) {
    DBG_PRINTF("[MOVE_Z] zUp_for sec=%u\r\n", sec);
    uint32_t t0 = millis();
    uint32_t timeout = sec * 1500UL;

    zDownRelay(false);
    zUpRelay(true);

    while (millis() - t0 < (uint32_t)sec * 1000UL) {
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] ESTOP while moving up"));
            break;
        }

        // вверх нельзя, если нижний концевик нажат
        if (digitalRead(SW_Z_BOTTOM) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] SW_Z_BOTTOM active while going up, error"));
            zUpRelay(false);
            return false;
        }
        // верхний концевик — дошли
        if (digitalRead(SW_Z_TOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] reached top switch"));
            zUpRelay(false);
            return true;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    zUpRelay(false);
    bool ok = (millis() - t0 < timeout);
    DBG_PRINTF("[MOVE_Z] zUp_for done, ok=%d\r\n", (int)ok);
    return ok;
}

// ---------------- HTTP ----------------

String stateToString(ProcState st) {
    switch (st) {
        case PS_IDLE:        return F("IDLE");
        case PS_HOMING:      return F("HOMING");
        case PS_MOVE_X:      return F("MOVE_X");
        case PS_LOWER_Z:     return F("LOWER_Z");
        case PS_HOLD:        return F("HOLD");
        case PS_RAISE_Z:     return F("RAISE_Z");
        case PS_DRY:         return F("DRY");
        case PS_NEXT_STEP:   return F("NEXT_STEP");
        case PS_RETURN_HOME: return F("RETURN_HOME");
        case PS_ERROR:       return F("ERROR");
        default:             return F("UNKNOWN");
    }
}

void handleRoot() {
    DBG_PRINTLN(F("[HTTP] GET /"));
    server.send_P(200, "text/html", WEB_UI_INDEX);
}

// GET /routes_list  -> список рецептов в библиотеке
void handleRoutesList() {
    DBG_PRINTLN(F("[HTTP] GET /routes_list"));
    auto ids = getRouteIds();
    String out;
    out.reserve(512);
    out = "{ \"active\":";
    uint16_t activeId = getActiveRouteId();
    if (activeId == 0xFFFF) out += "-1"; else out += String(activeId);
    out += ",\"routes\":[";

    for (size_t i = 0; i < ids.size(); ++i) {
        uint16_t id = ids[i];

        // читаем число шагов
        char keySteps[32];
        snprintf(keySteps, sizeof(keySteps), "route_%u_steps", id);
        prefs.begin(NVS_NS, true);
        uint16_t steps = prefs.getUShort(keySteps, 0);
        prefs.end();

        if (i > 0) out += ",";
        out += "{";
        out += "\"id\":" + String(id) + ",";
        out += "\"steps\":" + String(steps);
        out += "}";
    }
    out += "]}";
    server.send(200, "application/json", out);
}

// GET /route_get?id=N  -> получить рецепт N из библиотеки
void handleRouteGet() {
    DBG_PRINTLN(F("[HTTP] GET /route_get"));
    if (!server.hasArg("id")) {
        DBG_PRINTLN(F("[HTTP]   id missing"));
        server.send(400, "text/plain", "id required");
        return;
    }
    uint16_t id = (uint16_t)server.arg("id").toInt();
    DBG_PRINTF("[HTTP]   id=%u\r\n", id);

    Step tmp[ROUTE_MAX_STEPS];
    uint16_t n = 0;
    if (!loadLibRoute(id, tmp, n)) {
        server.send(404, "text/plain", "route not found");
        return;
    }

    String out;
    out.reserve(1024);
    out = "{ \"id\":";
    out += String(id);
    out += ",\"steps\":[";

    for (uint16_t i = 0; i < n; ++i) {
        if (i > 0) out += ",";
        const Step &s = tmp[i];
        out += "{";
        out += "\"bath\":"     + String(s.bath)     + ",";
        out += "\"z_down_s\":" + String(s.z_down_s) + ",";
        out += "\"hold_s\":"   + String(s.hold_s)   + ",";
        out += "\"z_up_s\":"   + String(s.z_up_s)   + ",";
        out += "\"dry_s\":"    + String(s.dry_s)    + ",";
        out += "\"spin\":"     + String((bool)(s.flags & 1) ? "true" : "false") + ",";
        out += "\"fan\":"      + String((bool)(s.flags & 2) ? "true" : "false");
        out += "}";
    }

    out += "]}";
    server.send(200, "application/json", out);
}

// POST /route_save?id=N  body: JSON-массив шагов
void handleRouteSaveLib() {
    DBG_PRINTLN(F("[HTTP] POST /route_save"));
    if (!server.hasArg("id") || !server.hasArg("plain")) {
        DBG_PRINTLN(F("[HTTP]   id/plain missing"));
        server.send(400, "text/plain", "id and body required");
        return;
    }
    uint16_t id = (uint16_t)server.arg("id").toInt();
    String body = server.arg("plain");
    DBG_PRINTF("[HTTP]   id=%u, bodyLen=%u\r\n", id, body.length());

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        DBG_PRINTLN(F("[HTTP]   JSON error"));
        server.send(400, "text/plain", "JSON error");
        return;
    }

    JsonArray arr;
    if (doc.is<JsonArray>()) {
        arr = doc.as<JsonArray>();
    } else if (doc.containsKey("steps") && doc["steps"].is<JsonArray>()) {
        arr = doc["steps"].as<JsonArray>();
    } else {
        DBG_PRINTLN(F("[HTTP]   Expected array or {steps:[]}"));
        server.send(400, "text/plain", "Expected array or {steps:[]}");
        return;
    }

    uint16_t count = arr.size();
    DBG_PRINTF("[HTTP]   steps count=%u\r\n", count);
    if (count == 0 || count > ROUTE_MAX_STEPS) {
        server.send(400, "text/plain", "Bad steps count");
        return;
    }

    Step tmp[ROUTE_MAX_STEPS];
    for (uint16_t i = 0; i < count; ++i) {
        JsonObject v = arr[i].as<JsonObject>();
        Step s{};
        s.bath      = v["bath"]      | 0;
        s.z_down_s  = v["z_down_s"]  | 3;
        s.hold_s    = v["hold_s"]    | 0;
        s.z_up_s    = v["z_up_s"]    | 3;
        s.dry_s     = v["dry_s"]     | 5;
        bool spin   = v["spin"]      | false;
        bool fan    = v["fan"]       | false;
        s.flags = 0;
        if (spin) s.flags |= 1;
        if (fan)  s.flags |= 2;
        tmp[i] = s;
    }

    if (!saveLibRoute(id, tmp, count)) {
        DBG_PRINTLN(F("[HTTP]   NVS error on saveLibRoute"));
        server.send(500, "text/plain", "NVS error");
        return;
    }

    server.send(200, "text/plain", "OK");
}

// DELETE /route_delete?id=N
void handleRouteDelete() {
    DBG_PRINTLN(F("[HTTP] DELETE /route_delete"));
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "id required");
        return;
    }
    uint16_t id = (uint16_t)server.arg("id").toInt();
    DBG_PRINTF("[HTTP]   id=%u\r\n", id);
    deleteLibRoute(id);
    server.send(200, "text/plain", "OK");
}

// POST /route_apply?id=N  -> сделать рецепт активным
void handleRouteApply() {
    DBG_PRINTLN(F("[HTTP] POST /route_apply"));
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    int id = server.arg("id").toInt();
    DBG_PRINTF("[HTTP]   id=%d\r\n", id);

    // проверить, что шаблон существует
    prefs.begin("galva", true);
    String key = "route_" + String(id) + "_steps";
    uint16_t steps = prefs.getUShort(key.c_str(), 0);
    prefs.end();

    if (steps == 0) {
        DBG_PRINTLN(F("[HTTP]   no such route in NVS"));
        server.send(404, "text/plain", "No such route");
        return;
    }

    saveActiveRoute(id);

    bool ok = applyLibRouteAsActive(id);
    if (!ok) {
        DBG_PRINTLN(F("[HTTP]   applyLibRouteAsActive failed"));
        server.send(500, "text/plain", "Apply failed");
        return;
    }
    server.send(200, "text/plain", "OK");
}

void handleStatus() {
    DBG_PRINTLN(F("[HTTP] GET /status"));
    // Для простоты: датчики заглушки
    float temp_c = 25.0f;
    float ph     = 7.0f;

    String out;
    out.reserve(256);
    out += F("{\"time\":\"");
    out += rtcTimeString();
    out += F("\",\"date\":\"");
    out += rtcDateString();
    out += F("\",\"state\":");
    out += String((int)g_state);
    out += F(",\"state_str\":\"");
    out += stateToString(g_state);
    out += F("\",\"bath\":");
    out += String(g_currentBath);
    out += F(",\"step_idx\":");
    out += String(g_stepIdx);
    out += F(",\"steps\":");
    out += String(g_routeSteps);
    out += F(",\"temp_c\":");
    out += String(temp_c, 1);
    out += F(",\"ph\":");
    out += String(ph, 2);
    out += F(",\"active_route\":");
    out += String(getActiveRouteId());
    out += F("}");

    server.send(200, "application/json", out);
}

void handleStart() {
    DBG_PRINTLN(F("[HTTP] POST /start"));
    if (g_state == PS_IDLE && g_routeSteps > 0) {
        g_startCommand = true;
        DBG_PRINTLN(F("[HTTP]   startCommand set"));
        server.send(200, "text/plain", "OK");
    } else {
        DBG_PRINTLN(F("[HTTP]   BUSY or no recipe"));
        server.send(409, "text/plain", "BUSY or no recipe");
    }
}

void handleStop() {
    DBG_PRINTLN(F("[HTTP] POST /stop"));
    g_stopCommand = true;
    server.send(200, "text/plain", "STOP requested");
}

void handleRoutePost() {
    DBG_PRINTLN(F("[HTTP] POST /route (active)"));
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "No body");
        return;
    }
    String body = server.arg("plain");
    DBG_PRINTF("[HTTP]   bodyLen=%u\r\n", body.length());

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        DBG_PRINTLN(F("[HTTP]   JSON error"));
        server.send(400, "text/plain", "JSON error");
        return;
    }
    if (!doc.is<JsonArray>()) {
        DBG_PRINTLN(F("[HTTP]   Expected array"));
        server.send(400, "text/plain", "Expected array");
        return;
    }
    JsonArray arr = doc.as<JsonArray>();
    uint16_t count = arr.size();
    DBG_PRINTF("[HTTP]   steps count=%u\r\n", count);
    if (count == 0 || count > ROUTE_MAX_STEPS) {
        server.send(400, "text/plain", "Bad steps count");
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        JsonObject v = arr[i].as<JsonObject>();
        Step s{};
        s.bath      = v["bath"]      | 0;
        s.z_down_s  = v["z_down_s"]  | 3;
        s.hold_s    = v["hold_s"]    | 0;
        s.z_up_s    = v["z_up_s"]    | 3;
        s.dry_s     = v["dry_s"]     | 5;
        bool spin   = v["spin_on"]   | false;
        bool fan    = v["fan_on"]    | false;
        s.flags = 0;
        if (spin) s.flags |= 1;
        if (fan)  s.flags |= 2;
        g_route[i] = s;
    }
    g_routeSteps = count;
    DBG_PRINTF("[HTTP]   active route updated, steps=%u\r\n", g_routeSteps);
    saveRoute(g_route, g_routeSteps);  // в слот 0
    server.send(200, "text/plain", "OK");
}

void handleBathHistory() {
    DBG_PRINTLN(F("[HTTP] GET /bath_history"));
    if(!server.hasArg("bath")){
        server.send(400,"text/plain","bath param required");
        return;
    }
    int b = server.arg("bath").toInt();
    DBG_PRINTF("[HTTP]   bath=%d\r\n", b);
    if (b < 0 || b >= dynamicBathCount) {
    server.send(400, "text/plain", "invalid bath");
    return;
}
    String out;
    out.reserve(2048);
    out = "[";

    int p = histPtr[b];
    for(int i=0;i<HIST_LEN;i++){
        int idx = (p+i+1)%HIST_LEN;
        if(i>0) out += ",";
        out += "{";
        out += "\"temp\":" + String(hist[b][idx].temp) + ",";
        out += "\"ph\":" + String(hist[b][idx].ph) + ",";
        out += "\"orp\":" + String(hist[b][idx].orp) + ",";
        out += "\"current\":" + String(hist[b][idx].current);
        out += "}";
    }
    out += "]";

    server.send(200,"application/json",out);
}

void handleBathData() {
    DBG_PRINTLN(F("[HTTP] GET /bath_data"));

 const int N = dynamicBathCount;


    String out;
    out.reserve(1024);
    out = "{\"baths\":[";

    for(int i=0;i<N;i++){
        if(i>0) out += ",";
        out += "{";
        out += "\"id\":" + String(i) + ",";
        out += "\"temp\":" + String(25.0 + i*0.3) + ",";
        out += "\"ph\":" + String(7.0 + i*0.01) + ",";
        out += "\"orp\":" + String(500 + i*3) + ",";
        out += "\"current\":" + String(120 + i*5);
        out += "}";
    }
    out += "]}";

    server.send(200, "application/json", out);
}

// ---------------- ФУНКЦИЯ СМЕНЫ СОСТОЯНИЯ ----------------

// Все переходы состояний идут через эту функцию — для логирования
void setState(ProcState newState, const char* reason) {
    static ProcState prev = PS_IDLE;
    if (newState != prev) {
        DBG_PRINTF("[STATE] %s: %d -> %d (%s)\r\n",
                   rtcTimeString().c_str(),
                   (int)prev,
                   (int)newState,
                   reason ? reason : "");
        prev = newState;
    }
    g_state = newState;
}

// ---------------- TASKS ----------------
void TaskProcess(void* pv) {
    (void)pv;
    DBG_PRINTLN(F("[TASK] TaskProcess started"));
    for (;;) {
        g_currentBath = bathIndex;

        // История ванн (пока синтетика, раз в 3 секунды)
        static uint32_t lastHist = 0;
        if (millis() - lastHist > 3000) {
            lastHist = millis();
         for (int b = 0; b < dynamicBathCount; b++)
 {
                int p = histPtr[b] = (histPtr[b] + 1) % HIST_LEN;
                hist[b][p].temp    = 25.0 + b * 0.2;
                hist[b][p].ph      = 7.00;
                hist[b][p].orp     = 500 + b * 3;
                hist[b][p].current = 100 + b * 4;
            }
        }

        // аварийный стоп
        if (digitalRead(PIN_ESTOP) == LOW) {
            relOffAll();
            setState(PS_ERROR, "ESTOP pressed");
        }

        if (g_stopCommand) {
            DBG_PRINTLN(F("[TASK] stopCommand received"));
            g_stopCommand = false;
            relOffAll();
            setState(PS_ERROR, "STOP command");
        }

        switch (g_state) {
               case PS_IDLE:
            if (g_startCommand) {
                DBG_PRINTLN(F("[FSM] Start command in IDLE"));
                g_startCommand = false;
                g_stepIdx = -1;
                setState(PS_HOMING, "Start from IDLE");
            }
            break;

        case PS_HOMING: {
            DBG_PRINTLN(F("[FSM] HOMING (go to bath 0 by RFID)"));

            // Едем к ванне 0, считая метку RFID
            bool ok = moveToBath(0, 30000);

            if (ok) {
                DBG_PRINTLN(F("[FSM] HOMING success, bathIndex=0"));
                bathIndex     = 0;
                g_currentBath = 0;
                g_stepIdx     = 0;
                setState(PS_MOVE_X, "Homing completed");
            } else {
                DBG_PRINTLN(F("[FSM] HOMING failed"));
                setState(PS_ERROR, "Homing failed");
            }
        } break;

            case PS_MOVE_X: {
                DBG_PRINTF("[FSM] MOVE_X, stepIdx=%d of %d\r\n", (int)g_stepIdx, (int)g_routeSteps);
                if (g_stepIdx >= g_routeSteps) {
                    setState(PS_RETURN_HOME, "No more steps");
                    break;
                }
                Step &st = g_route[g_stepIdx];
                DBG_PRINTF("[FSM]   target bath=%u\r\n", st.bath);
                bool ok = moveToBath(st.bath, 30000);
                setState(ok ? PS_LOWER_Z : PS_ERROR, ok ? "moveToBath OK" : "moveToBath FAIL");
            } break;

            case PS_LOWER_Z: {
                DBG_PRINTF("[FSM] LOWER_Z, stepIdx=%d\r\n", (int)g_stepIdx);
                Step &st = g_route[g_stepIdx];
                bool ok = zDown_for(st.z_down_s);
                setState(ok ? PS_HOLD : PS_ERROR, ok ? "zDown OK" : "zDown FAIL");
            } break;

            case PS_HOLD: {
                DBG_PRINTF("[FSM] HOLD, stepIdx=%d\r\n", (int)g_stepIdx);
                Step &st = g_route[g_stepIdx];
                if (st.flags & 1) {
                    DBG_PRINTLN(F("[FSM]   SPIN ON"));
                    digitalWrite(REL_SPIN, ON);
                }

                uint32_t t0 = millis();
                while (millis() - t0 < (uint32_t)st.hold_s * 1000UL) {
                    if (digitalRead(PIN_ESTOP) == LOW || g_stopCommand) {
                        DBG_PRINTLN(F("[FSM]   HOLD interrupted by stop/ESTOP"));
                        setState(PS_ERROR, "HOLD interrupted");
                        break;
                    }
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
                digitalWrite(REL_SPIN, OFF);
                if (g_state != PS_ERROR) {
                    setState(PS_RAISE_Z, "HOLD completed");
                }
            } break;

            case PS_RAISE_Z: {
                DBG_PRINTF("[FSM] RAISE_Z, stepIdx=%d\r\n", (int)g_stepIdx);
                Step &st = g_route[g_stepIdx];
                bool ok = zUp_for(st.z_up_s);
                setState(ok ? PS_DRY : PS_ERROR, ok ? "zUp OK" : "zUp FAIL");
            } break;

            case PS_DRY: {
                DBG_PRINTF("[FSM] DRY, stepIdx=%d\r\n", (int)g_stepIdx);
                Step &st = g_route[g_stepIdx];
                if (st.flags & 2) {
                    DBG_PRINTLN(F("[FSM]   FAN ON"));
                    digitalWrite(REL_FAN, ON);
                }

                uint32_t t0 = millis();
                while (millis() - t0 < (uint32_t)st.dry_s * 1000UL) {
                    if (digitalRead(PIN_ESTOP) == LOW || g_stopCommand) {
                        DBG_PRINTLN(F("[FSM]   DRY interrupted by stop/ESTOP"));
                        setState(PS_ERROR, "DRY interrupted");
                        break;
                    }
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                }
                digitalWrite(REL_FAN, OFF);
                if (g_state != PS_ERROR) {
                    setState(PS_NEXT_STEP, "DRY completed");
                }
            } break;

            case PS_NEXT_STEP:
                DBG_PRINTF("[FSM] NEXT_STEP, stepIdx=%d\r\n", (int)g_stepIdx);
                g_stepIdx++;
                if (g_stepIdx >= g_routeSteps) {
                    DBG_PRINTLN(F("[FSM]   no more steps, returning home"));
                    setState(PS_RETURN_HOME, "All steps done");
                } else {
                    setState(PS_MOVE_X, "Next step");
                }
                break;

            case PS_RETURN_HOME: {
                DBG_PRINTLN(F("[FSM] RETURN_HOME"));
                bool ok = moveToBath(0, 30000);
                relOffAll();
                setState(ok ? PS_IDLE : PS_ERROR, ok ? "Return OK" : "Return FAIL");
            } break;

            case PS_ERROR:
                // в ошибке всё выключено, ждём сброса
                relOffAll();
                if (startButtonPressed()) {
                    DBG_PRINTLN(F("[FSM] ERROR reset by START button"));
                    setState(PS_IDLE, "Reset from error");
                }
                break;
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void TaskUI(void* pv) {
    (void)pv;
    DBG_PRINTLN(F("[TASK] TaskUI started"));
    for (;;) {
        if (g_inSelectMenu) {
            // Режим выбора шаблона
            oledShowSelectMenu();

            int d = getEncDelta();
            if (d != 0) {
                g_selectedRoute += d;
                if (g_selectedRoute < 0) g_selectedRoute = ROUTE_SLOTS - 1;
                if (g_selectedRoute >= ROUTE_SLOTS) g_selectedRoute = 0;
                DBG_PRINTF("[UI] menu changed, selectedRoute=%d\r\n", (int)g_selectedRoute);
            }

            // Подтверждение выбором — кнопка START
            if (startButtonPressed()) {
                DBG_PRINTF("[UI] menu confirm, loading slot=%d\r\n", (int)g_selectedRoute);
                if (loadRouteSlot((uint8_t)g_selectedRoute)) {
                    DBG_PRINTLN(F("[UI]   route loaded from slot"));
                } else {
                    DBG_PRINTLN(F("[UI]   loadRouteSlot failed"));
                }
                g_inSelectMenu = false;
            }

        } else {
            // обычные экраны
            if (g_state == PS_IDLE) {
                oledShowIdle();

                // старт процесса по кнопке
                if (startButtonPressed() && g_routeSteps > 0) {
                    DBG_PRINTLN(F("[UI] START from IDLE"));
                    g_startCommand = true;
                }

                // энкодер в IDLE => вход в меню
                int d = getEncDelta();
                if (d != 0) {
                    DBG_PRINTLN(F("[UI] enter select menu"));
                    g_inSelectMenu = true;
                }
            } else {
                oledShowRun();
            }
        }

        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
}

void TaskWeb(void* pv) {
    (void)pv;
    DBG_PRINTLN(F("[TASK] TaskWeb started"));
    for (;;) {
        server.handleClient();
        vTaskDelay(1);
    }
}

// ---------------- SETUP/LOOP ----------------

void setup() {

    Serial.begin(115200);
    delay(300);

    Wire.begin(I2C_SDA, I2C_SCL);  // 1) запускаем шину I2C

    oledInit();    // 2) OLED ОБЯЗАТЕЛЬНО первым

    Serial.println("DEBUG: calling nfcInit()");

    nfcInit();     // 3) потом PN532

    // =============== NFC TEST MODE ===============
// Если удерживать кнопку START при включении — входим в NFC TEST MODE
// if (digitalRead(BTN_START) == LOW) {
//     Serial.println("[TEST] START held -> NFC TEST MODE");
//     delay(300);
//     nfcTestLoop();  // <<< запуск теста (БЛОКИРУЮЩИЙ)
// }

       // Инициализация таблиц
    initBathTables();          // очистка истории и g_baths (пусто)
    loadBathListFromNVS();     // загрузка ванн из NVS

    // Если хочешь гарантированный минимум "логических" ванн – можно создать пустые слоты
    if (dynamicBathCount < MIN_BATHS) {
        for (int i = dynamicBathCount; i < MIN_BATHS; ++i) {
            if ((int)g_baths.size() >= MAX_BATHS_LIMIT) break;
            BathInfo b;
            b.bathNumber = (uint16_t)i;
            b.uidLen     = 0;
            memset(b.uid, 0, sizeof(b.uid));
            b.isStart = (i == 0);                 // можно сразу пометить нулевую как старт
            b.isEnd   = false;
            g_baths.push_back(b);
        }
        dynamicBathCount = (int)g_baths.size();
        saveBathListToNVS();
    }
    

    Serial.printf("[SETUP] dynamicBathCount = %d\n", dynamicBathCount);


    rtc.Begin();   // потом RTC
      DBG_PRINTLN(F("[SETUP] RTC init"));
    if (!rtc.IsDateTimeValid()) {
        DBG_PRINTLN(F("[SETUP] RTC invalid, set from compile time"));
        rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
    } else {
        DBG_PRINTLN(F("[SETUP] RTC time OK"));
    }

    DBG_PRINTLN();
    DBG_PRINTLN(F("===== GalvaControl ESP32 boot ====="));

    // GPIO
    DBG_PRINTLN(F("[SETUP] GPIO init"));
    pinMode(REL_X_FWD, OUTPUT);
    pinMode(REL_X_REV, OUTPUT);
    pinMode(REL_Z_UP, OUTPUT);
    pinMode(REL_Z_DOWN, OUTPUT);
    pinMode(REL_SPIN, OUTPUT);
    pinMode(REL_FAN, OUTPUT);
    relOffAll();

    pinMode(SW_Z_TOP, INPUT_PULLUP);
    pinMode(SW_Z_BOTTOM, INPUT_PULLUP);
    pinMode(PIN_ESTOP, INPUT_PULLUP);
    pinMode(BTN_START, INPUT_PULLUP);

    pinMode(ENC_A, INPUT);
    pinMode(ENC_B, INPUT);
    pinMode(ENC_SW, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(ENC_A),    encISR,  CHANGE);
    DBG_PRINTLN(F("[SETUP] GPIO & interrupts configured"));

    // запустить тест NFC
    //nfcTestLoop();

    // загрузка активного маршрута id
    prefs.begin("galva", true);
    g_activeRoute = prefs.getShort("active_route", -1);
    prefs.end();
    DBG_PRINTF("[SETUP] activeRoute from NVS: %d\r\n", (int)g_activeRoute);

    // Инициализация слотов шаблонов: создадим тестовый в слоте 0, если пусто
    DBG_PRINTLN(F("[SETUP] Load slot 0"));
    if (!loadRouteSlot(0)) {
        DBG_PRINTLN(F("[SETUP] slot 0 empty, create demo route"));
        Step demo[3];
        demo[0] = { 1, 4, 3, 3, 5, 1 }; // ванна 1, 4с выдержка, крутилка
        demo[1] = { 2, 5, 3, 3, 6, 0 }; // ванна 2, без вращения/фена
        demo[2] = { 1, 3, 3, 3, 4, 2 }; // ванна 1, только фен

        saveRouteSlot(0, demo, 3);
        memcpy(g_route, demo, sizeof(demo));
        g_routeSteps = 3;
        g_selectedRoute = 0;
    } else {
        DBG_PRINTLN(F("[SETUP] slot 0 loaded"));
        // слот 0 уже был — подгрузили
        loadRouteSlot(0);
        g_selectedRoute = 0;
    }
    DBG_PRINTF("[SETUP] g_routeSteps=%u\r\n", g_routeSteps);

    // WiFi AP
    DBG_PRINTLN(F("[SETUP] WiFi AP start"));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress ip = WiFi.softAPIP();
    DBG_PRINTF("[SETUP] AP IP: %s\r\n", ip.toString().c_str());
    Serial.print(F("AP IP: "));
    Serial.println(ip);

    // HTTP routes
    DBG_PRINTLN(F("[SETUP] HTTP routes init"));
    server.on("/",          HTTP_GET,  handleRoot);
    server.on("/status",    HTTP_GET,  handleStatus);
    server.on("/start",     HTTP_POST, handleStart);
    server.on("/stop",      HTTP_POST, handleStop);
    server.on("/route",     HTTP_POST, handleRoutePost);
    server.on("/monitor",   HTTP_GET,  []() {
        DBG_PRINTLN(F("[HTTP] GET /monitor"));
        server.send_P(200, "text/html", WEB_UI_MONITOR);
    });
    server.on("/bath_data",    HTTP_GET, handleBathData);
    server.on("/bath_history", HTTP_GET, handleBathHistory);
    server.on("/routes_list",  HTTP_GET,  handleRoutesList);
    server.on("/route_get",    HTTP_GET,  handleRouteGet);
    server.on("/route_save",   HTTP_POST, handleRouteSaveLib);
    server.on("/route_delete", HTTP_DELETE, handleRouteDelete);
    server.on("/route_apply",  HTTP_POST,  handleRouteApply);

    // страница редактора шаблонов
    server.on("/routes_ui", HTTP_GET, []() {
        DBG_PRINTLN(F("[HTTP] GET /routes_ui"));
        server.send_P(200, "text/html", WEB_UI_ROUTES);
    });
    server.begin();
    DBG_PRINTLN(F("[SETUP] HTTP server started"));

    // FreeRTOS задачи
    DBG_PRINTLN(F("[SETUP] Create tasks"));
    xTaskCreatePinnedToCore(TaskProcess, "Process", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(TaskWeb,     "Web",     4096, nullptr, 1, nullptr, 0);

    DBG_PRINTLN(F("===== GalvaControl setup done ====="));
}

void loop() {
    // Всё работает в задачах FreeRTOS
    vTaskDelay(portMAX_DELAY);
}
