#ifndef ORA_APP_H
#define ORA_APP_H

// ==========================================================
//  Ora  --  az EREDETI ora.ino valtozatlan kodja,
//  csak namespace-be csomagolva, hogy a 4 program
//  egyutt eljen egy sketchben (nevutkozes nelkul).
// ==========================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <time.h>                // NTP-hez
#include "config.h"
#include "WifiGuard.h"

namespace Ora {

const char* ssid = SECRET_WIFI_SSID;          // -> secrets.h
const char* password = SECRET_WIFI_PASSWORD;  // -> secrets.h
bool offlineMode = false;                     // UJ: nincs WiFi

#define TFT_CS         5
#define TFT_RST        4
#define TFT_DC         2
#define ST7735_GRAY       0x8410 
#define ST7735_DARKGRAY   0x2104
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define BTN_A 26
#define BTN_B 27

bool bigClockMode = false;
bool lastBtnA = HIGH;

struct DashData {
  int hour = 0;
  int minute = 0;
  int second = 0;
  int year = 2026;
  int month = 1;
  int day = 1;
  float temp = 0.0;
  int weatherCode = 0;
  int rainChance = 0;      
  bool valid = false;
};

DashData dash;

unsigned long lastWeatherSync = 0;
unsigned long weatherSyncDelay = 600000;
unsigned long lastSecondTimer = 0;
unsigned long lastNevnapSwitch = 0;
bool showTomorrowNevnap = false;

int bigClockLastHour   = -1;
int bigClockLastMinute = -1;

const char* nevnapok[12][31] = {
  {"Fruzsina","Abel","Genoveva","Titusz","Simon","Boldizsar","Attila","Ramona","Szeverin","Melania","Agnes","Erno","Veronika","Bodog","Lorand","Gusztav","Antal","Piroska","Sara","Fabian","Agnes","Vince","Zelma","Timoteus","Pal","Vanda","Angelika","Karoly","Adel","Martina","Gerda"},
  {"Ignac","Karolina","Balazs","Reka","Agnes","Dorottya","Todor","Aranka","Ellina","Elvira","Bertold","Livia","Ella","Balint","Kolos","Julianna","Donat","Bernadett","Zsuzsanna","Aladar","Eleonora","Gerzson","Alfred","Szerena","Matyas","Geza","Edina","Olympia","Jozsua","",""},
  {"Albin","Lujza","Kornelia","Kazmer","Adel","Leonora","Tamas","Zoltan","Franciska","Ildiko","Szilard","Maximilian","Gergely","Matild","Kristof","Henrietta","Gertrud","Sandor","Jozsef","Benedek","Benedek","Beata","Emoke","Gabor","Irena","Arpad","Hajnalka","Gedeon","Auguszta","Zalan","Arpad"},
  {"Hugo","Aron","Buda","Richard","Vince","Vilmos","Herman","Denes","Erhard","Zsolt","Leo","Gyula","Ida","Tibor","Anasztazia","Csongor","Rudolf","Andrea","Emma","Tivadar","Konrad","Csilla","Noemi","Gyorgy","Bela","Ervin","Zita","Valeria","Peter","Katalin",""},
  {"Fuler","Zsigmond","Timea","Florian","Adrian","Gyongyi","Gizella","Mihaly","Gergely","Armin","Ferenc","Pongrac","Szervac","Bonifac","Zsofia","Botond","Paszkal","Erik","Ivoja","Bernat","Konstantin","Julia","Dezso","Eszter","Orban","Evelin","Emil","Magdolna","Zsanett","Janka","Angels"},
  {"Tunde","Karmen","Clara","Bulcsu","Fatime","Norbert","Robert","Medard","Felix","Margit","Barnabas","Csilla","Antal","Vazul","Jolanda","Justin","Laura","Arnold","Gyorgy","Rafael","Alajos","Paulina","Zoltan","Janos","Vilmos","Janos","Laszlo","Levente","Peter","Pal",""},
  {"Annamaria","Otto","Kornel","Ulrik","Emese","Csaba","Apollonia","Ellina","Lukrecia","Amalia","Nora","Izabella","Sara","Stella","Henrik","Valter","Endre","Frigyes","Emilia","Illes","Daniel","Magdolna","Lenke","Kinga","Kristof","Anna","Olga","Anna","Marta","Florean","Oszkar"},
  {"Bogarka","Lehel","Hermina","Domonkos","Krisztina","Berta","Ibolya","Laszlo","Emod","Laurent","Zsuzsanna","Klara","Ipoly","Samu","Maria","Abel","Jacint","Ilona","Huba","Istvan","Samu","Aranka","Felipe","Bertalan","Lajos","Izso","Gaspar","Agoston","Beatrix","Benjamin","Leopold"},
  {"Egyed","Rebeka","Hilda","Rozalia","Viktor","Zakarias","Regina","Maria","Adam","Nikolett","Teodora","Maria","Kornel","Roxana","Eniko","Edit","Edit","Diana","Wilhelmina","Friderika","Mate","Moric","Tekla","Gellert","Eufrozina","Justina","Adalbert","Vencel","Mihaly","Jeromos",""},
  {"Malvin","Petra","Helga","Ferenc","Aurel","Bruno","Amalia","Koppany","Denes","Gedeon","Brigitta","Miksa","Kalman","Ede","Terezia","Gal","Hedvig","Alfold","Nandor","Vendel","Orsolya","Elod","Gyongyi","Salamon","Blanka","Domonkos","Szabina","Simon","Narcisz","Alfonz","Farkas"},
  {"Marianna","Achilles","Gyozo","Karoly","Imre","Leonard","Rezso","Zsombor","Tivadar","Reka","Marton","Jonas","Szilvia","Aliz","Albert","Odon","Hortenzia","Jeno","Erzsebet","Jolan","Oliver","Cecilia","Kelemen","Emma","Katalin","Virag","Virag","Stefania","Taksony","Andras",""},
  {"Elza","Melinda","Ferenc","Borbala","Vilma","Miklos","Ambrus","Maria","Natalia","Judit","Arpad","Gabriella","Luca","Szilarda","Valer","Etelka","Lazar","Auguszta","Viola","Teofil","Tamas","Zeno","Viktoria","Adam","Eugen","Istvan","Janos","Kamillo","Tomas","David","Szilveszter"}
};


// --- elore-deklaraciok (ezt az .ino-nal az Arduino IDE generalta) ---
String getWeatherDesc(int code);
bool syncNTPTime();
void setup();
void loop();
void getDashboardData();
void drawStaticUIErr(int code);
void renderStaticUI();
void renderBigClock();
void renderBigClockTimeOnly();
void renderClockTime();
void renderLiveWiFi();
void renderNevnapZone();
// -------------------------------------------------------------------

String getWeatherDesc(int code) {
  if (code == 0)                return "Tiszta eg";
  if (code >= 1  && code <= 3)  return "Felhos";
  if (code == 45 || code == 48) return "Kod";
  if (code >= 51 && code <= 55) return "Szitalas";
  if (code >= 61 && code <= 65) return "Eso";
  if (code >= 71 && code <= 75) return "Hoeses";
  if (code >= 80 && code <= 82) return "Zapor";
  if (code == 95)               return "Zivatar";
  if (code >= 96 && code <= 99) return "Jegeso";
  return "Ismeretlen";
}

void renderStaticUI();
void renderBigClock();
void renderBigClockTimeOnly();
void renderClockTime();
void renderLiveWiFi();
void renderNevnapZone();
void getDashboardData();
void drawStaticUIErr(int code);

// ─── NTP SZINKRONIZÁCIÓ ───────────────────────────────────────
bool syncNTPTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {  // 5 másodperc timeout
    Serial.println("NTP sikertelen");
    return false;
  }
  dash.hour   = timeinfo.tm_hour;
  dash.minute = timeinfo.tm_min;
  dash.second = timeinfo.tm_sec;
  dash.year   = timeinfo.tm_year + 1900;
  dash.month  = timeinfo.tm_mon + 1;
  dash.day    = timeinfo.tm_mday;
  dash.valid  = true;
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 60000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1); 
  tft.fillScreen(ST7735_BLACK);
  
  tft.setCursor(10, 30);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.print("WiFi csatlakozas...");

  // UJ: idokorlatos csatlakozas (WifiGuard.h) - WiFi nelkul nem ragad be
  offlineMode = !wifiConnectWithTimeout(ssid, password);
  if (offlineMode) {
    drawNoWifiScreen(tft, "Ora");
    return;
  }
  
  // ── NTP beállítása ──
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  tft.fillRect(10, 40, 100, 10, ST7735_BLACK);
  tft.setCursor(10, 40);
  tft.print("Ido szinkron...");
  while (!syncNTPTime()) {
    delay(500);
    esp_task_wdt_reset();
  }
  
  tft.fillScreen(ST7735_BLACK);
  getDashboardData();   // időjárás adatok
  renderStaticUI();
}

