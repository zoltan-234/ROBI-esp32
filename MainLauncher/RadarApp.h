#ifndef RADAR_APP_H
#define RADAR_APP_H

// ==========================================================
//  Radar + ATC Radio
//  --------------------------------------------------------
//  Az EREDETI repulo_radar.ino valtozatlan adatfeldolgozo
//  magja (checkSpecialCallsign, checkAirline, getCompass,
//  renderPlane, getAircraftData) + UJ: 3 oldalas ATC radio
//  (MAX98357-M + ESP32-audioI2S 3.2.1).
//
//  FONTOS - egyszeri audio-inicializalas:
//  Mivel a launcher minden Radar-belepeskor ujra lefuttatja
//  ezt a setup()-ot ("teljes ujrainditassal"), az Audio
//  objektum setPinout()/connecttohost() hivasait egy
//  'audioInitialized' vedojelzovel csak ELOSZOR engedjuk le -
//  igy nem fordulhat elo ismetelt ujrainicializalas, ami
//  strukturalisan ugyanaz a hibaminta lenne, mint a MiniFace
//  canvas-fragmentacios osszeomlas anno.
// ==========================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "Audio.h"
#include "config.h"
#include "WifiGuard.h"

namespace Radar {

// ============================================================
// EREDETI - VALTOZATLAN
// ============================================================

// Wi-Fi adatok
const char* ssid = SECRET_WIFI_SSID;          // -> secrets.h
const char* password = SECRET_WIFI_PASSWORD;  // -> secrets.h

// OPENSKY FIOK ADATOK
const char* opensky_user = SECRET_OPENSKY_USER;  // -> secrets.h (ures = anonim)
const char* opensky_pass = SECRET_OPENSKY_PASS;

// Kijelzo labak
#define TFT_CS         5
#define TFT_RST        4
#define TFT_DC         2
#define ST7735_GRAY       0x8410
#define ST7735_DARKGRAY   0x2104
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

String lamin = CFG_RADAR_LAMIN;   // -> config.h
String lomin = CFG_RADAR_LOMIN;
String lamax = CFG_RADAR_LAMAX;
String lomax = CFG_RADAR_LOMAX;

unsigned long lastTime = 0;
unsigned long timerDelay = 120000;
unsigned long lastDisplaySwitch = 0;
unsigned long displaySwitchDelay = 15000;

unsigned long lastSecTime = 0;
unsigned long lastScrollTime = 0;
int scrollPos = 160;

struct PlaneData {
  String callsign;
  String airline;
  String routeStatus;
  String country;
  int altFeet;
  int speedKmh;
  int speedKnots;
  int heading;
  bool valid = false;
  bool isSpecial = false;
};

PlaneData selectedPlanes[8];
int currentDisplayIndex = 0;
int actualSelectedCount = 0;
int totalPlanesCount = 0;
String globalLajstromok = "";
bool hasPastData = false;

// ============================================================
// UJ - AUDIO (MAX98357)
// ============================================================
#define RADAR_I2S_BCLK  14
#define RADAR_I2S_LRC   13
#define RADAR_I2S_DOUT  21
Audio audio;

struct Channel {
  const char* name;
  const char* freq;
  const char* url;
};

Channel channels[] = {
  CFG_ATC_CHANNELS   // -> config.h
};
const int CHANNEL_COUNT = sizeof(channels) / sizeof(channels[0]);
int currentChannel = 0;
int volume = 15;
bool offlineMode = false;      // UJ: nincs WiFi -> a loop nem csinal semmit
bool audioInitialized = false; // UJ: vedojelzo - namespace-szinten EGYSZER all be true-ra a program teljes futasa alatt

// ============================================================
// UJ - GOMBOK
// ============================================================
#define RADAR_BTN_A  26   // rovid: oldalvaltas | hosszan: hangero le
#define RADAR_BTN_B  27   // rovid: csatornavaltas (2./3. oldalon) | hosszan: hangero fel

#define RADAR_LONG_PRESS_MS   500
#define RADAR_REPEAT_MS       150
#define RADAR_OVERLAY_HOLD_MS 1500

struct ButtonState {
  int pin;
  bool pressed = false;
  unsigned long pressStart = 0;
  bool longTriggered = false;
  unsigned long lastRepeat = 0;
  bool lastReading = HIGH;
  unsigned long lastChangeTime = 0;
};
ButtonState btnA = { RADAR_BTN_A };
ButtonState btnB = { RADAR_BTN_B };

// ============================================================
// UJ - OLDAL / OVERLAY ALLAPOTGEP
// ============================================================
enum Page { PAGE_RADAR, PAGE_RADIO, PAGE_SIMPLE };
Page currentPage = PAGE_RADAR;

enum OverlayType { OVERLAY_NONE, OVERLAY_VOLUME, OVERLAY_CHANNEL };
OverlayType activeOverlay = OVERLAY_NONE;
unsigned long overlayUntil = 0;

// ============================================================
// UJ - EGYSZERUSITETT RADAR ADATOK (3. oldal)
// ============================================================
struct SimplePlaneData {
  String callsign;
  bool isLanding; // true = leszallo, false = felszallo
  int altFeet;
  int speedKmh;
  bool valid = false;
};
SimplePlaneData simplePlanes[8];
int simpleCount = 0;
int simpleDisplayIndex = 0;
unsigned long lastSimpleSwitch = 0;
const unsigned long simpleSwitchDelay = 8000;

// --- elore-deklaraciok (namespace-ben ez segit a hivasi sorrendnek) ---
void drawCountdown();
void drawStaticUI(bool ok, int errorCode);
int checkSpecialCallsign(String callsign);
String checkAirline(String callsign, bool &isMil);
String getCompass(float heading);
void getAircraftData();
void renderPlane(PlaneData plane);
void connectChannel(int index, bool showOverlay);
void changeVolume(int delta);
void nextPage();
void redrawCurrentPage();
void drawRadioPageNormal();
void drawSimplePageNormal();
void renderSimplePlane(SimplePlaneData p);
void drawVolumeOverlay();
void drawVolumeOverlayLight(bool up);
void drawChannelOverlay();
void handleButton(ButtonState &btn, bool isA);
void stopAudio();

// ============================================================
// SETUP
// ============================================================
void setup() {
  totalPlanesCount = 0;
  currentPage = PAGE_RADAR; // UJ: minden belepes 1. oldallal indul (teljes ujrainditas filozofia)

  esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 60000,
  .idle_core_mask = 0,
  .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.begin(115200);

  pinMode(RADAR_BTN_A, INPUT_PULLUP);
  pinMode(RADAR_BTN_B, INPUT_PULLUP);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);

