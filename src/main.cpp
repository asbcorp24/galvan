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
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RtcDS1302.h>
#include <ArduinoJson.h>
#include <vector>
#include "web_ui.h"
#include "web_monitor.h"
#include "web_routes.h"
#include "web_baths.h"
#include "web_autolearn.h"
#include "web_logs.h"
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_PN532.h>
#include "web_layout.h"
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
#define ENABLE_PN532_X_STARTUP 1

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
#define SW_Z_TOP     0
#define SW_Z_BOTTOM  13

#define PN532_X_SCK    25
#define PN532_X_MISO   39
#define PN532_X_MOSI   27
#define PN532_X_SS     14
#define PN532_Z_RX     36   // TX пина PN532 Z -> RX ESP32
#define PN532_Z_TX     33   // RX пина PN532 Z <- TX ESP32
#define PN532_Z_RESET  255
// Аварийный стоп
#define PIN_ESTOP  26

// Кнопка START (дубль веб-старта)
#define BTN_START  13

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
volatile int16_t shadowBath = 0;  // промежуточный ожидаемый номер ванны
static const uint8_t ROUTE_MAX_STEPS = 50;
static const char*  NVS_NS          = "galva";
volatile int16_t g_targetBath = -1;

// ----------- NVS STORAGE FOR BATH TAGS ---------------
// Ключи в NVS
static const char* NVS_KEY_BATH_COUNT = "bath_cnt";
static const char* NVS_KEY_BATH_TAGS  = "bath_tags";
static const char* NVS_KEY_START_POINT = "start_pt";
static const char* NVS_KEY_END_POINT   = "end_pt";
static const char* NVS_KEY_Z_START_POINT = "z_start_pt";
static const char* NVS_KEY_Z_END_POINT   = "z_end_pt";
static const char* NVS_KEY_ZTAG_COUNT  = "ztag_cnt";
static const char* NVS_KEY_ZTAG_DATA   = "ztag_data";

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

enum LinePointKind : uint8_t {
    LPK_PROCESS = 0,
    LPK_START   = 1,
    LPK_END     = 2,
    LPK_SERVICE = 3
};

enum LastDetectedKind : uint8_t {
    LDK_NONE    = 0,
    LDK_PROCESS = 1,
    LDK_START   = 2,
    LDK_END     = 3,
    LDK_Z_LEVEL = 4,
    LDK_Z_START = 5,
    LDK_Z_END   = 6
};

struct ServicePointInfo {
    uint8_t uidLen;
    uint8_t uid[7];
    bool configured;
};

struct ZTagInfo {
    int16_t level;
    uint8_t uidLen;
    uint8_t uid[7];
};

struct LastDetectedInfo {
    uint8_t kind;
    int16_t number;
    uint8_t uidLen;
    uint8_t uid[7];
};

struct ActionLogEntry {
    uint32_t seq;
    String date;
    String time;
    String level;
    String category;
    String message;
};

// Описание рабочей ванны с RFID
struct BathInfo {
    uint16_t bathNumber;   // логический номер ванны (для маршрутов)
    uint8_t  uidLen;       // длина UID
    uint8_t  uid[7];       // сам UID (до 7 байт)
    bool     isStart;      // флаг "начальная ванна"
    bool     isEnd;        // флаг "конечная ванна"
};

// Динамический список ванн
std::vector<BathInfo> g_baths;
std::vector<ZTagInfo> g_zTags;
ServicePointInfo g_startPoint = {};
ServicePointInfo g_endPoint   = {};
ServicePointInfo g_zStartPoint = {};
ServicePointInfo g_zEndPoint   = {};
LastDetectedInfo g_lastDetected = {};
volatile int16_t g_currentZLevel = -1;
static const size_t ACTION_LOG_CAPACITY = 80;
ActionLogEntry g_actionLog[ACTION_LOG_CAPACITY];
size_t g_actionLogHead = 0;
size_t g_actionLogCount = 0;
uint32_t g_actionLogSeq = 0;


// слоты шаблонов
static const uint8_t ROUTE_SLOTS = 5;
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ---------------- ТИПЫ ----------------

// Описание одного шага (одной ванны) в рецепте
#pragma pack(push,1)
struct Step {
    uint16_t bath;
    uint16_t z_level_down;
    uint16_t z_down_timeout_s;
    uint16_t hold_s;
    uint16_t z_level_up;
    uint16_t z_up_timeout_s;
    uint16_t dry_s;
    uint16_t flags; // bit0=spin, bit1=fan
};

struct LegacyStep {
    uint16_t bath;
    uint16_t hold_s;
    uint16_t z_down_s;
    uint16_t z_up_s;
    uint16_t dry_s;
    uint16_t flags;
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
    PS_ERROR,
    PS_LEARN_BATHS 
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

// ---------------- FORWARD DECLARATIONS ----------------

void relOffAll();
void xFwd(bool en);
void xRev(bool en);
void zUpRelay(bool en);
void zDownRelay(bool en);
void addActionLog(const char *level, const char *category, const String &message);
String rtcTimeString();
String rtcDateString();
bool moveToBathIndex(int targetBathIndex, uint32_t timeoutMs);
bool moveToServicePoint(const ServicePointInfo &point, const char *label, bool isStartPoint, bool moveRight, uint32_t timeoutMs);
bool moveToZServicePoint(const ServicePointInfo &point, const char *label, bool isStartPoint, bool moveDown, uint32_t timeoutMs);
bool moveZToLevel(int16_t targetLevel, bool moveDown, uint16_t timeoutSec);


HardwareSerial pn532ZSerial(2);
Adafruit_PN532 nfcX(PN532_X_SCK, PN532_X_MISO, PN532_X_MOSI, PN532_X_SS);
Adafruit_PN532 nfcZ(PN532_Z_RESET, &pn532ZSerial);
bool nfcXReady = false;
bool nfcZReady = false;



// Меню выбора шаблона
volatile int16_t g_selectedRoute = 0;   // 0..ROUTE_SLOTS-1
volatile bool    g_inSelectMenu  = false;
enum UiMode : uint8_t {
    UI_HOME = 0,
    UI_MAIN_MENU,
    UI_ROUTE_SELECT,
    UI_SETTINGS
};
volatile UiMode g_uiMode = UI_HOME;
volatile int16_t g_menuIndex = 0;

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

struct ZTagRecordNVS {
    int16_t  level;
    uint8_t  uidLen;
    uint8_t  uid[7];
};
// Преобразовать UID в hex-строку без пробелов
String uidToHexString(const uint8_t *uid, uint8_t len) {
    String s;
    s.reserve(len * 2);
    for (uint8_t i = 0; i < len; ++i) {
        if (uid[i] < 16) s += "0";
        s += String(uid[i], HEX);
    }
    s.toUpperCase();
    return s;
}

void debugPrintUid(const char *prefix, const uint8_t *uid, uint8_t uidLen) {
    DBG_PRINT(prefix ? prefix : "[RFID] UID");
    DBG_PRINT(": ");
    if (!uid || uidLen == 0) {
        DBG_PRINTLN("(none)");
        return;
    }
    for (uint8_t i = 0; i < uidLen; ++i) {
        DBG_PRINTF("%02X", uid[i]);
        if (i + 1 < uidLen) DBG_PRINT(" ");
    }
    DBG_PRINTLN("");
}

bool i2cDevicePresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void scanI2CBus(const char *label) {
    DBG_PRINTF("[I2C] scan start: %s\r\n", label ? label : "bus");
    int found = 0;
    DBG_PRINTF("[I2C] scan done: found=%d\r\n", found);
}

bool readPassiveTargetX(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs = 50) {
    if (!nfcXReady) return false;
    return nfcX.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, timeoutMs);
}

bool readPassiveTargetZ(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs = 50) {
    if (!nfcZReady) return false;
    return nfcZ.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, timeoutMs);
}

bool waitForTagRelease(Adafruit_PN532 &reader, bool ready, const char *ctx, uint32_t timeoutMs) {
    if (!ready) return false;
    uint32_t t0 = millis();
    uint8_t missCount = 0;
    while (millis() - t0 < timeoutMs) {
        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (!reader.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 30)) {
            missCount++;
            if (missCount >= 3) {
                DBG_PRINTF("[RFID] %s tag released\r\n", ctx ? ctx : "unknown");
                return true;
            }
        } else {
            missCount = 0;
        }
        delay(30);
    }
    DBG_PRINTF("[RFID] %s release wait timeout\r\n", ctx ? ctx : "unknown");
    return false;
}

bool readUidWithTimeout(Adafruit_PN532 &reader, bool ready, const char *ctx, uint8_t *uid, uint8_t *uidLen, uint32_t timeoutMs) {
    if (!uid || !uidLen) return false;
    *uidLen = 0;
    if (!ready) {
        DBG_PRINTF("[RFID] %s: reader not ready\r\n", ctx ? ctx : "read");
        return false;
    }

    DBG_PRINTF("[RFID] %s: waiting for tag, timeout=%lu ms\r\n",
               ctx ? ctx : "read",
               (unsigned long)timeoutMs);

    const uint32_t t0 = millis();
    uint32_t lastLog = 0;
    while (millis() - t0 < timeoutMs) {
        if (reader.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 50)) {
            DBG_PRINTF("[RFID] %s: tag detected, uidLen=%u\r\n",
                       ctx ? ctx : "read",
                       (unsigned)*uidLen);
            debugPrintUid("[RFID] UID", uid, *uidLen);
            return true;
        }

        if (millis() - lastLog >= 500) {
            lastLog = millis();
            DBG_PRINTF("[RFID] %s: still waiting...\r\n", ctx ? ctx : "read");
        }
        delay(30);
    }

    DBG_PRINTF("[RFID] %s: timeout, tag not detected\r\n", ctx ? ctx : "read");
    return false;
}

void clearServicePoint(ServicePointInfo &point) {
    point.uidLen = 0;
    memset(point.uid, 0, sizeof(point.uid));
    point.configured = false;
}

void setServicePoint(ServicePointInfo &point, const uint8_t *uid, uint8_t uidLen) {
    clearServicePoint(point);
    if (uidLen == 0 || uidLen > 7) return;
    point.uidLen = uidLen;
    memcpy(point.uid, uid, uidLen);
    point.configured = true;
}

bool servicePointMatches(const ServicePointInfo &point, const uint8_t *uid, uint8_t uidLen) {
    if (!point.configured || point.uidLen != uidLen) return false;
    for (uint8_t i = 0; i < uidLen; ++i) {
        if (point.uid[i] != uid[i]) return false;
    }
    return true;
}

void noteLastDetected(uint8_t kind, int16_t number, const uint8_t *uid, uint8_t uidLen) {
    g_lastDetected.kind = kind;
    g_lastDetected.number = number;
    g_lastDetected.uidLen = uidLen > 7 ? 7 : uidLen;
    memset(g_lastDetected.uid, 0, sizeof(g_lastDetected.uid));
    if (g_lastDetected.uidLen > 0) {
        memcpy(g_lastDetected.uid, uid, g_lastDetected.uidLen);
    }
}

