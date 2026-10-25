#ifndef IDOJARAS_APP_H
#define IDOJARAS_APP_H

// ==========================================================
//  Idojaras  --  az EREDETI idojaras.ino valtozatlan kodja,
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
#include "config.h"
#include "WifiGuard.h"

namespace Idojaras {

// Wi-Fi adatok
const char* ssid = SECRET_WIFI_SSID;          // -> secrets.h
const char* password = SECRET_WIFI_PASSWORD;  // -> secrets.h
bool offlineMode = false;                     // UJ: nincs WiFi

// Kijelző lábak
#define TFT_CS         5
#define TFT_RST        4
#define TFT_DC         2
#define ST7735_GRAY       0x8410 
#define ST7735_DARKGRAY   0x2104 
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Kiterjesztett struktúra a jövőbeli előrejelzéshez (Eső esély nélkül)
struct WeatherData {
  float temp = 0.0;
  int humidity = 0;
  float precipitation = 0.0;
  int weatherCode = 0;
  int cloudCover = 0;
  float windSpeed = 0.0;
  int windDirection = 0;
  float uvIndex = 0.0;     
  int rainChance = 0;      
  String sunrise = "--:--";
  String sunset = "--:--";
  
  // Tömbök a jövőbeli napoknak: [0]=Holnap, [1]=Holnaputan, [2]=Azutan
  int dayOfWeek[3] = {0, 0, 0}; 
  float maxTemps[3] = {0.0, 0.0, 0.0};
  
  bool valid = false;
};

WeatherData current_weather;
bool weatherFetchError = false;

unsigned long lastWeatherUpdate = 0;
unsigned long weatherTimerDelay = 600000; // 10 perc

unsigned long lastScreenSwitch = 0;
unsigned long screenSwitchDelay = 8000;  // 8 másodperc
int currentScreen = 1;                   

bool hasWeatherPastData = false;

// Magyar napnevek az automatikus léptetéshez
const char* napokNevei[] = {"Vasarnap", "Hetfo", "Kedd", "Szerda", "Csutortok", "Pentek", "Szombat"};

// Zeller-féle algoritmus a nap nevének kiszámításához a dátumból

// --- elore-deklaraciok (ezt az .ino-nal az Arduino IDE generalta) ---
int calculateDayOfWeek(int y, int m, int d);
String getWeatherDesc(int code);
String getWindCompass(int degrees);
void setup();
void loop();
void getWeatherData();
void renderWeatherScreen();
// -------------------------------------------------------------------

int calculateDayOfWeek(int y, int m, int d) {
  if (m < 3) { m += 12; y--; }
  int k = y % 100;
  int j = y / 100;
  int h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  return ((h + 5) % 7); // 0 = Vasárnap, 1 = Hétfő ... 6 = Szombat
}

String getWeatherDesc(int code) {
  if (code == 0) return "Tiszta eg";
  if (code >= 1 && code <= 3) return "Reszben felhos";
  if (code == 45 || code == 48) return "Kod";
  if (code >= 51 && code <= 55) return "Szitalas";
  if (code >= 61 && code <= 65) return "Eso";
  if (code >= 71 && code <= 75) return "Hoeses";
  if (code >= 80 && code <= 82) return "Zapor";
  if (code == 95) return "Zivatar";
  if (code >= 96 && code <= 99) return "Jegeso";
  return "Ismeretlen";
}

String getWindCompass(int degrees) {
  if (degrees >= 337.5 || degrees < 22.5)  return "E";
  if (degrees >= 22.5  && degrees < 67.5)  return "EK";
  if (degrees >= 67.5  && degrees < 112.5) return "K";
  if (degrees >= 112.5 && degrees < 157.5) return "DK";
  if (degrees >= 157.5 && degrees < 202.5) return "D";
  if (degrees >= 202.5 && degrees < 247.5) return "DNY";
  if (degrees >= 247.5 && degrees < 292.5) return "NY";
  if (degrees >= 292.5 && degrees < 337.5) return "ENY";
  return "-";
}

void setup() {
  Serial.begin(115200);

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
  tft.print("Idojaras WiFi csatlakozas...");

  // UJ: idokorlatos csatlakozas (WifiGuard.h) - WiFi nelkul nem ragad be
  offlineMode = !wifiConnectWithTimeout(ssid, password);
  if (offlineMode) {
    drawNoWifiScreen(tft, "Idojaras");
    return;
  }
  
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(10, 50);
  tft.print("Adatok letoltese...");
  
  delay(1000); 
  getWeatherData();
  lastWeatherUpdate = millis(); 
  renderWeatherScreen();
}

void loop() {
  esp_task_wdt_reset();
  if (offlineMode) return;   // UJ: "Nincs WiFi" kepernyo marad
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      esp_task_wdt_reset();
      attempts++;
    }
  }

  if (currentMillis - lastScreenSwitch >= screenSwitchDelay) {
    lastScreenSwitch = currentMillis;
    if (current_weather.valid) {
      currentScreen++;
      if (currentScreen > 3) currentScreen = 1; 
      renderWeatherScreen();
    }
  }

  if (currentMillis - lastWeatherUpdate >= weatherTimerDelay) {
    lastWeatherUpdate = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      getWeatherData();
      renderWeatherScreen(); 
    }
  }
}