  tft.setCursor(10, 30);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.print("Csatlakozas a WiFi-hez...");

  // UJ: idokorlatos csatlakozas (WifiGuard.h) - WiFi nelkul nem ragad be
  offlineMode = !wifiConnectWithTimeout(ssid, password);
  if (offlineMode) {
    drawNoWifiScreen(tft, "Repulo radar");
    return;
  }

  tft.fillScreen(ST7735_BLACK);
  drawStaticUI(true, 0);
  getAircraftData(); // ha ez az elso futas, audioInitialized meg false -> biztonsagosan kihagyja a hang-reszt

  // UJ: az I2S labkiosztas CSAK EGYSZER allitodik be, a program teljes futasa alatt -
  // ismetelt Radar-belepesnel ez mar nem fut le ujra (audioInitialized vedi).
  // A connectChannel() viszont MINDEN belepeskor lefut, mert amikor elhagyod a Radart,
  // a leaveProgram() leallitja a hangot (lasd stopAudio()) - ezert visszalepeskor
  // ujra kell inditani a lejatszast, kulonben nema maradna.
  if (!audioInitialized) {
    audio.setPinout(RADAR_I2S_BCLK, RADAR_I2S_LRC, RADAR_I2S_DOUT);
    audioInitialized = true;
  }
  connectChannel(0, false);
}

// UJ: a launcher hivja meg, amikor elhagyod a Radar programot - leallitja a
// hangot, hogy ne maradjon nyitva a HTTPS-kapcsolat a hatterben a kovetkezo
// program alatt (ez okozta a WiFi-osszeomlast programvaltasnal).
void stopAudio() {
  if (audioInitialized) {
    Serial.print("stopAudio ELOTT - legnagyobb blokk: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    audio.stopSong();

    // UJ: kiurito ciklus - ugyanaz a mintazat, mint connectChannel()-ben.
    // A stopSong() onmagaban nem biztos, hogy azonnal, teljesen leallitja a
    // dekodert (MP3/AAC) - ha epp feldolgozas kozben volt, folytathatta volna
    // a hattterben, miutan mar a kovetkezo program (pl. MiniFace) atvette az
    // iranyitast, es ez okozott osszeomlast a kozos vermen/memorian.
    for (int i = 0; i < 8; i++) {
      audio.loop();
      delay(10);
    }

    Serial.print("stopAudio UTAN  - legnagyobb blokk: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  esp_task_wdt_reset();
  if (offlineMode) return;   // UJ: "Nincs WiFi" kepernyo marad, a gombok mukodnek
  audio.loop();          // UJ - mindig fusson, fuggetlenul az oldaltol
  handleButton(btnA, true);   // UJ
  handleButton(btnB, false);  // UJ

  // UJ: overlay lejarat-ellenorzes
  if (activeOverlay != OVERLAY_NONE && millis() > overlayUntil) {
    activeOverlay = OVERLAY_NONE;
    redrawCurrentPage();
  }

  // UJ: amig overlay lathato, semmi mas ne irjon a kijelzore
  if (activeOverlay != OVERLAY_NONE) {
    return;
  }

  // EREDETI - WiFi reconnect (oldaltol fuggetlenul mindig fut)
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi kiesett, ujracsatlakozas...");
    if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) drawStaticUI(false, -1);
    WiFi.reconnect();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      esp_task_wdt_reset();
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi visszaall!");
      if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) drawStaticUI(true, 0);
    }
  }

  unsigned long currentMillis = millis();

  // UJ: Visszaszamlalo - az 1. ES a 3. oldalon is fusson
  if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) {
    if (currentMillis - lastSecTime >= 1000) {
      lastSecTime = currentMillis;
      drawCountdown();
    }
  }

  // EREDETI blokkok - csak PAGE_RADAR-on futnak (UJ feltetel korulottuk)
  if (currentPage == PAGE_RADAR) {

    // 2. Futoszoveg
    if (globalLajstromok.length() > 0 && currentMillis - lastScrollTime >= 40) {
      lastScrollTime = currentMillis;

      tft.setTextSize(1);
      tft.fillRect(0, 118, 160, 12, ST7735_DARKGRAY);
      tft.setTextColor(ST7735_WHITE, ST7735_DARKGRAY);

      tft.setCursor(scrollPos, 120);
      tft.print(globalLajstromok);

      scrollPos -= 1;

      int textWidth = globalLajstromok.length() * 6;
      if (scrollPos < -textWidth) {
        scrollPos = 160;
      }
    }

    // 3. 15 mp-es gepvaltas
    if (currentMillis - lastDisplaySwitch >= displaySwitchDelay) {
      lastDisplaySwitch = currentMillis;
      if (actualSelectedCount > 0) {
        int attempts = 0;
        do {
          currentDisplayIndex = (currentDisplayIndex + 1) % 8;
          attempts++;
        } while (!selectedPlanes[currentDisplayIndex].valid && attempts < 8);

        if (selectedPlanes[currentDisplayIndex].valid) {
          renderPlane(selectedPlanes[currentDisplayIndex]);
        }
      }
    }
  }

  // UJ: 3. oldal - egyszerusitett gepvaltas
  if (currentPage == PAGE_SIMPLE) {
    if (currentMillis - lastSimpleSwitch >= simpleSwitchDelay) {
      lastSimpleSwitch = currentMillis;
      if (simpleCount > 0) {
        simpleDisplayIndex = (simpleDisplayIndex + 1) % simpleCount;
        renderSimplePlane(simplePlanes[simpleDisplayIndex]);
      }
    }
  }

  // EREDETI - 2 perces fo frissites, MINDIG fut, oldaltol fuggetlenul
  if (currentMillis - lastTime > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      getAircraftData();
    }
    lastTime = currentMillis;
    lastDisplaySwitch = currentMillis;
    lastSecTime = currentMillis;
  }
}