void addActionLog(const char *level, const char *category, const String &message) {
    ActionLogEntry &entry = g_actionLog[g_actionLogHead];
    entry.seq = ++g_actionLogSeq;
    entry.date = rtcDateString();
    entry.time = rtcTimeString();
    entry.level = level ? level : "info";
    entry.category = category ? category : "system";
    entry.message = message;

    g_actionLogHead = (g_actionLogHead + 1) % ACTION_LOG_CAPACITY;
    if (g_actionLogCount < ACTION_LOG_CAPACITY) {
        g_actionLogCount++;
    }
}

void saveServicePointToNVS(const char *key, const ServicePointInfo &point) {
    prefs.begin(NVS_NS, false);
    prefs.putBytes(key, &point, sizeof(ServicePointInfo));
    prefs.end();
}

void loadServicePointFromNVS(const char *key, ServicePointInfo &point) {
    clearServicePoint(point);
    prefs.begin(NVS_NS, true);
    size_t have = prefs.getBytesLength(key);
    if (have >= sizeof(ServicePointInfo)) {
        prefs.getBytes(key, &point, sizeof(ServicePointInfo));
    }
    prefs.end();
    if (point.uidLen > 7) {
        clearServicePoint(point);
    }
}

void saveZTagsToNVS() {
    prefs.begin(NVS_NS, false);
    uint16_t count = (uint16_t)g_zTags.size();
    prefs.putUShort(NVS_KEY_ZTAG_COUNT, count);
    if (count == 0) {
        prefs.remove(NVS_KEY_ZTAG_DATA);
        prefs.end();
        return;
    }

    ZTagRecordNVS records[MAX_BATHS_LIMIT];
    for (uint16_t i = 0; i < count && i < MAX_BATHS_LIMIT; ++i) {
        records[i].level = g_zTags[i].level;
        records[i].uidLen = g_zTags[i].uidLen;
        memset(records[i].uid, 0, sizeof(records[i].uid));
        if (g_zTags[i].uidLen > 0 && g_zTags[i].uidLen <= 7) {
            memcpy(records[i].uid, g_zTags[i].uid, g_zTags[i].uidLen);
        }
    }
    prefs.putBytes(NVS_KEY_ZTAG_DATA, records, count * sizeof(ZTagRecordNVS));
    prefs.end();
}

void loadZTagsFromNVS() {
    g_zTags.clear();
    prefs.begin(NVS_NS, true);
    uint16_t count = prefs.getUShort(NVS_KEY_ZTAG_COUNT, 0);
    size_t have = prefs.getBytesLength(NVS_KEY_ZTAG_DATA);
    if (count == 0 || count > MAX_BATHS_LIMIT || have < count * sizeof(ZTagRecordNVS)) {
        prefs.end();
        return;
    }

    ZTagRecordNVS records[MAX_BATHS_LIMIT];
    prefs.getBytes(NVS_KEY_ZTAG_DATA, records, count * sizeof(ZTagRecordNVS));
    prefs.end();

    g_zTags.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (records[i].uidLen == 0 || records[i].uidLen > 7) continue;
        ZTagInfo z{};
        z.level = records[i].level;
        z.uidLen = records[i].uidLen;
        memcpy(z.uid, records[i].uid, z.uidLen);
        g_zTags.push_back(z);
    }
}
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

void migrateLegacyServiceFlagsIfNeeded() {
    bool changed = false;
    if (g_startPoint.configured && g_endPoint.configured) return;

    for (const auto &b : g_baths) {
        if (!g_startPoint.configured && b.isStart && b.uidLen > 0) {
            setServicePoint(g_startPoint, b.uid, b.uidLen);
            changed = true;
        }
        if (!g_endPoint.configured && b.isEnd && b.uidLen > 0) {
            setServicePoint(g_endPoint, b.uid, b.uidLen);
            changed = true;
        }
    }

    if (changed) {
        if (g_startPoint.configured) saveServicePointToNVS(NVS_KEY_START_POINT, g_startPoint);
        if (g_endPoint.configured) saveServicePointToNVS(NVS_KEY_END_POINT, g_endPoint);
    }
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

int findBathStorageIndexByNumber(uint16_t bathNumber) {
    for (int i = 0; i < (int)g_baths.size(); ++i) {
        if (g_baths[i].bathNumber == bathNumber) {
            return i;
        }
    }
    return -1;
}

int findBathStorageIndexByFlag(bool wantStart) {
    for (int i = 0; i < (int)g_baths.size(); ++i) {
        if (wantStart && g_baths[i].isStart) return i;
        if (!wantStart && g_baths[i].isEnd) return i;
    }
    return -1;
}

uint16_t getBathDisplayNumberByIndex(int storageIndex) {
    if (storageIndex >= 0 && storageIndex < (int)g_baths.size()) {
        return g_baths[storageIndex].bathNumber;
    }
    return (storageIndex >= 0) ? (uint16_t)storageIndex : 0;
}

int findZTagIndexByUID(const uint8_t uid[], uint8_t uidLen) {
    if (uidLen == 0 || uidLen > 7) return -1;
    for (int i = 0; i < (int)g_zTags.size(); ++i) {
        if (g_zTags[i].uidLen != uidLen) continue;
        bool same = true;
        for (uint8_t j = 0; j < uidLen; ++j) {
            if (g_zTags[i].uid[j] != uid[j]) {
                same = false;
                break;
            }
        }
        if (same) return i;
    }
    return -1;
}

int findZTagIndexByLevel(int16_t level) {
    for (int i = 0; i < (int)g_zTags.size(); ++i) {
        if (g_zTags[i].level == level) return i;
    }
    return -1;
}

bool captureKnownZLevel(const uint8_t uid[], uint8_t uidLen) {
    int zIdx = findZTagIndexByUID(uid, uidLen);
    if (zIdx < 0) return false;
    g_currentZLevel = g_zTags[zIdx].level;
    noteLastDetected(LDK_Z_LEVEL, g_currentZLevel, uid, uidLen);
    DBG_PRINTF("[MOVE_Z] Detected Z level=%d\r\n", (int)g_currentZLevel);
    return true;
}

bool captureKnownZServicePoint(const uint8_t uid[], uint8_t uidLen, bool &isStart, bool &isEnd) {
    isStart = servicePointMatches(g_zStartPoint, uid, uidLen);
    isEnd = servicePointMatches(g_zEndPoint, uid, uidLen);
    if (isStart) {
        noteLastDetected(LDK_Z_START, -1, uid, uidLen);
        DBG_PRINTLN(F("[MOVE_Z] Detected Z START service point"));
    }
    if (isEnd) {
        noteLastDetected(LDK_Z_END, -1, uid, uidLen);
        DBG_PRINTLN(F("[MOVE_Z] Detected Z END service point"));
    }
    return isStart || isEnd;
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
bool decodeRouteBytes(const void *raw, size_t haveBytes, uint16_t stepCount, Step *dst);

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
    size_t have = prefs.getBytesLength(keyData);
    DBG_PRINTF("[NVS]  need(new)=%u, need(old)=%u, have=%u\r\n",
               (unsigned)(n * sizeof(Step)),
               (unsigned)(n * sizeof(LegacyStep)),
               (unsigned)have);
    if (have == 0) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  not enough data"));
        return false;
    }
    uint8_t rawBuf[ROUTE_MAX_STEPS * sizeof(Step)] = {0};
    prefs.getBytes(keyData, rawBuf, have > sizeof(rawBuf) ? sizeof(rawBuf) : have);
    prefs.end();

    if (!decodeRouteBytes(rawBuf, have, n, g_route)) {
        DBG_PRINTLN(F("[NVS]  route bytes incompatible"));
        return false;
    }

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

bool decodeRouteBytes(const void *raw, size_t haveBytes, uint16_t stepCount, Step *dst) {
    const size_t needNew = stepCount * sizeof(Step);
    if (haveBytes >= needNew) {
        memcpy(dst, raw, needNew);
        return true;
    }

    const size_t needLegacy = stepCount * sizeof(LegacyStep);
    if (haveBytes >= needLegacy) {
        const LegacyStep *legacy = static_cast<const LegacyStep*>(raw);
        for (uint16_t i = 0; i < stepCount; ++i) {
            dst[i].bath = legacy[i].bath;
            dst[i].z_level_down = 1;
            dst[i].z_down_timeout_s = legacy[i].z_down_s;
            dst[i].hold_s = legacy[i].hold_s;
            dst[i].z_level_up = 0;
            dst[i].z_up_timeout_s = legacy[i].z_up_s;
            dst[i].dry_s = legacy[i].dry_s;
            dst[i].flags = legacy[i].flags;
        }
        return true;
    }

    return false;
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
    size_t have = prefs.getBytesLength(keyData);
    if (have == 0) {
        prefs.end();
        DBG_PRINTLN(F("[NVS]  not enough bytes in NVS"));
        return false;
    }
    uint8_t rawBuf[ROUTE_MAX_STEPS * sizeof(Step)] = {0};
    prefs.getBytes(keyData, rawBuf, have > sizeof(rawBuf) ? sizeof(rawBuf) : have);
    prefs.end();

    if (!decodeRouteBytes(rawBuf, have, n, dst)) {
        DBG_PRINTLN(F("[NVS]  route bytes incompatible"));
        return false;
    }
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
    DBG_PRINTF("[PINMAP] nfcX SPI: SCK=%d MISO=%d MOSI=%d SS=%d\r\n",
               PN532_X_SCK, PN532_X_MISO, PN532_X_MOSI, PN532_X_SS);
    DBG_PRINTF("[PINMAP] nfcZ: RX=%d TX=%d RST=%d\r\n",
               PN532_Z_RX, PN532_Z_TX, PN532_Z_RESET);

#if ENABLE_PN532_X_STARTUP
    DBG_PRINTLN(F("[PN532-X] init start (SPI)"));
    pinMode(PN532_X_SS, OUTPUT);
    digitalWrite(PN532_X_SS, HIGH);
    delay(50);

    DBG_PRINTLN(F("[PN532-X] begin()..."));
    nfcX.begin();
    DBG_PRINTLN(F("[PN532-X] begin() returned"));

    uint32_t versiondata = 0;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        versiondata = nfcX.getFirmwareVersion();
        DBG_PRINTF("[PN532-X] getFirmwareVersion attempt %d = 0x%08lX\r\n",
                   attempt, (unsigned long)versiondata);
        if (versiondata) break;
        delay(150);
    }

    if (versiondata) {
        DBG_PRINTF("[PN532-X] Chip: 0x%02X, Ver: %u.%u\r\n",
                   (uint8_t)(versiondata >> 24),
                   (uint8_t)((versiondata >> 16) & 0xFF),
                   (uint8_t)((versiondata >> 8)  & 0xFF));
        DBG_PRINTLN(F("[PN532-X] setPassiveActivationRetries..."));
        nfcX.setPassiveActivationRetries(0x05);
        DBG_PRINTLN(F("[PN532-X] setPassiveActivationRetries done"));
        nfcXReady = true;
        DBG_PRINTLN(F("[PN532-X] init done"));
    } else {
        DBG_PRINTLN(F("[PN532-X] firmware read failed"));
    }
#else
    DBG_PRINTLN(F("[PN532-X] startup init skipped (temporary workaround)"));
#endif

    DBG_PRINTLN(F("[PN532-Z] init start (HSU/UART2)"));
    pn532ZSerial.begin(115200, SERIAL_8N1, PN532_Z_RX, PN532_Z_TX);
    if (nfcZ.begin()) {
        uint32_t versiondata = nfcZ.getFirmwareVersion();
        if (versiondata) {
            DBG_PRINTF("[PN532-Z] Chip: 0x%02X, Ver: %u.%u\r\n",
                       (uint8_t)(versiondata >> 24),
                       (uint8_t)((versiondata >> 16) & 0xFF),
                       (uint8_t)((versiondata >> 8)  & 0xFF));
            nfcZ.setPassiveActivationRetries(0x05);
            nfcZ.SAMConfig();
            nfcZReady = true;
            DBG_PRINTLN(F("[PN532-Z] init done"));
        } else {
            DBG_PRINTLN(F("[PN532-Z] firmware read failed"));
        }
    } else {
        DBG_PRINTLN(F("[PN532-Z] begin() failed"));
    }

    DBG_PRINTF("[PN532] ready state: X=%d Z=%d\r\n", (int)nfcXReady, (int)nfcZReady);
}