void loop() {
  esp_task_wdt_reset();
  if (offlineMode) return;   // UJ: "Nincs WiFi" kepernyo marad
  unsigned long currentMillis = millis();

  // ── GOMB ─────────────────────────────────────────────────
  bool currentBtnA = digitalRead(BTN_A);
  if (lastBtnA == HIGH && currentBtnA == LOW) {
    bigClockMode = !bigClockMode;
    bigClockLastHour   = -1;
    bigClockLastMinute = -1;
    if (bigClockMode) {
      renderBigClock();
    } else {
      renderStaticUI();
    }
  }
  lastBtnA = currentBtnA;

  // ── MÁSODPERCES KETYEGÉS ──────────────────────────────────
  if (currentMillis - lastSecondTimer >= 1000) {
    lastSecondTimer = currentMillis;
    if (dash.valid) {
      dash.second++;
      if (dash.second >= 60) {
        dash.second = 0;
        dash.minute++;
        if (dash.minute >= 60) {
          dash.minute = 0;
          dash.hour++;
          if (dash.hour >= 24) dash.hour = 0;
        }
      }
      if (bigClockMode) {
        renderBigClockTimeOnly();
      } else {
        renderClockTime();
        renderLiveWiFi();
      }
    }
  }

  // ── NÉVNAP VÁLTÁS ─────────────────────────────────────────
  if (!bigClockMode && currentMillis - lastNevnapSwitch >= 5000) {
    lastNevnapSwitch = currentMillis;
    if (dash.valid) {
      showTomorrowNevnap = !showTomorrowNevnap;
      renderNevnapZone();
    }
  }

  // ── 10 PERCES FRISSÍTÉS ───────────────────────────────────
  if (currentMillis - lastWeatherSync >= weatherSyncDelay) {
    lastWeatherSync = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      // NTP újraszinkronizálás (pontos idő)
      syncNTPTime();
      // Időjárás frissítés
      getDashboardData();
      if (!bigClockMode) renderStaticUI();
    }
  }
}