// ============================================================
// EREDETI - VALTOZATLAN FUGGVENYEK
// ============================================================

// UJ: kozos visszaszamlalo az 1. es 3. oldalhoz
void drawCountdown() {
  long remainingSec = (timerDelay - (millis() - lastTime)) / 1000;
  if (remainingSec < 0) remainingSec = 0;
  if (remainingSec > 120) remainingSec = 120;

  tft.setTextSize(1);
  tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
  tft.setCursor(135, 5);
  if (remainingSec < 10) tft.print("0");
  tft.print(remainingSec);
  tft.print("s ");
}

void drawStaticUI(bool ok, int errorCode) {
  tft.fillRect(5, 5, 80, 10, ST7735_BLACK);
  tft.setCursor(5, 5);
  tft.setTextSize(1);
  if (ok) {
    tft.setTextColor(ST7735_GREEN);
    tft.print("WiFi OK");
  } else {
    tft.setTextColor(ST7735_RED);
    tft.print("API ERR: ");
    tft.print(errorCode);
  }
}

int checkSpecialCallsign(String callsign) {
  callsign.trim();
  callsign.toUpperCase();

  if (callsign == "R906"     || callsign == "R-906")     return 1;
  if (callsign == "R907"     || callsign == "R-907")     return 1;
  if (callsign == "R908"     || callsign == "R-908")     return 1;
  if (callsign == "RENDOR01" || callsign == "RENDOR-01") return 1;
  if (callsign == "RENDOR1"  || callsign == "RENDOR-1")  return 1;

  if (callsign == "MEDIC01"  || callsign == "MEDIC-01")  return 2;
  if (callsign == "MEDIC02"  || callsign == "MEDIC-02")  return 2;
  if (callsign == "MEDIC1"   || callsign == "MEDIC-1")   return 2;
  if (callsign == "MENTO01"  || callsign == "MENTO-01")  return 2;
  if (callsign == "HEMS01"   || callsign == "HEMS-01")   return 2;
  if (callsign == "HEMS1"    || callsign == "HEMS-1")    return 2;

  if (callsign == "SHADOW01" || callsign == "SHADOW-01") return 3;
  if (callsign == "SHADOW1"  || callsign == "SHADOW-1")  return 3;
  if (callsign == "SHADOW02" || callsign == "SHADOW-02") return 3;
  if (callsign == "HUNAF01"  || callsign == "HUNAF-01")  return 3;
  if (callsign == "HUNAF1"   || callsign == "HUNAF-1")   return 3;

  return 0;
}