// Чтение RFID-метки.
// Возвращает true, если метка найдена и сопоставлена с какой-то ванной.
// detectedBath — индекс ванны (0..dynamicBathCount-1)
bool readTag(int &detectedBath)
{
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    bool success = readPassiveTargetX(uid, &uidLength);
    if (!success) return false;

    // выводим UID
    DBG_PRINT("[PN532-X] UID = ");
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
// ------------------ RFID helper ------------------
bool uidEquals(const uint8_t* a, const uint8_t* b, uint8_t len) {
    for (uint8_t i = 0; i < len; i++)
        if (a[i] != b[i]) return false;
    return true;
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

    if (!oledReady) {
        Serial.println("[NFC] OLED not ready, skipping display output");
    }

    if (oledReady) oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.println("NFC TEST MODE");
    oled.println("Поднесите метку...");
    oled.display();

    while (true) {
        uint8_t uid[7] = {0};
        uint8_t uidLength = 0;

        bool success = readPassiveTargetX(uid, &uidLength);
        if (success) {
            Serial.print("[NFC] UID: ");
            for (uint8_t i = 0; i < uidLength; i++) {
                Serial.printf("%02X ", uid[i]);
            }
            Serial.println();

            if (oledReady) {
                oled.clearDisplay();
                oled.setCursor(0, 0);
                oled.println("NFC UID FOUND:");
                for (uint8_t i = 0; i < uidLength; i++) {
                    oled.printf("%02X ", uid[i]);
                }
                oled.display();
            }

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
void oledShowLearn(int foundCount, uint8_t* uid, uint8_t uidLen) {
    if (!oledReady) return;
    oled.clearDisplay();
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);

    u8g2.setCursor(0, 10);
    u8g2.print("ОБУЧЕНИЕ ВАНН");

    u8g2.setCursor(0, 25);
    u8g2.print("Найдено: ");
    u8g2.print(foundCount);

    u8g2.setCursor(0, 40);
    u8g2.print("UID: ");
    for (int i = 0; i < uidLen; i++) {
        if (uid[i] < 16) u8g2.print("0");
        u8g2.print(uid[i], HEX);
        u8g2.print(" ");
    }

    // анимация движения →
    static int pos = 0;
    const char* anim = "=>     ";
    u8g2.setCursor(0, 58);
    u8g2.print(anim + (pos++ % 3));

    oled.display();
}

void oledInit() {
    DBG_PRINTLN(F("[OLED] init start"));

    // проверяем шину I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

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
    u8g2.begin(oled);
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
    if (!oledReady) return;
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    oled.clearDisplay();

    uint16_t activeRouteId = getActiveRouteId();

    u8g2.setCursor(0, 10);
    u8g2.print(rtcTimeString());

    u8g2.setCursor(0, 24);
    u8g2.print(F("IDLE"));
    u8g2.setCursor(44, 24);
    u8g2.print(F("R:"));
    if (activeRouteId == 0xFFFF) u8g2.print(F("-"));
    else u8g2.print(activeRouteId);

    u8g2.setCursor(0, 38);
    u8g2.print(F("B:"));
    u8g2.print(g_currentBath);
    u8g2.setCursor(44, 38);
    u8g2.print(F("ST:"));
    u8g2.print(g_routeSteps);

    u8g2.setCursor(0, 52);
    u8g2.print(F("Z:"));
    if (g_currentZLevel >= 0) u8g2.print(g_currentZLevel);
    else u8g2.print(F("-"));

    u8g2.setCursor(0, 62);
    u8g2.print(F("BTN=MENU"));
    oled.display();
}

void oledShowRun() {
    if (!oledReady) return;
    oled.clearDisplay();
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);

    const __FlashStringHelper* stateLabel = F("RUN");
    switch (g_state) {
        case PS_HOMING:      stateLabel = F("HOME"); break;
        case PS_MOVE_X:      stateLabel = F("MOVE X"); break;
        case PS_LOWER_Z:     stateLabel = F("DOWN"); break;
        case PS_HOLD:        stateLabel = F("HOLD"); break;
        case PS_RAISE_Z:     stateLabel = F("UP"); break;
        case PS_DRY:         stateLabel = F("DRY"); break;
        case PS_RETURN_HOME: stateLabel = F("RETURN"); break;
        case PS_ERROR:       stateLabel = F("ERROR"); break;
        default:             break;
    }

    u8g2.setCursor(0, 10);
    u8g2.print(rtcTimeString());

    u8g2.setCursor(0, 24);
    u8g2.print(stateLabel);

    u8g2.setCursor(0, 38);
    u8g2.print(F("STEP:"));
    u8g2.print(g_stepIdx + 1);
    u8g2.print(F("/"));
    u8g2.print(g_routeSteps);
    u8g2.setCursor(72, 38);
    u8g2.print(F("B:"));
    u8g2.print(g_currentBath);

    u8g2.setCursor(0, 52);
    u8g2.print(F("TO:"));
    if (g_targetBath >= 0) u8g2.print(g_targetBath);
    else u8g2.print(g_currentBath);
    u8g2.setCursor(72, 52);
    u8g2.print(F("Z:"));
    if (g_currentZLevel >= 0) u8g2.print(g_currentZLevel);
    else u8g2.print(F("-"));

    u8g2.setCursor(0, 62);
    u8g2.print(F("STOP=BTN"));
    oled.display();
}

void oledShowSelectMenu() {
    if (!oledReady) return;
    DBG_PRINTF("[OLED] show select menu, selected=%d\r\n", (int)g_selectedRoute);

    oled.clearDisplay();
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);

    const int lineHeight = 14;
    const int maxVisible = 3;
    const int startY = 24;

    oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    oled.setTextColor(SSD1306_WHITE);
    u8g2.setCursor(8, 12);
    u8g2.print(F("ROUTE SELECT"));

    int total = ROUTE_SLOTS;
    int first = 0;

    if (g_selectedRoute > 0) {
        first = g_selectedRoute - 1;
        if (first > total - maxVisible)
            first = total - maxVisible;
        if (first < 0) first = 0;
    }

    for (int i = 0; i < maxVisible; i++) {
        int idx = first + i;
        if (idx >= total) break;

        int yText = startY + i * lineHeight;
        bool selected = (idx == g_selectedRoute);

        if (selected) {
            oled.drawRect(4, yText - 11, 120, lineHeight, SSD1306_WHITE);
        }

        oled.setTextColor(SSD1306_WHITE);
        u8g2.setCursor(8, yText);
        u8g2.print(F("R"));
        u8g2.print(idx);
        if ((uint8_t)idx == (uint8_t)g_selectedRoute) u8g2.print(F(" *"));
    }

    oled.display();
}

void oledShowMainMenu() {
    if (!oledReady) return;
    static const char* items[] = {"START", "ROUTE", "STATUS"};
    const int itemCount = 3;

    oled.clearDisplay();
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    u8g2.setCursor(8, 12);
    u8g2.print(F("MAIN MENU"));

    for (int i = 0; i < itemCount; ++i) {
        const int y = 24 + i * 13;
        if (i == g_menuIndex) {
            oled.drawRect(4, y - 10, 120, 12, SSD1306_WHITE);
        }
        u8g2.setCursor(8, y);
        u8g2.print(items[i]);
    }

    oled.display();
}

void oledShowSettings() {
    if (!oledReady) return;
    uint16_t activeRouteId = getActiveRouteId();
    IPAddress ip = WiFi.softAPIP();

    oled.clearDisplay();
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);

    u8g2.setCursor(0, 10);
    u8g2.print(F("STATUS"));

    u8g2.setCursor(0, 24);
    u8g2.print(F("R:"));
    if (activeRouteId == 0xFFFF) u8g2.print(F("-"));
    else u8g2.print(activeRouteId);
    u8g2.setCursor(44, 24);
    u8g2.print(F("B:"));
    u8g2.print(dynamicBathCount);

    u8g2.setCursor(0, 36);
    u8g2.print(F("Z:"));
    if (g_currentZLevel >= 0) u8g2.print(g_currentZLevel);
    else u8g2.print(F("-"));
    u8g2.print(F("/"));
    u8g2.print((int)g_zTags.size());
    u8g2.setCursor(44, 36);
    u8g2.print(F("ST:"));
    u8g2.print(g_routeSteps);

    u8g2.setCursor(0, 48);
    u8g2.print(F("S:"));
    u8g2.print(g_startPoint.configured ? F("OK") : F("--"));
    u8g2.setCursor(44, 48);
    u8g2.print(F("E:"));
    u8g2.print(g_endPoint.configured ? F("OK") : F("--"));

    u8g2.setCursor(0, 60);
    u8g2.print(F("IP:"));
    u8g2.print(ip);
    oled.display();
}

// ---------------- ЭНКОДЕР + КНОПКА ----------------
// ---------------- ???? ----------------