void getDashboardData() {
  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" CFG_STR(CFG_WEATHER_LAT) "&longitude=" CFG_STR(CFG_WEATHER_LON) "&current=temperature_2m,weathercode&hourly=precipitation_probability&timezone=Europe/Budapest&forecast_days=1";
  http.begin(url);
  http.setTimeout(10000);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String response = http.getString();
    DynamicJsonDocument doc(12288); 
    deserializeJson(doc, response);

    dash.temp        = doc["current"]["temperature_2m"].as<float>();
    dash.weatherCode = doc["current"]["weathercode"].as<int>();

    String currentTime = doc["current"]["time"].as<String>(); 
    int currentHour    = currentTime.substring(11, 13).toInt();
    
    if (currentHour >= 0 && currentHour < 24)
      dash.rainChance = doc["hourly"]["precipitation_probability"][currentHour].as<int>();
    else
      dash.rainChance = doc["hourly"]["precipitation_probability"][0].as<int>();

    // ⚠️ IDŐ BEÁLLÍTÁS ELTÁVOLÍTVA – NTP kezeli
    // dash.valid már igaz az NTP-től
  } else {
    drawStaticUIErr(httpResponseCode);
  }
  http.end();
}

void drawStaticUIErr(int code) {
  tft.fillRect(5, 5, 60, 10, ST7735_BLACK);
  tft.setCursor(5, 5);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_RED);
  tft.print("ERR: "); tft.print(code);
}