String checkAirline(String callsign, bool &isMil) {
  isMil = false;
  if (callsign.length() < 3) return "None";

  if (callsign.startsWith("HUN"))    { isMil = true; return "HU-Military"; }
  if (callsign.startsWith("PUMA"))   { isMil = true; return "HU-Military"; }
  if (callsign.startsWith("AIRPOL")) { isMil = true; return "HU-Rendorseg"; }
  if (callsign.startsWith("HAWK"))   { isMil = true; return "HU-Military"; }
  if (callsign.startsWith("FALCON")) { isMil = true; return "HU-Military"; }

  String prefix = callsign.substring(0, 3);

  if (prefix == "HUA" || prefix == "HNA") { isMil = true; return "HU-Military"; }
  if (prefix == "BAF") { isMil = true; return "BE-Military"; }
  if (prefix == "GAF") { isMil = true; return "DE-Military"; }
  if (prefix == "IAM") { isMil = true; return "IT-Military"; }
  if (prefix == "AME") { isMil = true; return "ES-Military"; }
  if (prefix == "RFR") { isMil = true; return "UK-Military"; }
  if (prefix == "NAF") { isMil = true; return "NO-Military"; }
  if (prefix == "DAF") { isMil = true; return "DK-Military"; }
  if (prefix == "SVF") { isMil = true; return "SE-Military"; }
  if (prefix == "FAF") { isMil = true; return "FI-Military"; }
  if (prefix == "FAM") { isMil = true; return "FR-Military"; }
  if (prefix == "NED") { isMil = true; return "NL-Military"; }
  if (prefix == "PAF") { isMil = true; return "PL-Military"; }
  if (prefix == "CAF") { isMil = true; return "CA-Military"; }
  if (prefix == "RFF") { isMil = true; return "RU-Military"; }
  if (prefix == "CHF") { isMil = true; return "CN-Military"; }
  if (prefix == "UAF" || prefix == "CNV" || prefix == "RCH" || prefix == "SAM") { isMil = true; return "US-Military"; }
  if (prefix == "MMF") { isMil = true; return "NATO - MIL"; }
  if (prefix == "IAF") { isMil = true; return "IL-Military"; }
  if (prefix == "TUR") { isMil = true; return "TR-Military"; }
  if (prefix == "GRC") { isMil = true; return "GR-Military"; }
  if (prefix == "CSH") { isMil = true; return "CZ-Military"; }
  if (prefix == "SVK") { isMil = true; return "SK-Military"; }
  if (prefix == "ROU") { isMil = true; return "RO-Military"; }
  if (prefix == "BUL") { isMil = true; return "BG-Military"; }
  if (prefix == "CFC") { isMil = true; return "CA-Military"; }

  if (prefix == "WZZ") return "Wizz Air";
  if (prefix == "WUK") return "Wizz Air UK";
  if (prefix == "WMT") return "Wizz Air Ma";
  if (prefix == "RYR") return "Ryanair";
  if (prefix == "TVS") return "Smartwings";
  if (prefix == "EZY") return "easyJet";
  if (prefix == "EJU") return "easyJet EU";
  if (prefix == "BAW") return "British AW";
  if (prefix == "SHT") return "BA Shuttle";
  if (prefix == "DLH") return "Lufthansa";
  if (prefix == "GEC") return "Lufthansa";
  if (prefix == "CLH") return "LH CityLine";
  if (prefix == "AUA") return "Austrian";
  if (prefix == "SWR") return "Swiss";
  if (prefix == "AFR") return "Air France";
  if (prefix == "FCM") return "AF Hop";
  if (prefix == "KLM") return "KLM";
  if (prefix == "IBE") return "Iberia";
  if (prefix == "VLG") return "Vueling";
  if (prefix == "IBS") return "Iberia Exp";
  if (prefix == "AZA") return "ITA Airways";
  if (prefix == "AZI") return "Aeroitalia";
  if (prefix == "NOS") return "Neos";
  if (prefix == "LOT") return "LOT Poland";
  if (prefix == "CSA") return "Czech AL";
  if (prefix == "ROT") return "TAROM";
  if (prefix == "BTI") return "Air Baltic";
  if (prefix == "BMS") return "Belavia";
  if (prefix == "SAS") return "SAS";
  if (prefix == "NAX") return "Norwegian";
  if (prefix == "IBK") return "Norwegian I";
  if (prefix == "FIN") return "Finnair";
  if (prefix == "BEL") return "Brussels AL";
  if (prefix == "TRA") return "Transavia";
  if (prefix == "EWG") return "Eurowings";
  if (prefix == "EWE") return "EW Europe";
  if (prefix == "CFG") return "Condor";
  if (prefix == "TUI") return "TUI Belgium";
  if (prefix == "TUC") return "TUI fly";
  if (prefix == "TAR") return "Tunisair";
  if (prefix == "FHY") return "Freebird AL";
  if (prefix == "PGT") return "Pegasus";
  if (prefix == "THY") return "Turkish AL";
  if (prefix == "AHY") return "Azerbaijan";
  if (prefix == "UAR") return "Ukraine AL";
  if (prefix == "AFL") return "Aeroflot";
  if (prefix == "SDM") return "Rossiya";
  if (prefix == "UTA") return "UTair";

  if (prefix == "UAE") return "Emirates";
  if (prefix == "ETD") return "Etihad";
  if (prefix == "QTR") return "Qatar";
  if (prefix == "GFA") return "Gulf Air";
  if (prefix == "FDB") return "flydubai";
  if (prefix == "ABY") return "Air Arabia";
  if (prefix == "MEA") return "Mid East AL";
  if (prefix == "IAW") return "Iraqi";
  if (prefix == "ELY") return "El Al";
  if (prefix == "ISR") return "Arkia";
  if (prefix == "IRA") return "Iran Air";
  if (prefix == "THA") return "Thai";
  if (prefix == "BAV") return "Bangkok";
  if (prefix == "SIA") return "Singapore";
  if (prefix == "CPA") return "Cathay Pac";
  if (prefix == "CCA") return "Air China";
  if (prefix == "CSN") return "China South";
  if (prefix == "CES") return "China East";
  if (prefix == "CAL") return "China AL";
  if (prefix == "KAL") return "Korean Air";
  if (prefix == "AAR") return "Asiana";
  if (prefix == "MAS") return "Malaysia AL";
  if (prefix == "GIA") return "Garuda";
  if (prefix == "PAL") return "Philippine";
  if (prefix == "PIA") return "Pakistan AL";
  if (prefix == "AIC") return "Air India";
  if (prefix == "IGO") return "IndiGo";

  if (prefix == "AAL") return "American AL";

  if (prefix == "ETH") return "Ethiopian";
  if (prefix == "KQA") return "Kenya";
  if (prefix == "MSR") return "EgyptAir";
  if (prefix == "RAM") return "Royal Maroc";
  if (prefix == "SAA") return "S African AL";
  if (prefix == "QFA") return "Qantas";
  if (prefix == "CLX") return "Cargolux";
  if (prefix == "TUA") return "Turkmenistan";
  if (prefix == "TWI") return "TailWind";
  if (prefix == "CAO") return "AirChina";
  if (prefix == "TAY") return "Belgium AL";
  if (prefix == "AZG") return "Silk Way";

  if (prefix == "SXS") return "SunExpress";
  if (prefix == "GTI") return "Atlas Air";
  if (prefix == "MGH") return "Mavi Gok";
  if (prefix == "KZR") return "Air Astana";
  if (prefix == "EWG") return "Eurowings";
  if (prefix == "VKG") return "Sunclass Airlines";
  if (prefix == "ADB") return "ANTONOV An-124";

  return "None";
}