void getWeatherData() {
  HTTPClient http;
  
  // Lekérünk 4 napot, a mai nap kihagyásával a következő 3 tiszta jövőbeli napért
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" CFG_STR(CFG_WEATHER_LAT) "&longitude=" CFG_STR(CFG_WEATHER_LON) "&current=temperature_2m,relativehumidity_2m,precipitation,weathercode,cloudcover,windspeed_10m,winddirection_10m&hourly=uv_index,precipitation_probability&daily=sunrise,sunset,temperature_2m_max&timezone=Europe/Budapest&forecast_days=4";
  
  http.begin(url);
  http.setTimeout(15000); 
  
  int httpResponseCode = http.GET();
  Serial.print("HTTP Kod: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode == 200) {
    String response = http.getString();
    
    DynamicJsonDocument doc(12288);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("JSON Hiba: ");
      Serial.println(error.c_str());
      weatherFetchError = true;
      http.end();
      return;
    }

    // 1. Eredeti Első oldal adatai (Változatlan)
    current_weather.temp = doc["current"]["temperature_2m"].as<float>();
    current_weather.humidity = doc["current"]["relativehumidity_2m"].as<int>();
    current_weather.precipitation = doc["current"]["precipitation"].as<float>();
    current_weather.weatherCode = doc["current"]["weathercode"].as<int>();
    current_weather.cloudCover = doc["current"]["cloudcover"].as<int>(); 
    current_weather.windSpeed = doc["current"]["windspeed_10m"].as<float>();
    current_weather.windDirection = doc["current"]["winddirection_10m"].as<int>();

    String currentTime = doc["current"]["time"].as<String>();
    int currentHour = currentTime.substring(11, 13).toInt();
    if (currentHour >= 0 && currentHour < 24) {
      current_weather.uvIndex = doc["hourly"]["uv_index"][currentHour].as<float>();
      current_weather.rainChance = doc["hourly"]["precipitation_probability"][currentHour].as<int>();
    }

    // 2. Eredeti Második oldal adatai (Változatlan)
    String raw_sunrise = doc["daily"]["sunrise"][0].as<String>(); 
    String raw_sunset = doc["daily"]["sunset"][0].as<String>();
    if (raw_sunrise.length() >= 16) current_weather.sunrise = raw_sunrise.substring(11, 16);
    if (raw_sunset.length() >= 16) current_weather.sunset = raw_sunset.substring(11, 16);

    // 3. A Harmadik oldal (Előrejelzés) holnaptól [1] kezdődő 3 nap mentése
    int currentYear = currentTime.substring(0, 4).toInt();
    int currentMonth = currentTime.substring(5, 7).toInt();
    int currentDay = currentTime.substring(8, 10).toInt();
    int baseDayOfWeek = calculateDayOfWeek(currentYear, currentMonth, currentDay);

    for (int i = 0; i < 3; i++) {
      int nextIdx = i + 1; 
      current_weather.dayOfWeek[i] = (baseDayOfWeek + nextIdx) % 7;
      current_weather.maxTemps[i] = doc["daily"]["temperature_2m_max"][nextIdx].as<float>();
    }

    current_weather.valid = true;
    hasWeatherPastData = true;
    weatherFetchError = false; 
  } else {
    weatherFetchError = true; 
  }
  http.end();
}