// ── FŐ DASHBOARD ─────────────────────────────────────────────
void renderStaticUI() {
  tft.fillScreen(ST7735_BLACK);
  
  // Dátum
  tft.setTextSize(1);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(5, 5);
  tft.print(dash.year); tft.print(".");
  if (dash.month < 10) tft.print("0");
  tft.print(dash.month); tft.print(".");
  if (dash.day < 10) tft.print("0");
  tft.print(dash.day); tft.print(".");

  // Elválasztó vonal
  tft.drawFastHLine(0, 52, 160, ST7735_GRAY);

  // Időjárás
  tft.setCursor(5, 60);
  tft.setTextColor(ST7735_GRAY);
  tft.setTextSize(1);
  tft.print(CFG_CITY_NAME ":");
  
  tft.setCursor(5, 72);
  tft.setTextSize(2);
  tft.setTextColor(ST7735_WHITE);
  tft.print((int)dash.temp); tft.print("C");
  
  tft.setTextSize(1);
  tft.setCursor(5, 92);
  tft.setTextColor(ST7735_YELLOW);
  tft.print(getWeatherDesc(dash.weatherCode));

  tft.setCursor(5, 104);
  tft.setTextColor(ST7735_GRAY);
  tft.print("Eso val.: ");
  tft.setTextColor(ST7735_BLUE);
  tft.print(dash.rainChance); tft.print("%");

  // Függőleges elválasztó
  tft.drawFastVLine(92, 58, 62, ST7735_DARKGRAY);

  // Óra teljes kiírása egyben
  tft.fillRect(0, 18, 160, 30, ST7735_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST7735_GREEN);
  char timeBuff[9];
  sprintf(timeBuff, "%02d:%02d:%02d", dash.hour, dash.minute, dash.second);
  tft.setCursor(8, 20);
  tft.print(timeBuff);

  renderNevnapZone();
  renderLiveWiFi();
}

// ── NAGY ÓRA — statikus elemek (egyszer rajzolódik) ──────────
void renderBigClock() {
  tft.fillScreen(ST7735_BLACK);

  // Kerekített keret — CSAK egyszer
  tft.drawRoundRect(4, 4, 152, 120, 12, ST7735_CYAN);

  // Dátum alul, kis méret, fehér
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  char dateBuff[6];
  sprintf(dateBuff, "%02d.%02d", dash.month, dash.day);
  tft.setCursor(65, 106);
  tft.print(dateBuff);

  // Idő kiírása
  renderBigClockTimeOnly();
}

// ── NAGY ÓRA — csak idő frissítése ────────────────────────────
void renderBigClockTimeOnly() {
  if (dash.hour == bigClockLastHour && dash.minute == bigClockLastMinute) return;

  bigClockLastHour   = dash.hour;
  bigClockLastMinute = dash.minute;

  tft.fillRect(10, 20, 140, 72, ST7735_BLACK);

  tft.setTextSize(4);
  tft.setTextColor(ST7735_WHITE);
  char timeBuff[6];
  sprintf(timeBuff, "%02d:%02d", dash.hour, dash.minute);
  tft.setCursor(20, 30);
  tft.print(timeBuff);
}

// ── FŐ NÉZET — óra frissítés ─────────────────────────────────
void renderClockTime() {
  tft.fillRect(0, 18, 160, 30, ST7735_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST7735_GREEN);
  char timeBuff[9];
  sprintf(timeBuff, "%02d:%02d:%02d", dash.hour, dash.minute, dash.second);
  tft.setCursor(8, 20);
  tft.print(timeBuff);
}

// ── WIFI JELERŐSSÉG ──────────────────────────────────────────
void renderLiveWiFi() {
  long rssi = WiFi.RSSI();
  tft.fillRect(120, 5, 38, 10, ST7735_BLACK);
  tft.setTextSize(1);
  if (rssi >= -60)      tft.setTextColor(ST7735_GREEN);
  else if (rssi >= -75) tft.setTextColor(ST7735_YELLOW);
  else                  tft.setTextColor(ST7735_RED);
  tft.setCursor(120, 5);
  tft.print(rssi); tft.print("dB");
}

// ── NÉVNAP ZÓNA ──────────────────────────────────────────────
void renderNevnapZone() {
  tft.fillRect(96, 62, 62, 60, ST7735_BLACK);
  tft.setTextSize(1);
  
  int m = dash.month - 1;
  int d = dash.day - 1;
  if (m < 0 || m > 11 || d < 0 || d > 30) return;

  if (!showTomorrowNevnap) {
    tft.setCursor(96, 62);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Mai nev:");
    tft.setCursor(96, 76);
    tft.setTextColor(ST7735_CYAN);
    tft.print(nevnapok[m][d]);
  } else {
    int tm = m, td = d + 1;
    if (tm == 1 && td >= 28)                                           { td = 0; tm++; }
    else if (td >= 30 && (tm == 3 || tm == 5 || tm == 8 || tm == 10)) { td = 0; tm++; }
    else if (td >= 31)                                                  { td = 0; tm++; }
    if (tm > 11) tm = 0;
    tft.setCursor(96, 62);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Holnap:");
    tft.setCursor(96, 76);
    tft.setTextColor(ST7735_GRAY); 
    tft.print(nevnapok[tm][td]);
  }
}

} // namespace Ora

#endif