String getCompass(float heading) {
  if (heading >= 337.5 || heading < 22.5)  return "E";
  if (heading >= 22.5  && heading < 67.5)  return "EK";
  if (heading >= 67.5  && heading < 112.5) return "K";
  if (heading >= 112.5 && heading < 157.5) return "DK";
  if (heading >= 157.5 && heading < 202.5) return "D";
  if (heading >= 202.5 && heading < 247.5) return "DNY";
  if (heading >= 247.5 && heading < 292.5) return "NY";
  if (heading >= 292.5 && heading < 337.5) return "ENY";
  return "-";
}

void getAircraftData() {

  Serial.print("Szabad heap API hivas elott: ");
  Serial.println(ESP.getFreeHeap());

  // UJ: hang szuneteltetese - ket egyideju TLS kapcsolat kifogyaszthatja a RAM-ot
  bool audioWasPlaying = audioInitialized && audio.isRunning();
  if (audioWasPlaying) {
    audio.stopSong();
  }

  int httpResponseCode = -1;
  String response = "";

  {
    // UJ: kulon blokk - a client/http itt fut ki a scope-bol, meg mielott
    // barmi mas (hiba, feldolgozas, hang-visszakapcsolas) kovetkezne.
    // Igy sose verseng ket TLS kapcsolat egyszerre a RAM-ert.
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://opensky-network.org/api/states/all?lamin=" + lamin + "&lomin=" + lomin + "&lamax=" + lamax + "&lomax=" + lomax;
    http.begin(client, url);
    http.setTimeout(15000);
    http.setUserAgent("ESP32-Radar");
    if (strlen(opensky_user) > 0) http.setAuthorization(opensky_user, opensky_pass);  // UJ: ures = anonim

    httpResponseCode = http.GET();

    Serial.print("HTTP valasz kod: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode == 200) {
      response = http.getString();
    }

    http.end();
  } // client es http megsemmisul itt - felszabadul a TLS memoria

  // UJ: hang visszainditasa MOST, MEG a JSON-feldolgozas es barmilyen hibaag ELOTT -
  // igy egy esetleges JSON-hiba miatti korai visszateres sem hagyhatja nemán a radiot
  if (audioWasPlaying) {
    audio.connecttohost(channels[currentChannel].url);
    audio.setVolume(volume);
    audio.setTone(0, -6, -6);
  }

  Serial.print("Szabad heap API hivas utan: ");
  Serial.println(ESP.getFreeHeap());

  if (httpResponseCode == 200) {
    Serial.print("Valasz meret (byte): ");
    Serial.println(response.length());

    // 49152 -> 16384: a mert valaszmeretek 1030-1561 byte kozott voltak, tehat
    // 16KB meg mindig ~10x tartalek. A kisebb puffer tobb helyet hagy a
    // MiniFace canvas-anak, javitva az eselyt, hogy a foglalasa sikeruljon.
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("JSON hiba: ");
      Serial.println(error.c_str());
      if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) drawStaticUI(false, -99);
      return;
    }

    if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) drawStaticUI(true, 0);

    for (int i = 0; i < 8; i++) {
      selectedPlanes[i].valid = false;
      selectedPlanes[i].isSpecial = false;
    }
    actualSelectedCount = 0;
    globalLajstromok = "CallSigns: ";

    JsonArray states = doc["states"];

    Serial.print("states.size() (nyers): ");
    Serial.println(states.size());

    int milIndexes[20];   int milCount = 0;
    int upIndexes[20];    int upCount = 0;
    int downIndexes[20];  int downCount = 0;
    int transIndexes[40]; int transCount = 0;

    int validPlanesFound = 0;
    int groundSkipped = 0;
    int nullSkipped = 0;
    int noCallsignSkipped = 0;

    for (int i = 0; i < (int)states.size(); i++) {
      if (states[i].isNull()) { nullSkipped++; continue; }
      JsonArray plane = states[i];

      bool onGround = plane[8].as<bool>();
      if (onGround) { groundSkipped++; continue; }

      String callsign = plane[1].as<String>();
      callsign.trim();
      if (callsign.length() == 0) { noCallsignSkipped++; continue; }

      validPlanesFound++;
      globalLajstromok += callsign + " | ";

      bool isMil = false;
      checkAirline(callsign, isMil);

      int specType = checkSpecialCallsign(callsign);

      float altitude = plane[7].as<float>();
      float verticalRate = plane[11].as<float>();
      int altFeet = altitude * 3.28084;

      if (specType > 0 && milCount < 20) {
        milIndexes[milCount++] = i;
      } else if (isMil && milCount < 20) {
        milIndexes[milCount++] = i;
      } else if (altFeet < 4000 && verticalRate > 0.5 && upCount < 20) {
        upIndexes[upCount++] = i;
      } else if (altFeet < 15000 && verticalRate < -0.5 && downCount < 20) {
        downIndexes[downCount++] = i;
      } else if (transCount < 40) {
        transIndexes[transCount++] = i;
      }
    }

    totalPlanesCount = validPlanesFound;

    if (validPlanesFound == 0) {
      globalLajstromok = " Tiszta az eg! Nincs aktiv repulo. ";
    }

    // UJ: 3. oldalhoz - kulon lista, csak a fel-/leszallo gepekbol
    int simpleFilled = 0;
    for (int i = 0; i < upCount && simpleFilled < 8; i++) {
      JsonArray plane = states[upIndexes[i]];
      simplePlanes[simpleFilled].callsign = plane[1].as<String>();
      simplePlanes[simpleFilled].callsign.trim();
      float altitude = plane[7].as<float>();
      simplePlanes[simpleFilled].altFeet = altitude * 3.28084;
      float speed = plane[9].as<float>();
      simplePlanes[simpleFilled].speedKmh = speed * 3.6;
      simplePlanes[simpleFilled].isLanding = false;
      simplePlanes[simpleFilled].valid = true;
      simpleFilled++;
    }
    for (int i = 0; i < downCount && simpleFilled < 8; i++) {
      JsonArray plane = states[downIndexes[i]];
      simplePlanes[simpleFilled].callsign = plane[1].as<String>();
      simplePlanes[simpleFilled].callsign.trim();
      float altitude = plane[7].as<float>();
      simplePlanes[simpleFilled].altFeet = altitude * 3.28084;
      float speed = plane[9].as<float>();
      simplePlanes[simpleFilled].speedKmh = speed * 3.6;
      simplePlanes[simpleFilled].isLanding = true;
      simplePlanes[simpleFilled].valid = true;
      simpleFilled++;
    }
    for (int i = simpleFilled; i < 8; i++) simplePlanes[i].valid = false;
    simpleCount = simpleFilled;
    simpleDisplayIndex = 0;

    int chosenStatesIndexes[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
    int filledSlots = 0;

    for (int i = 0; i < milCount && filledSlots < 8; i++) {
      chosenStatesIndexes[filledSlots++] = milIndexes[i];
    }
    if (filledSlots < 8 && upCount > 0)    chosenStatesIndexes[filledSlots++] = upIndexes[0];
    if (filledSlots < 8 && downCount > 0)  chosenStatesIndexes[filledSlots++] = downIndexes[0];

    for (int i = 0; i < transCount && filledSlots < 8; i++) {
      bool alreadyChosen = false;
      for (int j = 0; j < filledSlots; j++) {
        if (chosenStatesIndexes[j] == transIndexes[i]) alreadyChosen = true;
      }
      if (!alreadyChosen) {
        chosenStatesIndexes[filledSlots++] = transIndexes[i];
      }
    }

    actualSelectedCount = filledSlots;

    for (int i = 0; i < 8; i++) {
      if (chosenStatesIndexes[i] != -1) {
        JsonArray plane = states[chosenStatesIndexes[i]];
        selectedPlanes[i].callsign = plane[1].as<String>();
        selectedPlanes[i].callsign.trim();

        bool isMil = false;
        selectedPlanes[i].airline = checkAirline(selectedPlanes[i].callsign, isMil);
        selectedPlanes[i].country = plane[2].as<String>();

        float heading = plane[10].as<float>();
        selectedPlanes[i].heading = (int)heading;

        float altitude = plane[7].as<float>();
        selectedPlanes[i].altFeet = altitude * 3.28084;

        float speed = plane[9].as<float>();
        selectedPlanes[i].speedKmh = speed * 3.6;
        selectedPlanes[i].speedKnots = speed * 1.94384;

        float verticalRate = plane[11].as<float>();

        int specType = checkSpecialCallsign(selectedPlanes[i].callsign);

        if (specType == 1) {
          selectedPlanes[i].routeStatus = "Rendor Heli";
          selectedPlanes[i].airline = "ORFK";
          selectedPlanes[i].isSpecial = true;
        } else if (specType == 2) {
          selectedPlanes[i].routeStatus = "Mento Heli";
          selectedPlanes[i].airline = "OMSZ";
          selectedPlanes[i].isSpecial = true;
        } else if (specType == 3) {
          selectedPlanes[i].routeStatus = "Katonai Helikopter";
          selectedPlanes[i].isSpecial = true;
        } else if (isMil) {
          selectedPlanes[i].routeStatus = "Katonai Bevetes";
        } else if (selectedPlanes[i].altFeet < 4000 && verticalRate > 0.5) {
          selectedPlanes[i].routeStatus = "Honnan: BUD -> ???";
        } else if (selectedPlanes[i].altFeet < 15000 && verticalRate < -0.5) {
          selectedPlanes[i].routeStatus = "Honnan: ??? -> BUD";
        } else {
          selectedPlanes[i].routeStatus = "Atrepulo";
        }
        selectedPlanes[i].valid = true;
      }
    }

    hasPastData = true;
    currentDisplayIndex = 0;

    if (currentPage == PAGE_RADAR && selectedPlanes[0].valid) {
      renderPlane(selectedPlanes[0]);
    } else if (currentPage == PAGE_SIMPLE) {
      if (simpleCount > 0) renderSimplePlane(simplePlanes[0]);
    }

  } else {
    if (currentPage == PAGE_RADAR || currentPage == PAGE_SIMPLE) {
      drawStaticUI(false, httpResponseCode);

      if (!hasPastData) {
        tft.fillRect(0, 18, 160, 100, ST7735_BLACK);
        tft.setCursor(5, 40);
        tft.setTextColor(ST7735_RED);
        tft.print("Nincs adat. Hiba: ");
        tft.print(httpResponseCode);
      }
    }
  }
}