void renderWeatherScreen() {
  if (!current_weather.valid && !hasWeatherPastData) {
    tft.fillScreen(ST7735_BLACK);
    tft.setCursor(10, 50);
    tft.setTextColor(ST7735_RED);
    tft.print("Nincs idojaras adat!");
    return;
  }

  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);

  if (weatherFetchError) {
    tft.fillCircle(154, 5, 2, ST7735_RED);
  } else {
    tft.fillCircle(154, 5, 2, ST7735_GREEN);
  }

  if (currentScreen == 1) {
    // ────────────────────────────────────────────────────────
    // 1. KÉPERNYŐ: FŐ ADATOK (EREDETI, ÉRINTETLEN)
    // ────────────────────────────────────────────────────────
    tft.setCursor(5, 8);
    tft.setTextSize(2);
    tft.setTextColor(ST7735_CYAN);
    tft.print(CFG_CITY_NAME);

    tft.setCursor(100, 8);
    tft.setTextColor(ST7735_WHITE);
    tft.print((int)current_weather.temp); tft.print("C");

    tft.setCursor(5, 28);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_YELLOW);
    tft.print(getWeatherDesc(current_weather.weatherCode));

    tft.drawFastHLine(0, 40, 160, ST7735_GRAY);

    tft.setCursor(5, 48);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Szel: ");
    tft.setTextColor(ST7735_GREEN);
    tft.print((int)current_weather.windSpeed); tft.print(" km/h ");
    tft.print(getWindCompass(current_weather.windDirection));

    tft.setCursor(5, 64);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Paratartalom: ");
    tft.setTextColor(ST7735_MAGENTA);
    tft.print(current_weather.humidity); tft.print("%");

    tft.setCursor(5, 80);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Eso eselye:   ");
    tft.setTextColor(ST7735_BLUE);
    tft.print(current_weather.rainChance); tft.print("%");

    tft.drawFastHLine(0, 95, 160, ST7735_GRAY);

    tft.setCursor(5, 105);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Felho: ");
    tft.setTextColor(ST7735_WHITE);
    tft.print(current_weather.cloudCover); tft.print("%   ");
    
    tft.setTextColor(ST7735_GRAY);
    tft.print("UV: ");
    tft.setTextColor(ST7735_ORANGE);
    tft.print(current_weather.uvIndex, 0);

  } else if (currentScreen == 2) {
    // ────────────────────────────────────────────────────────
    // 2. KÉPERNYŐ: NAP ADATOK + RÉSZLETEK (EREDETI, ÉRINTETLEN)
    // ────────────────────────────────────────────────────────
    tft.setCursor(5, 8);
    tft.setTextColor(ST7735_ORANGE);
    tft.print("Napfelkelte: ");
    tft.setTextColor(ST7735_WHITE);
    tft.print(current_weather.sunrise);

    tft.setCursor(5, 22);
    tft.setTextColor(ST7735_MAGENTA);
    tft.print("Napnyugta:   ");
    tft.setTextColor(ST7735_WHITE);
    tft.print(current_weather.sunset);

    tft.drawFastHLine(0, 34, 160, ST7735_GRAY);

    tft.setCursor(5, 42);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Homerseklet:  ");
    tft.setTextColor(ST7735_WHITE);
    if(current_weather.temp > 0) tft.print("+");
    tft.print(current_weather.temp, 1); tft.print(" C");

    tft.setCursor(5, 56);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Szelseb.:     ");
    tft.setTextColor(ST7735_GREEN);
    tft.print(current_weather.windSpeed, 1); tft.print(" km/h");

    tft.setCursor(5, 70);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Szelirany:    ");
    tft.setTextColor(ST7735_GREEN);
    tft.print(getWindCompass(current_weather.windDirection));
    tft.print(" "); tft.print(current_weather.windDirection); tft.print(" deg");

    tft.setCursor(5, 84);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Paratartalom: ");
    tft.setTextColor(ST7735_MAGENTA);
    tft.print(current_weather.humidity); tft.print("%");

    tft.setCursor(5, 98);
    tft.setTextColor(ST7735_GRAY);
    tft.print("Csapadek:     ");
    tft.setTextColor(ST7735_BLUE);
    tft.print(current_weather.precipitation, 1); tft.print(" mm");

  } else if (currentScreen == 3) {
    // ────────────────────────────────────────────────────────
    // 3. KÉPERNYŐ: KÖVETKEZŐ NAPOK (CSAK A MAX HŐMÉRSÉKLET)
    // ────────────────────────────────────────────────────────
    tft.setCursor(5, 8);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_YELLOW);
    tft.print("KOVETKEZO NAPOK");

    tft.drawFastHLine(0, 20, 160, ST7735_GRAY);

    // Kirajzolás szép, tiszta sorközzel
    for (int i = 0; i < 3; i++) {
      int yPos = 32 + (i * 30); 

      // 1. Oszlop: Nap neve ciánkék színnel
      tft.setCursor(5, yPos);
      tft.setTextColor(ST7735_CYAN);
      if (i == 0) tft.print("Holnap:");
      else tft.print(napokNevei[current_weather.dayOfWeek[i]]);

      // 2. Oszlop: Csúcshőmérséklet fehér színnel (Szépen jobbra igazítva a 115-ös pixelre)
      tft.setCursor(115, yPos);
      tft.setTextColor(ST7735_WHITE);
      tft.print((int)current_weather.maxTemps[i]); tft.print(" C");

      // Vízszintes elválasztó csík a sorok között
      if (i < 2) {
        tft.drawFastHLine(0, yPos + 18, 160, ST7735_DARKGRAY);
      }
    }
  }
}

} // namespace Idojaras

#endif