void relOffAll() {
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

/*bool moveToBath(int targetBath, uint32_t timeoutMs)
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
}*/
// Улучшенное движение к ванне по RFID
bool moveToBathIndex(int targetBathIndex, uint32_t timeoutMs)
{
    DBG_PRINTF("\n[MOVE_X] >>> moveToBathIndex(targetIndex=%d, currentIndex=%d)\n",
               targetBathIndex, bathIndex);
    shadowBath = bathIndex;  // стартуем с текущей подтверждённой точki
    g_targetBath = getBathDisplayNumberByIndex(targetBathIndex);
    uint32_t t0 = millis();

    if (targetBathIndex == bathIndex) {
        DBG_PRINTLN("[MOVE_X] Already at target bath");
        xFwd(false);
        xRev(false);
        g_currentBath = getBathDisplayNumberByIndex(bathIndex);
        return true;
    }

    // направление
    dirRight = (targetBathIndex > bathIndex);
    DBG_PRINTF("[MOVE_X] Direction = %s\n", dirRight ? "RIGHT" : "LEFT");

    if (dirRight) {
        xRev(false);
        xFwd(true);
    } else {
        xFwd(false);
        xRev(true);
    }

    // переменные стабилизации UID
    uint8_t lastUid[7] = {0};
    uint8_t lastLen = 0;
    int stableCount = 0;

    const int STABLE_REQUIRED = 2;  // нужно 2 подряд одинаковых чтения UID
    uint8_t uid[7];
    uint8_t uidLen;

    while (millis() - t0 < timeoutMs)
    {
        // --- Проверка аварийного стопа ---
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN("[MOVE_X] ESTOP -> STOP MOVEMENT");
            break;
        }

        int detectedBath = -1;

        // читаем RFID
        bool ok = readPassiveTargetX(uid, &uidLen);

        if (ok) {
            DBG_PRINT("[MOVE_X] Saw UID: ");
            for (uint8_t i = 0; i < uidLen; i++) DBG_PRINTF("%02X ", uid[i]);
            DBG_PRINTLN("");

            // ищем ванну
            detectedBath = findBathIndexByUID(uid, uidLen);

            if (detectedBath >= 0) {
                DBG_PRINTF("[MOVE_X] → detected bath index = %d\n", detectedBath);

                // антидребезг: проверка, что UID стабилен
                if (uidLen == lastLen && uidEquals(uid, lastUid, uidLen)) {
                    stableCount++;
                } else {
                    memcpy(lastUid, uid, uidLen);
                    lastLen = uidLen;
                    stableCount = 1;
                }

                DBG_PRINTF("[MOVE_X] StableCount = %d/%d\n",
                           stableCount, STABLE_REQUIRED);

                if (stableCount >= STABLE_REQUIRED) {
                    // стабильное распознавание ванны
                    bathIndex = detectedBath;
                    g_currentBath = getBathDisplayNumberByIndex(detectedBath);
                    shadowBath = detectedBath;     // ← синхронизируем
                    noteLastDetected(LDK_PROCESS, g_currentBath, uid, uidLen);
                    if (detectedBath == targetBathIndex) {
                        DBG_PRINTLN("[MOVE_X] *** ARRIVED at target bath! ***");
                        xFwd(false);
                        xRev(false);
                        return true;
                    }
                }
            } else {
                DBG_PRINTLN("[MOVE_X] UID not registered to any bath");
            }
        }

//////
if (dirRight) {
    // двигаемся вправо – ванны увеличиваются
    shadowBath++;
} else {
    // двигаемся влево – ванны уменьшаются
    shadowBath--;
}

// Ограничиваем диапазон
if (shadowBath < 0) shadowBath = 0;
if (shadowBath >= dynamicBathCount) shadowBath = dynamicBathCount - 1;

// Выводим промежуточную ванну
g_currentBath = getBathDisplayNumberByIndex(shadowBath);

/////
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // timeout или останов
    DBG_PRINTLN("[MOVE_X] TIMEOUT or STOP");
    xFwd(false);
    xRev(false);
    return false;
}

bool encoderButtonPressed() {
    static uint32_t last = 0;
    if (digitalRead(ENC_SW) == LOW) {
        uint32_t now = millis();
        if (now - last > 250) {
            last = now;
            DBG_PRINTLN(F("[BTN] ENC_SW pressed"));
            return true;
        }
    }
    return false;
}

bool moveToBathNumber(uint16_t bathNumber, uint32_t timeoutMs) {
    int targetIndex = findBathStorageIndexByNumber(bathNumber);
    if (targetIndex < 0) {
        DBG_PRINTF("[MOVE_X] bathNumber=%u not found in config\r\n", bathNumber);
        return false;
    }
    return moveToBathIndex(targetIndex, timeoutMs);
}

bool moveToServicePoint(const ServicePointInfo &point, const char *label, bool isStartPoint, bool moveRight, uint32_t timeoutMs) {
    if (!point.configured || point.uidLen == 0) {
        DBG_PRINTF("[MOVE_X] %s point is not configured\r\n", label ? label : "service");
        return false;
    }

    uint32_t t0 = millis();
    shadowBath = bathIndex;
    g_targetBath = -1;
    dirRight = moveRight;
    if (moveRight) {
        xRev(false);
        xFwd(true);
    } else {
        xFwd(false);
        xRev(true);
    }

    uint8_t lastUid[7] = {0};
    uint8_t lastLen = 0;
    int stableCount = 0;
    const int STABLE_REQUIRED = 2;

    while (millis() - t0 < timeoutMs) {
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN("[MOVE_X] ESTOP while moving to service point");
            break;
        }

        uint8_t uid[7];
        uint8_t uidLen = 0;
        if (readPassiveTargetX(uid, &uidLen)) {
            int detectedBath = findBathIndexByUID(uid, uidLen);
            if (detectedBath >= 0) {
                bathIndex = detectedBath;
                shadowBath = detectedBath;
                g_currentBath = getBathDisplayNumberByIndex(detectedBath);
            }

            if (uidLen == lastLen && uidEquals(uid, lastUid, uidLen)) {
                stableCount++;
            } else {
                memcpy(lastUid, uid, uidLen);
                lastLen = uidLen;
                stableCount = 1;
            }

            if (stableCount >= STABLE_REQUIRED && servicePointMatches(point, uid, uidLen)) {
                xFwd(false);
                xRev(false);
                noteLastDetected(isStartPoint ? LDK_START : LDK_END, -1, uid, uidLen);
                DBG_PRINTF("[MOVE_X] Arrived at %s point\r\n", label ? label : "service");
                return true;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    xFwd(false);
    xRev(false);
    return false;
}

bool moveToStartPoint(uint32_t timeoutMs) {
    return moveToServicePoint(g_startPoint, "start", true, false, timeoutMs);
}

bool moveToZServicePoint(const ServicePointInfo &point, const char *label, bool isStartPoint, bool moveDown, uint32_t timeoutMs) {
    if (!point.configured) {
        DBG_PRINTF("[MOVE_Z] %s point is not configured\r\n", label ? label : "z_service");
        return false;
    }

    uint8_t lastUid[7] = {0};
    uint8_t lastLen = 0;
    int stableCount = 0;
    const int STABLE_REQUIRED = 2;
    const uint32_t t0 = millis();

    if (moveDown) {
        zUpRelay(false);
        zDownRelay(true);
    } else {
        zDownRelay(false);
        zUpRelay(true);
    }

    while (millis() - t0 < timeoutMs) {
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] ESTOP while moving to Z service point"));
            zDownRelay(false);
            zUpRelay(false);
            return false;
        }

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (readPassiveTargetZ(uid, &uidLen)) {
            captureKnownZLevel(uid, uidLen);

            if (uidLen == lastLen && uidEquals(uid, lastUid, uidLen)) {
                stableCount++;
            } else {
                memcpy(lastUid, uid, uidLen);
                lastLen = uidLen;
                stableCount = 1;
            }

            if (stableCount >= STABLE_REQUIRED && servicePointMatches(point, uid, uidLen)) {
                noteLastDetected(isStartPoint ? LDK_Z_START : LDK_Z_END, -1, uid, uidLen);
                zDownRelay(false);
                zUpRelay(false);
                DBG_PRINTF("[MOVE_Z] Reached %s\r\n", label ? label : "z_service");
                return true;
            }
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    zDownRelay(false);
    zUpRelay(false);
    DBG_PRINTF("[MOVE_Z] Timeout while moving to %s\r\n", label ? label : "z_service");
    return false;
}

bool moveToZStartPoint(uint32_t timeoutMs) {
    return moveToZServicePoint(g_zStartPoint, "z_start", true, false, timeoutMs);
}

bool moveToZEndPoint(uint32_t timeoutMs) {
    return moveToZServicePoint(g_zEndPoint, "z_end", false, true, timeoutMs);
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

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (readPassiveTargetZ(uid, &uidLen)) {
            captureKnownZLevel(uid, uidLen);
            bool isStart = false, isEnd = false;
            captureKnownZServicePoint(uid, uidLen, isStart, isEnd);
            if (isEnd) {
                DBG_PRINTLN(F("[MOVE_Z] reached Z END service point"));
                zDownRelay(false);
                return true;
            }
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

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (readPassiveTargetZ(uid, &uidLen)) {
            captureKnownZLevel(uid, uidLen);
            bool isStart = false, isEnd = false;
            captureKnownZServicePoint(uid, uidLen, isStart, isEnd);
            if (isStart) {
                DBG_PRINTLN(F("[MOVE_Z] reached Z START service point"));
                zUpRelay(false);
                return true;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    zUpRelay(false);
    bool ok = (millis() - t0 < timeout);
    DBG_PRINTF("[MOVE_Z] zUp_for done, ok=%d\r\n", (int)ok);
    return ok;
}

bool moveZToLevel(int16_t targetLevel, bool moveDown, uint16_t timeoutSec) {
    const bool haveTargetLevel = (targetLevel >= 0) && (findZTagIndexByLevel(targetLevel) >= 0);
    const uint32_t timeoutMs = (uint32_t)(timeoutSec > 0 ? timeoutSec : 1) * 1000UL;

    DBG_PRINTF("[MOVE_Z] moveZToLevel target=%d dir=%s timeout=%us haveTarget=%d\r\n",
               (int)targetLevel,
               moveDown ? "DOWN" : "UP",
               (unsigned)timeoutSec,
               haveTargetLevel ? 1 : 0);

    if (haveTargetLevel && g_currentZLevel == targetLevel) {
        DBG_PRINTLN(F("[MOVE_Z] Already at target Z level"));
        return true;
    }

    if (!haveTargetLevel) {
        DBG_PRINTLN(F("[MOVE_Z] Target Z level not configured, fallback to timeout motion"));
        return moveDown ? zDown_for(timeoutSec) : zUp_for(timeoutSec);
    }

    const uint32_t t0 = millis();
    if (moveDown) {
        zUpRelay(false);
        zDownRelay(true);
    } else {
        zDownRelay(false);
        zUpRelay(true);
    }

    while (millis() - t0 < timeoutMs) {
        if (digitalRead(PIN_ESTOP) == LOW) {
            DBG_PRINTLN(F("[MOVE_Z] ESTOP during moveZToLevel"));
            break;
        }

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (readPassiveTargetZ(uid, &uidLen)) {
            captureKnownZLevel(uid, uidLen);
            bool isStart = false, isEnd = false;
            captureKnownZServicePoint(uid, uidLen, isStart, isEnd);
            if (g_currentZLevel == targetLevel) {
                zDownRelay(false);
                zUpRelay(false);
                DBG_PRINTLN(F("[MOVE_Z] Target Z level reached"));
                return true;
            }
            if (moveDown && isEnd) {
                DBG_PRINTLN(F("[MOVE_Z] Reached Z END before target level"));
                zDownRelay(false);
                return false;
            }
            if (!moveDown && isStart) {
                DBG_PRINTLN(F("[MOVE_Z] Reached Z START before target level"));
                zUpRelay(false);
                return false;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    zDownRelay(false);
    zUpRelay(false);
    DBG_PRINTLN(F("[MOVE_Z] Timeout waiting for target Z level, using fallback timed motion"));
    return moveDown ? zDown_for(timeoutSec) : zUp_for(timeoutSec);
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
        case PS_LEARN_BATHS: return F("LEARN_BATHS");   // 👈
        default:             return F("UNKNOWN");
    }
}

String directionXToString() {
    return dirRight ? F("right") : F("left");
}

String currentActionString() {
    switch (g_state) {
        case PS_IDLE: return F("Ожидание");
        case PS_HOMING: return F("Поиск стартовой точки");
        case PS_MOVE_X: return F("Движение по X");
        case PS_LOWER_Z: return F("Опускание по Z");
        case PS_HOLD: return F("Выдержка в ванне");
        case PS_RAISE_Z: return F("Подъем по Z");
        case PS_DRY: return F("Сушка / постобработка");
        case PS_NEXT_STEP: return F("Переход к следующему шагу");
        case PS_RETURN_HOME: return F("Возврат домой");
        case PS_ERROR: return F("Ошибка / останов");
        case PS_LEARN_BATHS: return F("Автокалибровка ванн");
        default: return F("Неизвестно");
    }
}

String currentWaitString() {
    switch (g_state) {
        case PS_IDLE: return F("Команда Старт");
        case PS_HOMING: return F("RFID метка Start");
        case PS_MOVE_X: return F("RFID целевой ванны");
        case PS_LOWER_Z: return F("Z уровень / Z End / таймаут");
        case PS_HOLD: return F("Окончание таймера выдержки");
        case PS_RAISE_Z: return F("Z уровень / Z Start / таймаут");
        case PS_DRY: return F("Окончание таймера сушки");
        case PS_NEXT_STEP: return F("Следующий шаг маршрута");
        case PS_RETURN_HOME: return F("RFID метка Start");
        case PS_ERROR: return F("Сброс ошибки / новый старт");
        case PS_LEARN_BATHS: return F("RFID ванны / RFID End");
        default: return F("Неизвестно");
    }
}

void handleBathsAutoLearn() {
    DBG_PRINTLN(F("[HTTP] POST /baths_autolearn"));
      DBG_PRINTF("Калибруем ванны...\n");
    // Разрешаем калибровку ТОЛЬКО из IDLE
    if (g_state != PS_IDLE) {
        DBG_PRINTF("[HTTP]  cannot start LEARN, state=%s\n",
                   stateToString(g_state).c_str());
        server.send(409, "text/plain", "BUSY");
        return;
    }

    g_stopCommand  = false;
    g_startCommand = false;

    DBG_PRINTLN(F("[HTTP]  switching to PS_LEARN_BATHS"));
    setState(PS_LEARN_BATHS, "HTTP /baths_autolearn");

    server.send(200, "text/plain", "OK");
}

void handleBathsList() {
    DBG_PRINTLN(F("[HTTP] GET /baths_list"));

    String out;
    out.reserve(2048);
    out = "{\"count\":";
    out += String(dynamicBathCount);
    out += ",\"current_z_level\":";
    out += String(g_currentZLevel);
    out += ",\"baths\":[";

    for (int i = 0; i < dynamicBathCount; ++i) {
        if (i > 0) out += ",";
        const BathInfo &b = g_baths[i];

        out += "{";
        out += "\"index\":" + String(i) + ",";
        out += "\"bathNumber\":" + String(b.bathNumber) + ",";
        out += "\"uid_len\":" + String(b.uidLen) + ",";

        if (b.uidLen > 0 && b.uidLen <= 7) {
            out += "\"uid_hex\":\"" + uidToHexString(b.uid, b.uidLen) + "\",";
        } else {
            out += "\"uid_hex\":\"\",";
        }

        out += "\"isStart\":" + String(b.isStart ? "true" : "false") + ",";
        out += "\"isEnd\":"   + String(b.isEnd   ? "true" : "false");
        out += "}";
    }

    out += "],\"service_points\":{";
    out += "\"start\":{\"configured\":";
    out += String(g_startPoint.configured ? "true" : "false");
    out += ",\"uid_hex\":\"";
    out += (g_startPoint.configured ? uidToHexString(g_startPoint.uid, g_startPoint.uidLen) : "");
    out += "\"},\"end\":{\"configured\":";
    out += String(g_endPoint.configured ? "true" : "false");
    out += ",\"uid_hex\":\"";
    out += (g_endPoint.configured ? uidToHexString(g_endPoint.uid, g_endPoint.uidLen) : "");
    out += "\"},\"z_start\":{\"configured\":";
    out += String(g_zStartPoint.configured ? "true" : "false");
    out += ",\"uid_hex\":\"";
    out += (g_zStartPoint.configured ? uidToHexString(g_zStartPoint.uid, g_zStartPoint.uidLen) : "");
    out += "\"},\"z_end\":{\"configured\":";
    out += String(g_zEndPoint.configured ? "true" : "false");
    out += ",\"uid_hex\":\"";
    out += (g_zEndPoint.configured ? uidToHexString(g_zEndPoint.uid, g_zEndPoint.uidLen) : "");
    out += "\"}},\"z_tags\":[";

    for (size_t i = 0; i < g_zTags.size(); ++i) {
        if (i > 0) out += ",";
        out += "{";
        out += "\"level\":" + String(g_zTags[i].level) + ",";
        out += "\"uid_hex\":\"" + uidToHexString(g_zTags[i].uid, g_zTags[i].uidLen) + "\"";
        out += "}";
    }

    out += "]}";
    server.send(200, "application/json", out);
}
void handleBathsUpdate() {
    DBG_PRINTLN(F("[HTTP] POST /baths_update"));

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

    if (!doc.containsKey("baths") || !doc["baths"].is<JsonArray>()) {
        server.send(400, "text/plain", "Expected {baths:[]}");
        return;
    }

    JsonArray arr = doc["baths"].as<JsonArray>();
    for (JsonObject v : arr) {
        int idx = v["index"] | -1;
        if (idx < 0 || idx >= dynamicBathCount) continue;

        BathInfo &b = g_baths[idx];
        b.bathNumber = v["bathNumber"] | b.bathNumber;
        b.isStart = false;
        b.isEnd = false;
    }

    saveBathListToNVS();
    server.send(200, "text/plain", "OK");
}

void handleBathAdd() {
    DBG_PRINTLN(F("[HTTP] POST /baths_add"));

    if (g_state != PS_IDLE) {
        server.send(409, "text/plain", "BUSY");
        return;
    }
    if ((int)g_baths.size() >= MAX_BATHS_LIMIT) {
        server.send(400, "text/plain", "Bath limit reached");
        return;
    }

    uint16_t nextBathNumber = 1;
    for (const auto &b : g_baths) {
        if (b.bathNumber >= nextBathNumber) {
            nextBathNumber = b.bathNumber + 1;
        }
    }

    BathInfo b{};
    b.bathNumber = nextBathNumber;
    b.uidLen = 0;
    memset(b.uid, 0, sizeof(b.uid));
    b.isStart = false;
    b.isEnd = false;
    g_baths.push_back(b);

    dynamicBathCount = (int)g_baths.size();
    saveBathListToNVS();
    server.send(200, "text/plain", "OK");
}

void handleBathDelete() {
    DBG_PRINTLN(F("[HTTP] POST /baths_delete"));

    if (g_state != PS_IDLE) {
        server.send(409, "text/plain", "BUSY");
        return;
    }
    if (!server.hasArg("index")) {
        server.send(400, "text/plain", "index required");
        return;
    }

    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= (int)g_baths.size()) {
        server.send(400, "text/plain", "bad index");
        return;
    }

    g_baths.erase(g_baths.begin() + idx);
    dynamicBathCount = (int)g_baths.size();

    if (dynamicBathCount <= 0) {
        bathIndex = 0;
        shadowBath = 0;
        g_currentBath = 0;
    } else {
        if (bathIndex >= dynamicBathCount) bathIndex = dynamicBathCount - 1;
        if (shadowBath >= dynamicBathCount) shadowBath = dynamicBathCount - 1;
        if (shadowBath < 0) shadowBath = 0;
        g_currentBath = getBathDisplayNumberByIndex(bathIndex);
    }

    saveBathListToNVS();
    server.send(200, "text/plain", "OK");
}

void handleServicePointLearn() {
    DBG_PRINTLN(F("[HTTP] POST /service_point_learn"));
    if (!server.hasArg("kind")) {
        server.send(400, "text/plain", "kind required");
        return;
    }

    String kind = server.arg("kind");
    ServicePointInfo *target = nullptr;

    DBG_PRINTF("[RFID] service learn requested for kind=%s\r\n", kind.c_str());

    uint8_t uid[7] = {0};
    uint8_t uidLen = 0;
    Adafruit_PN532 *reader = &nfcX;
    bool readerReady = nfcXReady;
    const char *saveKey = nullptr;
    if (kind == "start") {
        target = &g_startPoint;
        saveKey = NVS_KEY_START_POINT;
    } else if (kind == "end") {
        target = &g_endPoint;
        saveKey = NVS_KEY_END_POINT;
    } else if (kind == "z_start") {
        target = &g_zStartPoint;
        saveKey = NVS_KEY_Z_START_POINT;
        reader = &nfcZ;
        readerReady = nfcZReady;
    } else if (kind == "z_end") {
        target = &g_zEndPoint;
        saveKey = NVS_KEY_Z_END_POINT;
        reader = &nfcZ;
        readerReady = nfcZReady;
    }

    if (!target || !saveKey) {
        server.send(400, "text/plain", "invalid kind");
        return;
    }

    if (readUidWithTimeout(*reader, readerReady, kind.c_str(), uid, &uidLen, 5000UL)) {
        setServicePoint(*target, uid, uidLen);
        saveServicePointToNVS(saveKey, *target);
        uint8_t detectedKind =
            kind == "start" ? LDK_START :
            kind == "end" ? LDK_END :
            kind == "z_start" ? LDK_Z_START : LDK_Z_END;
        noteLastDetected(detectedKind, -1, uid, uidLen);
        DBG_PRINTF("[RFID] service point %s saved to NVS\r\n", kind.c_str());
        waitForTagRelease(*reader, readerReady, kind.c_str(), 1500UL);

        String resp = "{\"ok\":true,\"kind\":\"" + kind + "\",\"uid_hex\":\"" + uidToHexString(uid, uidLen) + "\"}";
        server.send(200, "application/json", resp);
        return;
    }

    server.send(408, "application/json", "{\"ok\":false,\"error\":\"timeout: tag not detected\"}");
}

void handleZTagLearn() {
    DBG_PRINTLN(F("[HTTP] POST /z_tag_learn"));
    if (!server.hasArg("level")) {
        server.send(400, "text/plain", "level required");
        return;
    }

    int16_t level = (int16_t)server.arg("level").toInt();
    DBG_PRINTF("[RFID] Z learn requested for level=%d\r\n", (int)level);
    uint8_t uid[7] = {0};
    uint8_t uidLen = 0;
    if (readUidWithTimeout(nfcZ, nfcZReady, "z_level", uid, &uidLen, 5000UL)) {
        int existingIdx = findZTagIndexByLevel(level);
        ZTagInfo z{};
        z.level = level;
        z.uidLen = uidLen > 7 ? 7 : uidLen;
        memcpy(z.uid, uid, z.uidLen);

        if (existingIdx >= 0) {
            DBG_PRINTF("[RFID] replacing existing Z tag at index=%d\r\n", existingIdx);
            g_zTags[existingIdx] = z;
        } else if ((int)g_zTags.size() < MAX_BATHS_LIMIT) {
            DBG_PRINTF("[RFID] adding new Z tag, count before=%u\r\n", (unsigned)g_zTags.size());
            g_zTags.push_back(z);
        }

        saveZTagsToNVS();
        g_currentZLevel = level;
        noteLastDetected(LDK_Z_LEVEL, level, uid, uidLen);
        DBG_PRINTF("[RFID] Z level %d saved to NVS\r\n", (int)level);
        waitForTagRelease(nfcZ, nfcZReady, "z_level", 1500UL);

        String resp = "{\"ok\":true,\"level\":";
        resp += String(level);
        resp += ",\"uid_hex\":\"";
        resp += uidToHexString(uid, uidLen);
        resp += "\"}";
        server.send(200, "application/json", resp);
        return;
    }

    server.send(408, "application/json", "{\"ok\":false,\"error\":\"timeout: tag not detected\"}");
}

void handleZAutoLearn() {
    DBG_PRINTLN(F("[HTTP] POST /z_autolearn"));

    if (g_state != PS_IDLE) {
        server.send(409, "text/plain", "BUSY");
        return;
    }
    if (!nfcZReady) {
        server.send(500, "text/plain", "Z reader not ready");
        return;
    }

    int16_t startLevel = 0;
    if (server.hasArg("start")) {
        startLevel = (int16_t)server.arg("start").toInt();
    }
    DBG_PRINTF("[Z-AUTO] startLevel=%d\r\n", (int)startLevel);

    std::vector<ZTagInfo> oldTags = g_zTags;
    int16_t oldLevel = g_currentZLevel;

    if (!g_zStartPoint.configured || !g_zEndPoint.configured) {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"z start/end not configured\"}");
        return;
    }
    if (servicePointMatches(g_zStartPoint, g_zEndPoint.uid, g_zEndPoint.uidLen)) {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"z start/end identical\"}");
        return;
    }

    DBG_PRINTLN(F("[Z-AUTO] moving to Z START before scan"));
    if (!moveToZStartPoint(30000)) {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"cannot reach z start\"}");
        return;
    }

    g_zTags.clear();
    g_currentZLevel = -1;

    uint8_t lastUid[7] = {0};
    uint8_t lastLen = 0;
    int stable = 0;
    int16_t nextLevel = startLevel;
    const uint32_t t0 = millis();
    const uint32_t timeoutMs = 60000UL;
    bool leftStartZone = false;

    zUpRelay(false);
    zDownRelay(true);

    while (millis() - t0 < timeoutMs) {
        if (digitalRead(PIN_ESTOP) == LOW || g_stopCommand) {
            DBG_PRINTLN(F("[Z-AUTO] interrupted by stop/ESTOP"));
            zDownRelay(false);
            g_zTags = oldTags;
            g_currentZLevel = oldLevel;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"stopped\"}");
            return;
        }

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (readPassiveTargetZ(uid, &uidLen)) {
            bool isStart = false, isEnd = false;
            captureKnownZServicePoint(uid, uidLen, isStart, isEnd);

            if (isStart) {
                if (!leftStartZone) {
                    vTaskDelay(40 / portTICK_PERIOD_MS);
                    continue;
                }
                DBG_PRINTLN(F("[Z-AUTO] Z START detected again after leaving start zone"));
                zDownRelay(false);
                g_zTags = oldTags;
                g_currentZLevel = oldLevel;
                server.send(500, "application/json", "{\"ok\":false,\"error\":\"z start detected again\"}");
                return;
            }

            if (isEnd) {
                DBG_PRINTLN(F("[Z-AUTO] Z END reached"));
                break;
            }

            leftStartZone = true;

            if (uidLen == lastLen && uidEquals(uid, lastUid, uidLen)) {
                stable++;
            } else {
                memcpy(lastUid, uid, uidLen);
                lastLen = uidLen;
                stable = 1;
            }

            if (stable >= 2) {
                stable = 0;
                lastLen = 0;
                memset(lastUid, 0, sizeof(lastUid));

                int existingIdx = findZTagIndexByUID(uid, uidLen);
                if (existingIdx < 0) {
                    ZTagInfo z{};
                    z.level = nextLevel++;
                    z.uidLen = uidLen > 7 ? 7 : uidLen;
                    memcpy(z.uid, uid, z.uidLen);
                    g_zTags.push_back(z);
                    g_currentZLevel = z.level;
                    noteLastDetected(LDK_Z_LEVEL, z.level, uid, uidLen);
                    DBG_PRINTF("[Z-AUTO] learned Z level=%d count=%u\r\n",
                               (int)z.level,
                               (unsigned)g_zTags.size());
                    debugPrintUid("[Z-AUTO] UID", uid, uidLen);
                    waitForTagRelease(nfcZ, nfcZReady, "z_auto", 1200UL);
                }
            }
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    zDownRelay(false);

    if (g_zTags.empty()) {
        DBG_PRINTLN(F("[Z-AUTO] no Z tags found, restoring old config"));
        g_zTags = oldTags;
        g_currentZLevel = oldLevel;
        moveToZStartPoint(30000);
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"no z tags found\"}");
        return;
    }

    saveZTagsToNVS();
    DBG_PRINTF("[Z-AUTO] saved %u Z tags\r\n", (unsigned)g_zTags.size());

    DBG_PRINTLN(F("[Z-AUTO] returning to Z START"));
    moveToZStartPoint(30000);

    String resp = "{\"ok\":true,\"count\":";
    resp += String((int)g_zTags.size());
    resp += ",\"start_level\":";
    resp += String(startLevel);
    resp += "}";
    server.send(200, "application/json", resp);
}

void handleZTagDelete() {
    DBG_PRINTLN(F("[HTTP] POST /z_tag_delete"));

    if (g_state != PS_IDLE) {
        server.send(409, "text/plain", "BUSY");
        return;
    }
    if (!server.hasArg("level")) {
        server.send(400, "text/plain", "level required");
        return;
    }

    int16_t level = (int16_t)server.arg("level").toInt();
    int idx = findZTagIndexByLevel(level);
    if (idx < 0) {
        server.send(404, "text/plain", "level not found");
        return;
    }

    DBG_PRINTF("[RFID] deleting Z level=%d at index=%d\r\n", (int)level, idx);
    g_zTags.erase(g_zTags.begin() + idx);
    if (g_currentZLevel == level) g_currentZLevel = -1;
    saveZTagsToNVS();
    server.send(200, "text/plain", "OK");
}
void handleBathsLearn() {
    DBG_PRINTLN(F("[HTTP] POST /baths_learn"));

    if (!server.hasArg("index")) {
        server.send(400, "text/plain", "index required");
        return;
    }

    int idx = server.arg("index").toInt();
    DBG_PRINTF("[HTTP]   index=%d\r\n", idx);

    if (idx < 0) {
        server.send(400, "text/plain", "bad index");
        return;
    }

    // при необходимости расширяем g_baths
    while (idx >= (int)g_baths.size() && (int)g_baths.size() < MAX_BATHS_LIMIT) {
        BathInfo b{};
        b.bathNumber = (uint16_t)(g_baths.size() + 1);
        b.uidLen     = 0;
        memset(b.uid, 0, sizeof(b.uid));
        b.isStart = false;
        b.isEnd   = false;
        g_baths.push_back(b);
        dynamicBathCount = (int)g_baths.size();
    }

    if (idx >= (int)g_baths.size()) {
        server.send(500, "text/plain", "index too big");
        return;
    }

    uint8_t uid[7] = {0};
    uint8_t uidLen = 0;
    DBG_PRINTF("[RFID] bath learn requested for storage index=%d\r\n", idx);
    if (!readUidWithTimeout(nfcX, nfcXReady, "bath", uid, &uidLen, 5000UL)) {
        server.send(408, "application/json",
                    "{\"ok\":false,\"error\":\"timeout: tag not detected\"}");
        return;
    }

    if (uidLen == 0 || uidLen > 7) {
        server.send(500, "application/json",
                    "{\"ok\":false,\"error\":\"invalid UID length\"}");
        return;
    }

    // Записываем в структуру
    BathInfo &b = g_baths[idx];
    b.uidLen = uidLen;
    memset(b.uid, 0, sizeof(b.uid));
    memcpy(b.uid, uid, uidLen);
    DBG_PRINTF("[RFID] bath index=%d assigned bathNumber=%u\r\n", idx, b.bathNumber);
    debugPrintUid("[RFID] bath saved UID", uid, uidLen);

    saveBathListToNVS();
    waitForTagRelease(nfcX, nfcXReady, "bath", 1500UL);

    String resp;
    resp.reserve(128);
    resp = "{\"ok\":true,\"index\":";
    resp += String(idx);
    resp += ",\"uid_len\":";
    resp += String(uidLen);
    resp += ",\"uid_hex\":\"";
    resp += uidToHexString(uid, uidLen);
    resp += "\"}";

    server.send(200, "application/json", resp);
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
        out += "\"z_level_down\":" + String(s.z_level_down) + ",";
        out += "\"z_down_timeout_s\":" + String(s.z_down_timeout_s) + ",";
        out += "\"hold_s\":"   + String(s.hold_s)   + ",";
        out += "\"z_level_up\":" + String(s.z_level_up) + ",";
        out += "\"z_up_timeout_s\":" + String(s.z_up_timeout_s) + ",";
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
        s.z_level_down = v["z_level_down"] | 0;
        s.z_down_timeout_s = v["z_down_timeout_s"] | (uint16_t)(v["z_down_s"] | 3);
        s.hold_s    = v["hold_s"]    | 0;
        s.z_level_up = v["z_level_up"] | 0;
        s.z_up_timeout_s = v["z_up_timeout_s"] | (uint16_t)(v["z_up_s"] | 3);
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
void uidToHex(const uint8_t* uid, uint8_t len, char* out) {
    char* p = out;
    for (uint8_t i = 0; i < len; i++) {
        sprintf(p, "%02X", uid[i]);
        p += 2;
        if (i < len - 1) {
            *p++ = ':';
        }
    }
    *p = '\0';
}
void handleStatus() {
    StaticJsonDocument<6144> doc;
    const Step *curStep = (g_stepIdx >= 0 && g_stepIdx < g_routeSteps) ? &g_route[g_stepIdx] : nullptr;
    const bool spinRequested = curStep ? ((curStep->flags & 1) != 0) : false;
    const bool fanRequested = curStep ? ((curStep->flags & 2) != 0) : false;
    const bool spinActive = digitalRead(REL_SPIN) == ON;
    const bool fanActive = digitalRead(REL_FAN) == ON;
    const bool xFwdActive = digitalRead(REL_X_FWD) == ON;
    const bool xRevActive = digitalRead(REL_X_REV) == ON;
    const bool zUpActive = digitalRead(REL_Z_UP) == ON;
    const bool zDownActive = digitalRead(REL_Z_DOWN) == ON;

    doc["state"] = g_state;
    doc["state_str"] = stateToString(g_state);
    doc["action"] = currentActionString();
    doc["waiting_for"] = currentWaitString();
    doc["time"] = rtcTimeString();
    doc["date"] = rtcDateString();
    doc["bath"] = g_currentBath;
    doc["z_level"] = g_currentZLevel;
    doc["step_idx"] = g_stepIdx;
    doc["steps"] = g_routeSteps;
    doc["active_route"] = getActiveRouteId();
    doc["target_bath"] = g_targetBath;
    doc["target_z_level"] = curStep ? (g_state == PS_RAISE_Z ? curStep->z_level_up : curStep->z_level_down) : -1;
    doc["direction_x"] = directionXToString();
    doc["spin_requested"] = spinRequested;
    doc["fan_requested"] = fanRequested;
    doc["spin_active"] = spinActive;
    doc["fan_active"] = fanActive;
    doc["x_reader_ready"] = nfcXReady;
    doc["z_reader_ready"] = nfcZReady;

    JsonObject relays = doc.createNestedObject("relays");
    relays["x_fwd"] = xFwdActive;
    relays["x_rev"] = xRevActive;
    relays["z_up"] = zUpActive;
    relays["z_down"] = zDownActive;
    relays["spin"] = spinActive;
    relays["fan"] = fanActive;

    if (curStep) {
        JsonObject step = doc.createNestedObject("current_step");
        step["bath"] = curStep->bath;
        step["z_level_down"] = curStep->z_level_down;
        step["z_down_timeout_s"] = curStep->z_down_timeout_s;
        step["hold_s"] = curStep->hold_s;
        step["z_level_up"] = curStep->z_level_up;
        step["z_up_timeout_s"] = curStep->z_up_timeout_s;
        step["dry_s"] = curStep->dry_s;
        step["spin"] = spinRequested;
        step["fan"] = fanRequested;
    }

    // Количество ванн в текущей калибровке
    doc["learn_count"] = dynamicBathCount;

    doc["start_configured"] = g_startPoint.configured;
    doc["end_configured"] = g_endPoint.configured;

    JsonObject sp = doc.createNestedObject("service_points");
    JsonObject spStart = sp.createNestedObject("start");
    spStart["configured"] = g_startPoint.configured;
    spStart["uid"] = g_startPoint.configured ? uidToHexString(g_startPoint.uid, g_startPoint.uidLen) : "";
    JsonObject spEnd = sp.createNestedObject("end");
    spEnd["configured"] = g_endPoint.configured;
    spEnd["uid"] = g_endPoint.configured ? uidToHexString(g_endPoint.uid, g_endPoint.uidLen) : "";
    JsonObject spZStart = sp.createNestedObject("z_start");
    spZStart["configured"] = g_zStartPoint.configured;
    spZStart["uid"] = g_zStartPoint.configured ? uidToHexString(g_zStartPoint.uid, g_zStartPoint.uidLen) : "";
    JsonObject spZEnd = sp.createNestedObject("z_end");
    spZEnd["configured"] = g_zEndPoint.configured;
    spZEnd["uid"] = g_zEndPoint.configured ? uidToHexString(g_zEndPoint.uid, g_zEndPoint.uidLen) : "";

    JsonArray zTags = doc.createNestedArray("z_tags");
    for (const auto &z : g_zTags) {
        JsonObject zo = zTags.createNestedObject();
        zo["level"] = z.level;
        zo["uid"] = uidToHexString(z.uid, z.uidLen);
    }

    // Последняя найденная точка/уровень
    if (g_lastDetected.kind != LDK_NONE) {
        JsonObject last = doc.createNestedObject("last_found");
        last["kind"] = g_lastDetected.kind;
        last["kind_str"] =
            g_lastDetected.kind == LDK_START ? "start" :
            g_lastDetected.kind == LDK_END ? "end" :
            g_lastDetected.kind == LDK_Z_START ? "z_start" :
            g_lastDetected.kind == LDK_Z_END ? "z_end" :
            g_lastDetected.kind == LDK_Z_LEVEL ? "z_level" : "process";
        last["index"] = g_lastDetected.number;
        last["isStart"] = g_lastDetected.kind == LDK_START;
        last["isEnd"]   = g_lastDetected.kind == LDK_END;
        last["uid"] = uidToHexString(g_lastDetected.uid, g_lastDetected.uidLen);
    }

    // Вся таблица рабочих ванн
    JsonArray arr = doc.createNestedArray("baths");
    for (auto &b : g_baths) {
        JsonObject o = arr.createNestedObject();
        o["index"] = b.bathNumber;
        o["isStart"] = b.isStart;
        o["isEnd"] = b.isEnd;
        o["kind"] = "process";

        char uidBuf[32];
        uidToHex(b.uid, b.uidLen, uidBuf);
        o["uid"] = uidBuf;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleActionLog() {
    StaticJsonDocument<16384> doc;
    JsonArray arr = doc.createNestedArray("items");

    size_t start = (g_actionLogCount == ACTION_LOG_CAPACITY) ? g_actionLogHead : 0;
    for (size_t i = 0; i < g_actionLogCount; ++i) {
        size_t idx = (start + i) % ACTION_LOG_CAPACITY;
        const ActionLogEntry &e = g_actionLog[idx];
        JsonObject o = arr.createNestedObject();
        o["seq"] = e.seq;
        o["date"] = e.date;
        o["time"] = e.time;
        o["level"] = e.level;
        o["category"] = e.category;
        o["message"] = e.message;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleActionLogCsv() {
    String out = "seq,date,time,level,category,message\r\n";
    size_t start = (g_actionLogCount == ACTION_LOG_CAPACITY) ? g_actionLogHead : 0;
    for (size_t i = 0; i < g_actionLogCount; ++i) {
        size_t idx = (start + i) % ACTION_LOG_CAPACITY;
        const ActionLogEntry &e = g_actionLog[idx];
        String msg = e.message;
        msg.replace("\"", "\"\"");
        out += String(e.seq) + ",";
        out += "\"" + e.date + "\",";
        out += "\"" + e.time + "\",";
        out += "\"" + e.level + "\",";
        out += "\"" + e.category + "\",";
        out += "\"" + msg + "\"\r\n";
    }
    server.send(200, "text/csv; charset=utf-8", out);
}

bool activeRouteIsExecutable() {
    if (g_routeSteps == 0) return false;
    if (!g_startPoint.configured) return false;

    for (uint16_t i = 0; i < g_routeSteps; ++i) {
        if (findBathStorageIndexByNumber(g_route[i].bath) < 0) {
            DBG_PRINTF("[ROUTE] Step %u references missing bathNumber=%u\r\n",
                       (unsigned)i, g_route[i].bath);
            return false;
        }
    }
    return true;
}

void handleStart() {
    DBG_PRINTLN(F("[HTTP] POST /start"));
    if (g_state == PS_IDLE && activeRouteIsExecutable()) {
        g_startCommand = true;
        DBG_PRINTLN(F("[HTTP]   startCommand set"));
        addActionLog("info", "command", "Получена команда START");
        server.send(200, "text/plain", "OK");
    } else {
        DBG_PRINTLN(F("[HTTP]   BUSY or route config invalid"));
        addActionLog("warn", "command", "START отклонен: линия занята или маршрут невалиден");
        server.send(409, "text/plain", "BUSY or invalid route/start config");
    }
}

void handleStop() {
    DBG_PRINTLN(F("[HTTP] POST /stop"));
    g_stopCommand = true;
    g_startCommand = false;
    addActionLog("warn", "command", "Получена команда STOP");
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
        s.z_level_down = v["z_level_down"] | 0;
        s.z_down_timeout_s = v["z_down_timeout_s"] | (uint16_t)(v["z_down_s"] | 3);
        s.hold_s    = v["hold_s"]    | 0;
        s.z_level_up = v["z_level_up"] | 0;
        s.z_up_timeout_s = v["z_up_timeout_s"] | (uint16_t)(v["z_up_s"] | 3);
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
    int requestedBath = server.arg("bath").toInt();
    DBG_PRINTF("[HTTP]   bath=%d\r\n", requestedBath);
    int b = findBathStorageIndexByNumber((uint16_t)requestedBath);
    if (b < 0) {
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
        out += "\"id\":" + String(g_baths[i].bathNumber) + ",";
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
        addActionLog(newState == PS_ERROR ? "error" : "info",
                     "state",
                     stateToString(prev) + " -> " + stateToString(newState) +
                     (reason && reason[0] ? String(" (") + reason + ")" : ""));
        prev = newState;
    }
    g_state = newState;
}

// ---------------- TASKS ----------------
void TaskProcess(void* pv) {
    (void)pv;
    DBG_PRINTLN(F("[TASK] TaskProcess started"));
    for (;;) {
        g_currentBath = getBathDisplayNumberByIndex(bathIndex);

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
            g_stepIdx = -1;
            setState(PS_IDLE, "STOP command");
        }

        switch (g_state) {
               case PS_IDLE:
                relOffAll();   // ← добавляем
            if (g_startCommand) {
                DBG_PRINTLN(F("[FSM] Start command in IDLE"));
                g_startCommand = false;
                g_stepIdx = -1;
                setState(PS_HOMING, "Start from IDLE");
            }
            break;

        case PS_HOMING: {
            DBG_PRINTLN(F("[FSM] HOMING (go to start point by RFID)"));

            // Едем к стартовой точке линии, считая метку RFID
            bool ok = moveToStartPoint(30000);

            if (ok) {
                DBG_PRINTLN(F("[FSM] HOMING success"));
                g_currentBath = getBathDisplayNumberByIndex(bathIndex);
                g_stepIdx     = 0;
                setState(PS_MOVE_X, "Homing completed");
            } else {
                DBG_PRINTLN(F("[FSM] HOMING failed"));
                setState(PS_ERROR, "Homing failed");
            }
        } break;
case PS_LEARN_BATHS: {
    DBG_PRINTLN("[LEARN] START auto learn baths");

    if (!g_startPoint.configured || !g_endPoint.configured) {
        DBG_PRINTLN("[LEARN] ERROR: start or end service point not set");
        setState(PS_ERROR, "No start/end service point");
        break;
    }
    if (servicePointMatches(g_startPoint, g_endPoint.uid, g_endPoint.uidLen)) {
        DBG_PRINTLN("[LEARN] ERROR: start and end points are identical");
        setState(PS_ERROR, "Start/end are identical");
        break;
    }

    std::vector<BathInfo> oldBaths = g_baths;
    int oldDynamicBathCount = dynamicBathCount;

    noteLastDetected(LDK_NONE, -1, nullptr, 0);

    DBG_PRINTLN("[LEARN] Moving to START point before scan...");
    if (!moveToStartPoint(30000)) {
        DBG_PRINTLN("[LEARN] ERROR: cannot reach START point");
        setState(PS_ERROR, "Cannot reach START");
        break;
    }

    g_baths.clear();
    dynamicBathCount = 0;
    noteLastDetected(LDK_START, -1, g_startPoint.uid, g_startPoint.uidLen);
    oledShowLearn(0, g_startPoint.uid, g_startPoint.uidLen);

    uint8_t lastUid[7] = {0};
    uint8_t lastLen = 0;
    int stable = 0;
    bool leftStartZone = false;
    bool learnSucceeded = false;
    const uint32_t learnTimeoutMs = 120000UL;
    const uint32_t tLearn0 = millis();

    xRev(false);
    xFwd(true);

    while (millis() - tLearn0 < learnTimeoutMs) {
        if (digitalRead(PIN_ESTOP) == LOW || g_stopCommand) {
            xFwd(false);
            xRev(false);
            g_baths = oldBaths;
            dynamicBathCount = oldDynamicBathCount;
            setState(PS_ERROR, "LEARN stopped");
            break;
        }

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        bool ok = readPassiveTargetX(uid, &uidLen);
        if (!ok || uidLen == 0) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (uidLen == lastLen && uidEquals(uid, lastUid, uidLen)) {
            stable++;
        } else {
            memcpy(lastUid, uid, uidLen);
            lastLen = uidLen;
            stable = 1;
        }

        if (stable < 2) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        // consume this tag once, then wait for the next stable read
        stable = 0;
        lastLen = 0;
        memset(lastUid, 0, sizeof(lastUid));

        if (servicePointMatches(g_startPoint, uid, uidLen)) {
            noteLastDetected(LDK_START, -1, uid, uidLen);
            oledShowLearn(dynamicBathCount, uid, uidLen);
            if (!leftStartZone) {
                vTaskDelay(80 / portTICK_PERIOD_MS);
                continue;
            }

            DBG_PRINTLN("[LEARN] START point detected again after leaving start zone");
            xFwd(false);
            xRev(false);
            g_baths = oldBaths;
            dynamicBathCount = oldDynamicBathCount;
            setState(PS_ERROR, "Start detected again");
            break;
        }

        leftStartZone = true;

        if (servicePointMatches(g_endPoint, uid, uidLen)) {
            noteLastDetected(LDK_END, -1, uid, uidLen);
            oledShowLearn(dynamicBathCount, uid, uidLen);
            DBG_PRINTLN("[LEARN] END service point detected");
            xFwd(false);
            xRev(false);
            learnSucceeded = true;
            break;
        }

        int idx = findBathIndexByUID(uid, uidLen);
        if (idx < 0) {
            BathInfo b{};
            b.bathNumber = dynamicBathCount + 1;
            b.uidLen = uidLen;
            memcpy(b.uid, uid, uidLen);
            b.isStart = false;
            b.isEnd   = false;

            g_baths.push_back(b);
            dynamicBathCount = (int)g_baths.size();
            noteLastDetected(LDK_PROCESS, b.bathNumber, uid, uidLen);

            DBG_PRINTF("[LEARN] NEW BATH %d UID: ", b.bathNumber);
            for (int i = 0; i < uidLen; i++) DBG_PRINTF("%02X ", uid[i]);
            DBG_PRINTLN("");
        } else {
            noteLastDetected(LDK_PROCESS, g_baths[idx].bathNumber, uid, uidLen);
        }

        oledShowLearn(dynamicBathCount, uid, uidLen);
        vTaskDelay(80 / portTICK_PERIOD_MS);
    }

    if (learnSucceeded) {
        if (dynamicBathCount <= 0) {
            DBG_PRINTLN("[LEARN] ERROR: no baths found between START and END");
            g_baths = oldBaths;
            dynamicBathCount = oldDynamicBathCount;
            setState(PS_ERROR, "No baths found");
            break;
        }

        DBG_PRINTLN("[LEARN] Learning finished, saving...");
        saveBathListToNVS();

        DBG_PRINTLN("[LEARN] Returning Home...");
        moveToStartPoint(30000);
        setState(PS_IDLE, "Learn done");
        break;
    }

    if (g_state != PS_ERROR) {
        DBG_PRINTLN("[LEARN] ERROR: timeout waiting for END point");
        xFwd(false);
        xRev(false);
        g_baths = oldBaths;
        dynamicBathCount = oldDynamicBathCount;
        setState(PS_ERROR, "Learn timeout");
    }
    break;
}

break;

            case PS_MOVE_X: {
                DBG_PRINTF("[FSM] MOVE_X, stepIdx=%d of %d\r\n", (int)g_stepIdx, (int)g_routeSteps);
                if (g_stepIdx >= g_routeSteps) {
                    setState(PS_RETURN_HOME, "No more steps");
                    break;
                }
                Step &st = g_route[g_stepIdx];
                DBG_PRINTF("[FSM]   target bath=%u\r\n", st.bath);
                bool ok = moveToBathNumber(st.bath, 30000);
                setState(ok ? PS_LOWER_Z : PS_ERROR, ok ? "moveToBath OK" : "moveToBath FAIL");
            } break;

            case PS_LOWER_Z: {
                DBG_PRINTF("[FSM] LOWER_Z, stepIdx=%d\r\n", (int)g_stepIdx);
                Step &st = g_route[g_stepIdx];
                bool ok = moveZToLevel((int16_t)st.z_level_down, true, st.z_down_timeout_s);
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
                bool ok = moveZToLevel((int16_t)st.z_level_up, false, st.z_up_timeout_s);
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
                bool ok = moveToStartPoint(30000);
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
    uint32_t lastUiHeartbeat = 0;
    for (;;) {
        if (!oledReady) {
            vTaskDelay(250 / portTICK_PERIOD_MS);
            continue;
        }
        if (millis() - lastUiHeartbeat >= 5000UL) {
            lastUiHeartbeat = millis();
            DBG_PRINTF("[UI] heartbeat state=%d mode=%d oled=%d X=%d Z=%d\r\n",
                       (int)g_state, (int)g_uiMode, (int)oledReady, (int)nfcXReady, (int)nfcZReady);
        }
        if (g_state == PS_LEARN_BATHS) {

            // <<< ВАЖНО! НИЧЕГО НЕ РИСУЕМ ВО ВРЕМЯ LEARN
            // oledShowLearn вызывается только из TaskProcess
            vTaskDelay(100);
        } else if (g_state != PS_IDLE) {
            oledShowRun();
        } else {
            int d = getEncDelta();
            bool encPress = encoderButtonPressed();
            bool startPress = startButtonPressed();

            switch (g_uiMode) {
                case UI_HOME:
                    oledShowIdle();

                    if (startPress && activeRouteIsExecutable()) {
                        g_startCommand = true;
                    }

                    if (encPress) {
                        g_uiMode = UI_MAIN_MENU;
                        g_menuIndex = 0;
                    }
                    break;

                case UI_MAIN_MENU:
                    oledShowMainMenu();
                    if (d != 0) {
                        g_menuIndex += d;
                        if (g_menuIndex < 0) g_menuIndex = 2;
                        if (g_menuIndex > 2) g_menuIndex = 0;
                    }
                    if (encPress) {
                        if (g_menuIndex == 0) {
                            if (activeRouteIsExecutable()) {
                                g_startCommand = true;
                                g_uiMode = UI_HOME;
                            }
                        } else if (g_menuIndex == 1) {
                            g_uiMode = UI_ROUTE_SELECT;
                        } else {
                            g_uiMode = UI_SETTINGS;
                        }
                    }
                    break;

                case UI_ROUTE_SELECT:
                    oledShowSelectMenu();
                    if (d != 0) {
                        g_selectedRoute += d;
                        if (g_selectedRoute < 0) g_selectedRoute = ROUTE_SLOTS - 1;
                        if (g_selectedRoute >= ROUTE_SLOTS) g_selectedRoute = 0;
                    }
                    if (encPress) {
                        loadRouteSlot((uint8_t)g_selectedRoute);
                        g_uiMode = UI_HOME;
                    }
                    if (startPress) {
                        g_uiMode = UI_MAIN_MENU;
                    }
                    break;

                case UI_SETTINGS:
                    oledShowSettings();
                    if (encPress || startPress) {
                        g_uiMode = UI_MAIN_MENU;
                    }
                    break;
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
    DBG_PRINTLN(F("[SETUP] OLED init complete"));

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
    loadServicePointFromNVS(NVS_KEY_START_POINT, g_startPoint);
    loadServicePointFromNVS(NVS_KEY_END_POINT, g_endPoint);
    loadServicePointFromNVS(NVS_KEY_Z_START_POINT, g_zStartPoint);
    loadServicePointFromNVS(NVS_KEY_Z_END_POINT, g_zEndPoint);
    loadZTagsFromNVS();
    migrateLegacyServiceFlagsIfNeeded();

    dynamicBathCount = (int)g_baths.size();

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
        demo[0] = { 1, 1, 3, 4, 0, 3, 5, 1 };
        demo[1] = { 2, 1, 3, 5, 0, 3, 6, 0 };
        demo[2] = { 1, 1, 3, 3, 0, 3, 4, 2 };

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
   // HTTP routes
DBG_PRINTLN(F("[SETUP] HTTP routes init"));

server.on("/", HTTP_GET, handleRoot);
server.on("/status", HTTP_GET, handleStatus);
server.on("/action_log", HTTP_GET, handleActionLog);
server.on("/action_log.csv", HTTP_GET, handleActionLogCsv);
server.on("/start", HTTP_POST, handleStart);
server.on("/stop", HTTP_POST, handleStop);
server.on("/route", HTTP_POST, handleRoutePost);

server.on("/bath_data", HTTP_GET, handleBathData);
server.on("/bath_history", HTTP_GET, handleBathHistory);

server.on("/routes_list", HTTP_GET, handleRoutesList);
server.on("/route_get", HTTP_GET, handleRouteGet);
server.on("/route_save", HTTP_POST, handleRouteSaveLib);
server.on("/route_delete", HTTP_DELETE, handleRouteDelete);
server.on("/route_apply", HTTP_POST, handleRouteApply);

server.on("/baths_list", HTTP_GET, handleBathsList);
server.on("/baths_update", HTTP_POST, handleBathsUpdate);
server.on("/baths_learn", HTTP_POST, handleBathsLearn);
server.on("/baths_add", HTTP_POST, handleBathAdd);
server.on("/baths_delete", HTTP_POST, handleBathDelete);
server.on("/baths_autolearn", HTTP_POST, handleBathsAutoLearn);
server.on("/service_point_learn", HTTP_POST, handleServicePointLearn);
server.on("/z_tag_learn", HTTP_POST, handleZTagLearn);
server.on("/z_tag_delete", HTTP_POST, handleZTagDelete);
server.on("/z_autolearn", HTTP_POST, handleZAutoLearn);

// ============================
//    *** UI ROUTES ***
// ============================

// MONITOR PAGE
server.on("/monitor", HTTP_GET, []() {
    DBG_PRINTLN(F("[HTTP] GET /monitor"));
    String page = renderPageMonitor();
    server.send(200, "text/html", page);
});

server.on("/logs_ui", HTTP_GET, []() {
    DBG_PRINTLN(F("[HTTP] GET /logs_ui"));
    String page = renderPageLogs();
    server.send(200, "text/html", page);
});

// ROUTES PAGE
server.on("/routes_ui", HTTP_GET, []() {
    DBG_PRINTLN(F("[HTTP] GET /routes_ui"));
    String page = renderPageRoutes();
    server.send(200, "text/html", page);
});

// BATHS (RFID BINDING) PAGE
server.on("/baths_ui", HTTP_GET, []() {
    DBG_PRINTLN(F("[HTTP] GET /baths_ui"));
    String page = renderPageBaths();
    server.send(200, "text/html", page);
});

// AUTOLEARN PAGE
server.on("/baths_autolearn_ui", HTTP_GET, []() {
    DBG_PRINTLN(F("[HTTP] GET /baths_autolearn_ui"));
    String page = renderPageAutoLearn();
    server.send(200, "text/html", page);
});

    server.begin();
    DBG_PRINTLN(F("[SETUP] HTTP server started"));

    // FreeRTOS задачи
    DBG_PRINTLN(F("[SETUP] Create tasks"));
    xTaskCreatePinnedToCore(TaskProcess, "Process", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(TaskWeb,     "Web",     4096, nullptr, 1, nullptr, 0);

    DBG_PRINTLN(F("[SETUP] Tasks created, init PN532 after UI start"));
    nfcInit();

    DBG_PRINTLN(F("===== GalvaControl setup done ====="));
}

void loop() {
    // Всё работает в задачах FreeRTOS
    vTaskDelay(portMAX_DELAY);
}