void renderPlane(PlaneData plane) {

  tft.fillRect(0, 18, 160, 100, ST7735_BLACK);

  tft.fillRect(85, 0, 75, 12, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
  tft.setCursor(85, 5);
  tft.print("Db:");
  tft.print(totalPlanesCount);

  tft.setCursor(5, 20);
  tft.setTextSize(2);
  if (plane.isSpecial || plane.routeStatus == "Katonai Bevetes") {
    tft.setTextColor(ST7735_RED);
  } else {
    tft.setTextColor(ST7735_CYAN);
  }
  tft.print(plane.callsign);

  tft.setTextSize(1);
  tft.setTextColor(ST7735_GRAY);
  tft.print(" ");
  tft.print(plane.airline);

  tft.setCursor(5, 40);
  tft.setTextColor(ST7735_YELLOW);
  if (plane.routeStatus == "Rendor Heli") {
    tft.print("Rendor");
  } else if (plane.routeStatus == "Mento Heli") {
    tft.print("Mento Helikopter");
  } else if (plane.routeStatus == "Katonai Bevetes") {
    tft.print("Katonai Bevetes");
  } else {
    tft.print(plane.routeStatus);
  }

  tft.setCursor(5, 55);
  tft.setTextColor(ST7735_WHITE);
  int altM = plane.altFeet / 3.28084;

  if (plane.routeStatus == "Atrepulo" || plane.routeStatus == "Katonai Bevetes") {
    int fl = plane.altFeet / 100;
    tft.print("FL");
    tft.print(fl);
    tft.print("  ");
    tft.setTextColor(ST7735_GREEN);
    tft.print(plane.altFeet);
    tft.print("ft  ");
    tft.print(altM);
    tft.print("m");
  } else {
    tft.print("ALT.: ");
    tft.setTextColor(ST7735_GREEN);
    tft.print(plane.altFeet);
    tft.print("ft  ");
    tft.print(altM);
    tft.print("m");
  }

  tft.setCursor(5, 70);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Speed:  ");
  tft.setTextColor(ST7735_MAGENTA);
  tft.print(plane.speedKnots); tft.print(" kt  ");
  tft.print(plane.speedKmh); tft.print(" km/h");

  tft.setCursor(5, 85);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Irany:  ");
  tft.setTextColor(ST7735_ORANGE);
  tft.print(plane.heading); tft.print(" deg (");
  tft.print(getCompass(plane.heading)); tft.print(")");

  tft.setCursor(5, 100);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Base:   ");
  tft.setTextColor(ST7735_BLUE);
  tft.print(plane.country);
}

// ============================================================
// UJ - RADIO FUGGVENYEK
// ============================================================

void connectChannel(int index, bool showOverlay) {
  audio.stopSong();

  // UJ: rovid szunet + loop-hivasok, hogy a regi stream tenylegesen kiuruljon
  // a pufferbol/I2S-bol, mielott az uj kapcsolat elindulna - enelkul a ket
  // hangsav idoszakosan "egymasra" johet
  for (int i = 0; i < 8; i++) {
    audio.loop();
    delay(10);
  }

  currentChannel = index;
  audio.connecttohost(channels[currentChannel].url);
  audio.setVolume(volume);
  audio.setTone(0, -6, -6);

  if (showOverlay) {
    activeOverlay = OVERLAY_CHANNEL;
    overlayUntil = millis() + RADAR_OVERLAY_HOLD_MS;
    drawChannelOverlay();
  }
}

void changeVolume(int delta) {
  volume = constrain(volume + delta, 0, 21);
  audio.setVolume(volume);
  activeOverlay = OVERLAY_VOLUME;
  overlayUntil = millis() + RADAR_OVERLAY_HOLD_MS;

  if (currentPage == PAGE_SIMPLE) {
    drawVolumeOverlayLight(delta > 0);
  } else {
    drawVolumeOverlay();
  }
}

void audio_info(const char *info) {
  Serial.print("audio_info: "); Serial.println(info);
}

void audio_eof_stream(const char *info) {
  Serial.println("Stream vege, ujracsatlakozas...");
  audio.connecttohost(channels[currentChannel].url);
}

// ============================================================
// UJ - OLDALVALTAS
// ============================================================

void nextPage() {
  currentPage = (Page)((currentPage + 1) % 3);
  if (currentPage == PAGE_RADAR && currentChannel != 0) {
    connectChannel(0, false); // 1. oldal: mindig fix Approach 1, nema valtas
  }
  redrawCurrentPage();
}

void redrawCurrentPage() {
  switch (currentPage) {
    case PAGE_RADAR:
      tft.fillScreen(ST7735_BLACK);
      drawStaticUI(WiFi.status() == WL_CONNECTED, 0);
      if (selectedPlanes[currentDisplayIndex].valid) {
        renderPlane(selectedPlanes[currentDisplayIndex]);
      }
      break;
    case PAGE_RADIO:
      drawRadioPageNormal();
      break;
    case PAGE_SIMPLE:
      drawSimplePageNormal();
      break;
  }
}

// ============================================================
// UJ - 2. OLDAL (radio)
// ============================================================

void drawRadioPageNormal() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.setTextColor(ST7735_CYAN);
  tft.println("LHBP ATC Radio");

  tft.setTextSize(2);
  tft.setCursor(5, 35);
  tft.setTextColor(ST7735_WHITE);
  tft.println(channels[currentChannel].name);

  tft.setTextSize(1);
  tft.setCursor(5, 65);
  tft.setTextColor(ST7735_YELLOW);
  tft.print(channels[currentChannel].freq);
  tft.println(" MHz");
}

// ============================================================
// UJ - 3. OLDAL (egyszerusitett radar + hatter-audio)
// ============================================================

void drawSimplePageNormal() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.setTextColor(ST7735_CYAN);
  tft.print("Fel/Leszallok");

  if (simpleCount > 0 && simplePlanes[simpleDisplayIndex].valid) {
    renderSimplePlane(simplePlanes[simpleDisplayIndex]);
  } else {
    tft.setCursor(5, 40);
    tft.setTextColor(ST7735_WHITE);
    tft.print("Nincs fel/leszallo gep");
  }
}

void renderSimplePlane(SimplePlaneData p) {
  tft.fillRect(0, 18, 160, 100, ST7735_BLACK);

  tft.setCursor(5, 25);
  tft.setTextSize(2);
  tft.setTextColor(ST7735_CYAN);
  tft.print(p.callsign);

  tft.setTextSize(1);
  tft.setCursor(5, 50);
  if (p.isLanding) {
    tft.setTextColor(ST7735_ORANGE);
    tft.print("LESZALL");
  } else {
    tft.setTextColor(ST7735_GREEN);
    tft.print("FELSZALL");
  }

  tft.setCursor(5, 70);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Alt: ");
  tft.setTextColor(ST7735_YELLOW);
  tft.print(p.altFeet);
  tft.print(" ft");

  tft.setCursor(5, 85);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Speed: ");
  tft.setTextColor(ST7735_MAGENTA);
  tft.print(p.speedKmh);
  tft.print(" km/h");

  tft.setCursor(5, 105);
  tft.setTextColor(ST7735_CYAN);
  tft.print("Radio: ");
  tft.print(channels[currentChannel].name);
}

// ============================================================
// UJ - OVERLAY KEPERNYOK
// ============================================================

void drawVolumeOverlay() {
  tft.fillScreen(ST7735_BLACK);

  tft.setTextSize(2);
  tft.setCursor(15, 15);
  tft.setTextColor(ST7735_CYAN);
  tft.println("Hangero");

  tft.setTextSize(4);
  tft.setCursor(35, 45);
  tft.setTextColor(ST7735_WHITE);
  if (volume < 10) tft.print(" ");
  tft.print(volume);

  int barHeight = map(volume, 0, 21, 0, 30);
  tft.drawRect(20, 95, 118, 30, ST7735_WHITE);
  tft.fillRect(20, 95 + (30 - barHeight), 118, barHeight, ST7735_GREEN);
}

void drawVolumeOverlayLight(bool up) {
  tft.fillScreen(ST7735_BLACK);

  tft.setTextSize(2);
  tft.setCursor(15, 15);
  tft.setTextColor(ST7735_CYAN);
  tft.println("Hangero");

  int cx = 80, cy = 75;
  if (up) {
    tft.fillTriangle(cx - 25, cy + 20, cx + 25, cy + 20, cx, cy - 20, ST7735_GREEN);
  } else {
    tft.fillTriangle(cx - 25, cy - 20, cx + 25, cy - 20, cx, cy + 20, ST7735_RED);
  }
}

void drawChannelOverlay() {
  tft.fillScreen(ST7735_BLACK);

  tft.setTextSize(1);
  tft.setCursor(15, 20);
  tft.setTextColor(ST7735_CYAN);
  tft.println("Atvaltva:");

  tft.setTextSize(2);
  tft.setCursor(10, 55);
  tft.setTextColor(ST7735_WHITE);
  tft.println(channels[currentChannel].name);

  tft.setTextSize(1);
  tft.setCursor(15, 90);
  tft.setTextColor(ST7735_YELLOW);
  tft.print(channels[currentChannel].freq);
  tft.println(" MHz");
}

// ============================================================
// UJ - GOMBKEZELES
// ============================================================

void handleButton(ButtonState &btn, bool isA) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastReading) {
    btn.lastChangeTime = millis();
  }
  btn.lastReading = reading;

  if (millis() - btn.lastChangeTime < 30) return;

  bool isDown = (reading == LOW);

  if (isDown && !btn.pressed) {
    btn.pressed = true;
    btn.pressStart = millis();
    btn.longTriggered = false;
  }
  else if (isDown && btn.pressed) {
    unsigned long held = millis() - btn.pressStart;
    if (held > RADAR_LONG_PRESS_MS) {
      if (!btn.longTriggered || millis() - btn.lastRepeat > RADAR_REPEAT_MS) {
        changeVolume(isA ? -1 : +1);
        btn.longTriggered = true;
        btn.lastRepeat = millis();
      }
    }
  }
  else if (!isDown && btn.pressed) {
    if (!btn.longTriggered) {
      if (isA) {
        nextPage();
      } else {
        if (currentPage != PAGE_RADAR) {
          int newIndex = (currentChannel + 1) % CHANNEL_COUNT;
          connectChannel(newIndex, true);
        }
      }
    }
    btn.pressed = false;
  }
}

} // namespace Radar

#endif
