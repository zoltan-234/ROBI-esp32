#ifndef MINIFACE_APP_H
#define MINIFACE_APP_H

// ==========================================================
//  MiniFace  --  az EREDETI MiniFace.ino valtozatlan kodja,
//  csak namespace-be csomagolva, hogy a 4 program
//  egyutt eljen egy sketchben (nevutkozes nelkul).
// ==========================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "MiniFaceTypes.h"

namespace MiniFace {
/*
  ===========================================================
  MiniFace - ESP32 + ST7735 animalt karakter
  ===========================================================
  Teljes viselkedesi rendszer:
    - Napi ritmus (ebrenlet / lenyugvas / alvas) NTP ido alapjan
    - Oranekenti idojaras lekerdezes (Open-Meteo)
    - 2 gomb: D26 (simogatas / toltes-hosszu), D27 (jatek / menu-hosszu)
    - Ehseg-ciklus: random varakozas -> kor szem -> 20mp -> orom/szomoru
    - Szem-forma rendszer (lagy, blendelt atalakulas)
    - MiniFace jelenet-konyvtar (gondolkodas, orom, kacsintas, alom,
      kor-szekvencia, robbanas, cikazas, le-fel tunes)
    - Fej vagy iras jatek (D27 rovid nyomas)

  ELOTTE KITOLTENDO:
    - WiFi adatok: secrets.h (lasd secrets.example.h)
    - Szukseges konyvtarak: ArduinoJson, HTTPClient (ESP32 core resze)

  EGYSZERUSITESEK / FELTETELEZESEK:
    - A jatek "fej vagy iras": a keszulek maga sorsol egy oldalt es egy
      nyertes felet is (50-50%), a kepernyon kiirja az eredmenyt es hogy
      "Nyertem!" vagy "Te nyertel!" - nincs kulon gomb a valasztashoz,
      mivel csak 2 gomb all rendelkezesre, es azok mar foglaltak.
    - Az idojaras csak a homerskletet olvassa ki (Open-Meteo current_weather)
  ===========================================================
*/


// ---------------- KONFIG ----------------
#define TFT_CS 5
#define TFT_RST 4
#define TFT_DC 2
#define BTN_PET_FEED 26   // D26: rovid=simogatas, hosszu=toltes/etetes (v. ejszakai ebresztes)
#define BTN_GAME_MENU 27  // D27: rovid=jatek, hosszu=menu (ido/kedves szo/idojaras)

const char* WIFI_SSID = SECRET_WIFI_SSID;          // -> secrets.h
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;  // -> secrets.h

// Helyszin koordinatak (Open-Meteo-hoz) -> config.h
const float LAT = CFG_WEATHER_LAT;
const float LON = CFG_WEATHER_LON;

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;       // CET = UTC+1
const int DAYLIGHT_OFFSET_SEC = 3600;   // nyari idoszamitas +1

Adafruit_ST7735 display = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
// UJ MEGKOZELITES: a canvas dinamikus, de csak akkor letezik, amikor a MiniFace
// tenylegesen fut.
//   - Statikus (allando) 41KB-os foglalas NEM jo: elveszi a helyet az I2S DMA
//     pufferektol es a FreeRTOS-tol, a rendszer mar indulaskor elszall.
//   - Allando heap-foglalas sem jo: a Radar+Radio mellett nem fer be.
// Ezert: a MiniFace setup()-jaban foglaljuk (ekkor a radio mar leallt, van hely),
// es kilepeskor felszabaditjuk (freeCanvas()), hogy a radionak legyen helye.
GFXcanvas16* canvasPtr = nullptr;
#define canvas (*canvasPtr)

bool allocCanvas() {
  if (canvasPtr != nullptr) return true;

  // UJ: tobbszori probalkozas. A heap allapota valtozhat ket probalkozas kozott
  // (hatterben futo feladatok felszabadithatnak valamit), ezert nem adjuk fel
  // elsore. A legnagyobb EGYBEFUGGO szabad blokk merete szamit, nem az osszes
  // szabad hely - ezert irjuk ki azt is.
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("Canvas foglalas #");
    Serial.print(attempt);
    Serial.print(" - szabad heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(", legnagyobb egybefuggo blokk: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    canvasPtr = new GFXcanvas16(160, 128);
    if (canvasPtr != nullptr && canvasPtr->getBuffer() != nullptr) {
      return true;
    }
    // sikertelen - takaritsunk el a felig sikerult objektumot, es varjunk
    if (canvasPtr != nullptr) {
      delete canvasPtr;
      canvasPtr = nullptr;
    }
    delay(50);
  }
  return false;
}

void freeCanvas() {
  if (canvasPtr != nullptr) {
    Serial.print("freeCanvas ELOTT - legnagyobb blokk: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    delete canvasPtr;
    canvasPtr = nullptr;
    Serial.print("freeCanvas UTAN  - legnagyobb blokk: ");
    Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
}
#define NEON_BLUE 0x07FF
#define LEMON_YELLOW 0xFFEF
#define REACT_RED 0xF800
#define REACT_GREEN 0x07E0
#define BG_BLACK ST77XX_BLACK

const int EYE1_BASE_X = 40, EYE2_BASE_X = 90, EYE_BASE_Y = 40;
const int EYE_W = 26, EYE_H = 40; // vekonyabb alap

// ---------------- IDLE MOZGAS ----------------
float curX = 0, curY = 0, targetX = 0, targetY = 0;
unsigned long lastMoveTime = 0, waitTime = 0;
bool isMoving = false, isBlinking = false;
unsigned long lastBlinkTime = 0;
float eye1X = 0, eye1Y = 0, eye2X = 0, eye2Y = 0;
float speed1 = 0.2, speed2 = 0.2;

// ---------------- IDO / NAPI RITMUS ----------------
DayPhase dayPhase = DAY_ACTIVE;
DayPhase prevDayPhase = DAY_ACTIVE;
int currentHour = 12, currentMinute = 0;
int currentMonth = 1, currentDay = 1;
unsigned long lastClockCheck = 0;
const unsigned long CLOCK_CHECK_INTERVAL = 10000; // 10 mp-enkent olvassuk az RTC-t

// ---------------- WEATHER ----------------
float lastTemperature = NAN;
int lastWeatherCode = -1;

const char* hungarianMonths[] = {
  "Januar","Februar","Marcius","Aprilis","Majus","Junius",
  "Julius","Augusztus","Szeptember","Oktober","November","December"
};


// --- elore-deklaraciok (ezt az .ino-nal az Arduino IDE generalta) ---
String weatherCodeToHungarian(int code);
unsigned long randomHungerWait();
ShapeParams lerpShape(ShapeParams a, ShapeParams b, float t);
void drawEyeShaped(int cx, int cy, ShapeParams s, bool closed, bool isLeftEye, float extraScale = 1.0, uint16_t color = NEON_BLUE);
void drawSyncEye(int cx, int cy, SyncShape shape, uint16_t color);
void drawArcPoints(int cx, int cy, int r, float startDeg, float endDeg, uint16_t color);
void drawDreamIcon(int cx, int cy, int iconType, uint16_t color);
void drawBubbleOutline(int x, int y, int &bx, int &by);
void drawThoughtBubbleDots(int x, int y, int dotsVisible);
void drawThoughtBubbleIcon(int x, int y, int iconType);
void drawSleepZZZ(int x, int y, int count);
void drawKindMessage(const String &msg);
void updateClock();
void fetchWeatherIfDue();
void handleButton(ButtonState &btn, int pin, void (*onShort)(), void (*onLong)());
bool isEffectivelyAwake();
void startChargeHappy();
void startFeedHappy();
void resolveGame(bool playerPickedHeads);
void onPetShort();
void onPetLong();
void startCoinflipGame();
void startReactionGame();
void finishReactionGame(bool cheated, unsigned long reactionMs);
void updateReactionGame(unsigned long now);
void handleReactionPress();
void startSyncGame();
void finishSyncGame(bool matched);
void updateSyncGame(unsigned long now);
void handleSyncPress();
void startGameSelect();
void cycleGameSelect();
void confirmGameSelect();
void updateGameSelect(unsigned long now);
void onGameShort();
void onGameLong();
void triggerRandomEvent();
bool updateInfoFace(unsigned long now);
bool updateThinking(unsigned long now);
bool updateDream(unsigned long now);
bool updateJoy();
void updateSleepZZZ(unsigned long now);
bool updateWake(unsigned long now);
bool updateChargeHappy();
bool updateFeedHappy(unsigned long now);
bool updateCircleSeq(unsigned long now);
bool updateExplode(unsigned long now);
bool updateDart(unsigned long now);
bool updateSinkRepeat();
void updateHunger(unsigned long now);
void updateShapeState(unsigned long now);
void updateGame(unsigned long now);
void setup();
void loop();
// -------------------------------------------------------------------

String weatherCodeToHungarian(int code) {
  if (code < 0) return "meg nem tudom";
  if (code == 0) return "tiszta eg";
  if (code == 1 || code == 2) return "reszben naposs";
  if (code == 3) return "borult";
  if (code >= 45 && code <= 48) return "kodos";
  if (code >= 51 && code <= 67) return "esos";
  if (code >= 71 && code <= 77) return "havas";
  if (code >= 80 && code <= 82) return "zaporos";
  if (code >= 85 && code <= 86) return "havazasos";
  if (code >= 95) return "viharos";
  return "valtozekony";
}
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 3600000UL; // orankent

// ---------------- MINIFACE ALLAPOTGEP ----------------
MiniFaceState miniState = MF_NONE;
// ---------------- KOZOS ESEMENY-UTEMEZO ----------------
// EGY idozito donti el, mi tortenjen (szem-forma / MiniFace / info-arc), sullyal.
// Igy nem lehet, hogy az egyik tipus (pl. szem-forma) sokkal gyakoribb legyen
// a masiknal (pl. MiniFace) - mindenki egy kozos "sorsolason" mulik.
unsigned long lastEventCheck = 0;
unsigned long nextEventWait = 30000; // ennyi idonkent dontunk uj esemenyrol (also/felso hatar lent, setup()-ban)
const unsigned long EVENT_CHECK_MIN = 15000; // 15 mp
const unsigned long EVENT_CHECK_MAX = 35000; // 35 mp

/*
// sulyok - magasabb szam = gyakoribb esemeny. Ird at barmelyiket kedved szerint.
int eventWeights[EVT_COUNT] = {
  22, // EVT_NONE          - marad meg nyugton egy kicsit (ne legyen mindig tortenik valami)
  10, // EVT_SHAPE_CURIOUS
  10, // EVT_SHAPE_CUTE
  22, // EVT_WINK          - a leggyakoribb "esemeny"
  8,  // EVT_THINKING
  6,  // EVT_JOY
  7,  // EVT_DREAM
  4,  // EVT_CIRCLE_SEQ
  4,  // EVT_DART
  4,  // EVT_SINK_REPEAT
  1,  // EVT_EXPLODE       - a legritkabb
  6,  // EVT_INFO_TIME
  5,  // EVT_INFO_WEATHER
  6   // EVT_INFO_KIND
};

 */
// sulyok - magasabb szam = gyakoribb esemeny. Ird at barmelyiket kedved szerint.
int eventWeights[EVT_COUNT] = {
  22, // EVT_NONE          - marad meg nyugton egy kicsit (ne legyen mindig tortenik valami)
  10, // EVT_SHAPE_CURIOUS
  10, // EVT_SHAPE_CUTE
  30, // EVT_WINK          - a leggyakoribb "esemeny"
  18,  // EVT_THINKING
  15,  // EVT_JOY
  15,  // EVT_DREAM
  15,  // EVT_CIRCLE_SEQ
  15,  // EVT_DART
  15,  // EVT_SINK_REPEAT
  15,  // EVT_EXPLODE       
  22,  // EVT_INFO_TIME
  22,  // EVT_INFO_WEATHER
  22   // EVT_INFO_KIND      <-- UJ: kedves uzenet (allitsd kedved szerint)
};


const float LOOKAWAY_TARGET_X = -22, LOOKAWAY_TARGET_Y = 18;

// -- Thinking --
bool thinkInPlace = false; int thinkDotsVisible = 0;
unsigned long thinkPhaseTime = 0; int thinkLoops = 0;
const int THINK_DOT_INTERVAL = 350, THINK_MAX_LOOPS = 3;

// -- Joy (kor mozgas, gyozelemnel is ezt hasznaljuk) --
int joyPhase = -1; float joyTheta = 0;
const float JOY_RADIUS = 14, JOY_THETA_STEP = 18;
float joyDownY = 0; const float JOY_MAX_DOWN = 26;
float joySpringPos = 0, joySpringVel = 0;

// -- Wink --
int winkEyeIndex = 0; unsigned long winkStart = 0; const int WINK_DURATION = 200;

// -- Dream --
bool dreamInPlace = false; unsigned long dreamPhaseTime = 0; int dreamIcon = 0;
const int DREAM_DURATION = 1800;

// -- Sleep / Wake --
int sleepZVisible = 0; unsigned long sleepZTime = 0;
const int SLEEP_Z_INTERVAL = 500;
int wakePhase = 0; unsigned long wakeStartTime = 0;
const int WAKE_SHAKE_DURATION = 600;
const float WAKE_AMPLITUDE = 5, WAKE_FREQ = 0.025;
float eye1WakeOffX=0, eye1WakeOffY=0, eye2WakeOffX=0, eye2WakeOffY=0;
bool manualWakeActive = false;
unsigned long manualWakeUntil = 0;
const unsigned long MANUAL_WAKE_DURATION = 300000UL; // 5 perc

// -- Charge happy (2x pattogas - toltesnel es simogatasnal is) --
int chargeBouncePhase = 0; int chargeBounceCount = 0;
float chargeBounceY = 0; float chargeBounceVel = 0;
const int CHARGE_BOUNCE_TARGET = 2;

// -- Feed happy (citromsarga golyo + villogas) --
int feedPhase = 0; // 0 = golyo felfele mozgas, 1 = villogas
float feedBallY = 0;
const float FEED_BALL_SPEED = 5.0;
int feedFlashCount = 0;
unsigned long feedFlashTime = 0;
const int FEED_FLASH_INTERVAL = 150;
bool feedFlashOn = false;
const int FEED_FLASH_TOGGLES = 6; // 3x fel-le villanas

// -- Circle sequence (bal->jobb->mindketto kor) --
int circleSeqStep = 0;
float circleSeqBlend = 0; int circleSeqPhase = 0;
unsigned long circleSeqHoldStart = 0;
const int CIRCLE_SEQ_HOLD = 700;
const float CIRCLE_SEQ_SPEED = 0.06;

// -- Explode --
int explodePhase = 0;
float explodeScale = 1.0;
const float EXPLODE_MAX_SCALE = 1.9;
Particle particles[8];
unsigned long explodePhaseTime = 0;

// -- Dart (cikazas) --
unsigned long dartStartTime = 0;
unsigned long dartNextJump = 0;
float dartEye1X=0, dartEye1Y=0, dartEye2X=0, dartEye2Y=0;
const int DART_DURATION = 5000;

// -- Sink repeat (le-fel tunes, gyorsulva) --
int sinkCycle = 0; const int SINK_MAX_CYCLES = 9;
int sinkPhase = 0;
float sinkY = 0;

// -- Game lose flash --
unsigned long gameLoseStart = 0;
const int GAME_LOSE_DURATION = 6000;
unsigned long gameWinHoldStart = 0;
bool gameWinHolding = false;
const int GAME_WIN_HOLD_DURATION = 6000;

// ---------------- EHSEG ----------------
HungerState hungerState = HUNGER_IDLE;
unsigned long hungerTimerStart = 0;
unsigned long hungerWaitDuration = 0;
const unsigned long HUNGER_WINDOW = 60000;
const unsigned long HUNGER_SAD_DURATION = 2000;
float hungerShapeBlend = 0;

unsigned long randomHungerWait() {
  int opts[] = {30, 60, 120, 180}; // perc
  int pick = opts[random(0,4)];
  return (unsigned long)pick * 60000UL;
}

unsigned long hungerLastTick = 0;
unsigned long hungerElapsedMs = 0;

// ---------------- JATEK: FEJ VAGY IRAS ----------------
bool gameActive = false;
bool gameWaitingChoice = false;
unsigned long gameStartTime = 0;
const unsigned long GAME_CHOICE_TIMEOUT = 15000; // ha nem valaszolsz, veletlen dont helyetted
String gameMessage = "";
unsigned long gameMessageUntil = 0;

// ---------------- JATEK-VALASZTO (D27 rovid nyit, majd lapoz/inditrol) ----------------
enum GameType { GAME_COINFLIP, GAME_REACTION, GAME_SYNC, GAME_COUNT };
GameType selectedGameType = GAME_COINFLIP;
bool gameSelectMode = false;
unsigned long gameSelectUntil = 0;
const unsigned long GAME_SELECT_TIMEOUT = 10000; // 10 mp, utana visszalep valasztas nelkul

// ---------------- JATEK: REAKCIO TESZT ----------------
enum ReactionPhase { REACT_WAITING, REACT_ARMED };
bool reactionActive = false;
ReactionPhase reactionPhase = REACT_WAITING;
unsigned long reactionWaitUntil = 0;
unsigned long reactionArmedTime = 0;
bool reactionSide = false; // false=bal, true=jobb
const unsigned long REACT_ARMED_TIMEOUT = 3000; // ha nem nyomsz idoben, elveszted

// ---------------- JATEK: SZEM-SZINKRON ----------------
bool syncActive = false;
SyncShape syncShapeLeft = SYNC_CURIOUS;
SyncShape syncShapeRight = SYNC_CIRCLE;
unsigned long syncLeftNextChange = 0;
unsigned long syncRightNextChange = 0;
unsigned long syncGameStart = 0;
const unsigned long SYNC_MIN_INTERVAL = 600;
const unsigned long SYNC_MAX_INTERVAL = 1400;
const unsigned long SYNC_GAME_TIMEOUT = 20000;
unsigned long syncLastPetPress = 0;
unsigned long syncLastGamePress = 0;
const unsigned long SYNC_SIMULTANEOUS_WINDOW = 200;

// ---------------- MENU (D27 hosszu) ----------------
int menuIndex = 0; // 0=ido, 1=kedves szo, 2=idojaras
const char* kindWords[] = {
  "Jo latni teged!",
  "Szep napod legyen!",
  "Orulok hogy itt vagy!",
  "Te vagy a kedvenc emberem!",
  "Ma is ragyogsz!",
  "Buszke vagyok rad!",
  "Csak semmi stressz, minden rendben lesz!",
  "Egy mosoly mindig segit!",
  "Koszonom hogy velem vagy!",
  "Klassz napod legyen!",
  "Te tudod a legjobban csinalni!",
  "Sose add fel!",
  "Ma is jol csinalod!",
  "Egy jo szo sokat szamit!",
  "Erted vagyok!"
};
const int KIND_WORDS_COUNT = 15;
String menuMessage = "";
unsigned long menuMessageUntil = 0;

// ---------------- KEDVES UZENET INFO-ARC (UJ) ----------------
String infoKindMessage = "";

// ---------------- GOMB KEZELES ----------------
ButtonState btnPet, btnGame;
const unsigned long LONG_PRESS_MS = 800;

// ---------------- SZEM-FORMA RENDSZER ----------------
ShapeParams SHAPE_BASE   = {26, 40, 10, 0, 0, 0};
ShapeParams SHAPE_DEFS[] = {
  {26, 40, 10, 0, 0, 0},   // NORMAL (placeholder, nem hasznalt kozvetlenul)
  {26, 40, 10, 1, 0, 0},   // WORRIED
  {26, 40, 10, 0, 1, 0},   // ANGRY
  {20, 46, 9,  0, 0, 0},   // CURIOUS
  {26, 34, 13, 0, 0, 1},   // CUTE
  {32, 20, 8,  0, 0, 0},   // FOCUSED (lapos)
  {30, 30, 15, 0, 0, 0}    // CIRCLE (ehseg)
};

// idle random pool - CSAK ez a harom valaszthato nezelodeskor
EyeShapeType idlePool[] = { SHAPE_NORMAL, SHAPE_CURIOUS, SHAPE_CUTE };

EyeShapeType activeShapeType = SHAPE_NORMAL;
int shapePhase = -1; // -1=inaktiv, 0=be, 1=tart, 2=ki
float shapeBlend = 0;
unsigned long shapeHoldStart = 0;
const float SHAPE_BLEND_SPEED = 0.015;
const int SHAPE_HOLD_DURATION = 3000;

// formankenti blend-sebesseg es tartas-ido (index = EyeShapeType)
float SHAPE_BLEND_SPEEDS[] = {
  0.015, // SHAPE_NORMAL
  0.015, // SHAPE_WORRIED
  0.015, // SHAPE_ANGRY
  0.05,  // SHAPE_CURIOUS - gyorsabb atalakulas
  0.05,  // SHAPE_CUTE - gyorsabb atalakulas
  0.015, // SHAPE_FOCUSED
  0.015  // SHAPE_CIRCLE
};
int SHAPE_HOLD_DURATIONS[] = {
  3000,  // SHAPE_NORMAL
  3000,  // SHAPE_WORRIED
  3000,  // SHAPE_ANGRY
  300000, // SHAPE_CURIOUS - hosszabb kitartas
  25000, // SHAPE_CUTE - hosszabb kitartas
  3000,  // SHAPE_FOCUSED
  3000   // SHAPE_CIRCLE
};

bool forceFocusedShape = false; // 20:00-08:00 kozott (windown + waking)

ShapeParams lerpShape(ShapeParams a, ShapeParams b, float t) {
  ShapeParams r;
  r.w = a.w + (b.w - a.w) * t;
  r.h = a.h + (b.h - a.h) * t;
  r.cornerRadius = a.cornerRadius + (b.cornerRadius - a.cornerRadius) * t;
  r.outerTopCut = a.outerTopCut + (b.outerTopCut - a.outerTopCut) * t;
  r.innerTopCut = a.innerTopCut + (b.innerTopCut - a.innerTopCut) * t;
  r.bottomCurveIn = a.bottomCurveIn + (b.bottomCurveIn - a.bottomCurveIn) * t;
  return r;
}

// ================= RAJZOLO FUGGVENYEK =================
void drawEyeShaped(int cx, int cy, ShapeParams s, bool closed, bool isLeftEye, float extraScale, uint16_t color) {
  if (closed) {
    int halfW = (int)(s.w * extraScale / 2);
    canvas.drawLine(cx - halfW, cy, cx + halfW, cy, color);
    return;
  }
  float w = s.w * extraScale, h = s.h * extraScale;
  int x = cx - w/2, y = cy - h/2;
  int wi = (int)w, hi = (int)h;
  if (wi < 1 || hi < 1) return; // teljesen eltunt (robbanas kozepe)

  canvas.fillRoundRect(x, y, wi, hi, (int)(s.cornerRadius * extraScale), color);

  int outerCut = (int)(s.outerTopCut * 14 * extraScale);
  int innerCut = (int)(s.innerTopCut * 14 * extraScale);
  if (outerCut > 1) {
    if (isLeftEye) canvas.fillTriangle(x, y, x+outerCut, y, x, y+outerCut, BG_BLACK);
    else canvas.fillTriangle(x+wi, y, x+wi-outerCut, y, x+wi, y+outerCut, BG_BLACK);
  }
  if (innerCut > 1) {
    if (isLeftEye) canvas.fillTriangle(x+wi, y, x+wi-innerCut, y, x+wi, y+innerCut, BG_BLACK);
    else canvas.fillTriangle(x, y, x+innerCut, y, x, y+innerCut, BG_BLACK);
  }
  if (s.bottomCurveIn > 0.02) {
    float maxCarveR = w * 0.95;
    float carveR = maxCarveR * s.bottomCurveIn;
    int carveCy = y + hi + (int)(carveR * 0.35);
    canvas.fillCircle(cx, carveCy, (int)carveR, BG_BLACK);
  }
}

void drawSyncEye(int cx, int cy, SyncShape shape, uint16_t color) {
  switch (shape) {
    case SYNC_CURIOUS: drawEyeShaped(cx, cy, SHAPE_DEFS[SHAPE_CURIOUS], false, true, 1.0, color); break;
    case SYNC_FLAT:     drawEyeShaped(cx, cy, SHAPE_DEFS[SHAPE_FOCUSED], false, true, 1.0, color); break;
    case SYNC_CIRCLE:   drawEyeShaped(cx, cy, SHAPE_DEFS[SHAPE_CIRCLE], false, true, 1.0, color); break;
    case SYNC_DIAMOND: {
      int hw = 15, hh = 20;
      canvas.fillTriangle(cx, cy - hh, cx - hw, cy, cx + hw, cy, color);
      canvas.fillTriangle(cx, cy + hh, cx - hw, cy, cx + hw, cy, color);
      break;
    }
    case SYNC_TRIANGLE: {
      int hw = 15, hh = 20;
      canvas.fillTriangle(cx, cy - hh, cx - hw, cy + hh, cx + hw, cy + hh, color);
      break;
    }
  }
}

void drawArcPoints(int cx, int cy, int r, float startDeg, float endDeg, uint16_t color) {
  for (float a = startDeg; a <= endDeg; a += 12) {
    float rad = radians(a);
    canvas.drawPixel(cx + cos(rad)*r, cy + sin(rad)*r, color);
    canvas.drawPixel(cx + cos(rad)*(r-1), cy + sin(rad)*(r-1), color);
  }
}

void drawDreamIcon(int cx, int cy, int iconType, uint16_t color) {
  switch (iconType) {
    case 0: // hopehely
      canvas.drawLine(cx-7,cy,cx+7,cy,color);
      canvas.drawLine(cx,cy-7,cx,cy+7,color);
      canvas.drawLine(cx-5,cy-5,cx+5,cy+5,color);
      canvas.drawLine(cx-5,cy+5,cx+5,cy-5,color);
      break;
    case 1: // hangjegy
      canvas.fillCircle(cx-3,cy+5,3,color);
      canvas.drawLine(cx,cy+5,cx,cy-7,color);
      canvas.drawLine(cx,cy-7,cx+5,cy-5,color);
      break;
    case 2: // sziv
      canvas.fillCircle(cx-3,cy-2,4,color);
      canvas.fillCircle(cx+3,cy-2,4,color);
      canvas.fillTriangle(cx-7,cy,cx+7,cy,cx,cy+8,color);
      break;
    case 3: // ora
      canvas.drawCircle(cx,cy,8,color);
      canvas.drawLine(cx,cy,cx,cy-5,color);
      canvas.drawLine(cx,cy,cx+4,cy,color);
      break;
    case 4: // mosoly
      canvas.drawCircle(cx,cy,8,color);
      canvas.fillCircle(cx-3,cy-2,1,color);
      canvas.fillCircle(cx+3,cy-2,1,color);
      drawArcPoints(cx,cy,4,20,160,color);
      break;
    case 5: // integetes
      canvas.fillRoundRect(cx-3,cy-2,6,8,2,color);
      canvas.drawLine(cx-3,cy-2,cx-5,cy-7,color);
      canvas.drawLine(cx,cy-2,cx,cy-8,color);
      canvas.drawLine(cx+3,cy-2,cx+5,cy-7,color);
      break;
    case 6: // szeles mosoly
      canvas.drawCircle(cx,cy,8,color);
      canvas.fillCircle(cx-3,cy-2,1,color);
      canvas.fillCircle(cx+3,cy-2,1,color);
      drawArcPoints(cx,cy-1,5,15,165,color);
      break;
  }
}

void drawBubbleOutline(int x, int y, int &bx, int &by) {
  canvas.fillCircle(x, y, 2, NEON_BLUE);
  canvas.fillCircle(x+7, y-7, 3, NEON_BLUE);
  canvas.fillCircle(x+15, y-15, 4, NEON_BLUE);
  bx = x+16; by = y-36;
  canvas.drawRoundRect(bx, by, 44, 22, 10, NEON_BLUE);
}
void drawThoughtBubbleDots(int x, int y, int dotsVisible) {
  int bx, by; drawBubbleOutline(x, y, bx, by);
  for (int i=0;i<3;i++) if (i<dotsVisible) canvas.fillCircle(bx+12+i*11, by+11, 3, NEON_BLUE);
}
void drawThoughtBubbleIcon(int x, int y, int iconType) {
  int bx, by; drawBubbleOutline(x, y, bx, by);
  drawDreamIcon(bx+22, by+11, iconType, NEON_BLUE);
}
void drawSleepZZZ(int x, int y, int count) {
  canvas.setTextColor(NEON_BLUE);
  if (count>=1){canvas.setTextSize(1); canvas.setCursor(x,y); canvas.print("Z");}
  if (count>=2){canvas.setTextSize(2); canvas.setCursor(x+8,y-12); canvas.print("Z");}
  if (count>=3){canvas.setTextSize(3); canvas.setCursor(x+18,y-28); canvas.print("Z");}
}

// ================= IDO / NAPI RITMUS =================
// ---- UJ: kedves uzenet kirajzolasa, automatikus sortoressel, kozepre ----
void drawKindMessage(const String &msg) {
  const int maxChars = 25;          // 160 px / 6 px per karakter
  String lines[3];
  int lineCount = 0;
  String cur = "";
  int i = 0;

  while (i <= (int)msg.length() && lineCount < 3) {
    int sp = msg.indexOf(' ', i);
    String word = (sp < 0) ? msg.substring(i) : msg.substring(i, sp);
    String test = (cur.length() == 0) ? word : cur + " " + word;
    if ((int)test.length() > maxChars && cur.length() > 0) {
      lines[lineCount++] = cur;
      cur = word;
    } else {
      cur = test;
    }
    if (sp < 0) break;
    i = sp + 1;
  }
  if (lineCount < 3 && cur.length() > 0) lines[lineCount++] = cur;

  canvas.setTextSize(1);
  canvas.setTextColor(LEMON_YELLOW);
  int y = 64 - (lineCount * 12) / 2;
  for (int k = 0; k < lineCount; k++) {
    int w = lines[k].length() * 6;
    canvas.setCursor(80 - w / 2, y + k * 12);
    canvas.print(lines[k]);
  }
}

void updateClock() {
  unsigned long now = millis();
  if (now - lastClockCheck < CLOCK_CHECK_INTERVAL) return;
  lastClockCheck = now;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 200)) {
    currentHour = timeinfo.tm_hour;
    currentMinute = timeinfo.tm_min;
    currentMonth = timeinfo.tm_mon + 1;
    currentDay = timeinfo.tm_mday;
  }

  prevDayPhase = dayPhase;
  int mins = currentHour*60 + currentMinute;
  if (mins >= 23*60 || mins < 7*60+30) dayPhase = DAY_SLEEP;
  else if (mins < 8*60) dayPhase = DAY_WAKING;
  else if (mins < 22*60+30) dayPhase = DAY_ACTIVE;
  else dayPhase = DAY_WINDDOWN;

  if (dayPhase == DAY_SLEEP && prevDayPhase != DAY_SLEEP && miniState != MF_SLEEP) {
    miniState = MF_SLEEP;
    sleepZVisible = 0; sleepZTime = now;
    manualWakeActive = false;
    eye1X = eye1Y = eye2X = eye2Y = 0;
    isMoving = false;
    if (hungerState != HUNGER_IDLE) {
      hungerState = HUNGER_IDLE; hungerElapsedMs = 0; hungerWaitDuration = randomHungerWait();
    }
  }
  if (dayPhase == DAY_WAKING && prevDayPhase == DAY_SLEEP) {
    miniState = MF_WAKE; wakePhase = 0; wakeStartTime = now;
    eye1WakeOffX=eye1WakeOffY=eye2WakeOffX=eye2WakeOffY=0;
  }
  forceFocusedShape = (dayPhase == DAY_WINDDOWN || dayPhase == DAY_WAKING);
}

// ================= WEATHER =================
void fetchWeatherIfDue() {
  unsigned long now = millis();
  if (lastWeatherFetch != 0 && now - lastWeatherFetch < WEATHER_INTERVAL) return;
  if (WiFi.status() != WL_CONNECTED) return;
  lastWeatherFetch = now;

  HTTPClient http;
  // UJ: http:// (nem https://) - igy nem kell TLS-kapcsolatot epiteni, ami sok
  // RAM-ot enne. A canvas 41KB-ja mellett a TLS gyakran elhasalt, ezert nem jott
  // az idojaras. Az Open-Meteo titkositas nelkul is kiszolgalja a kerest -
  // ugyanigy hivja az Idojaras modul is, ott sosem volt gond vele.
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(LAT,2) +
               "&longitude=" + String(LON,2) + "&current_weather=true";
  http.begin(url);
  int code = http.GET();
  Serial.print("MiniFace idojaras HTTP kod: ");
  Serial.println(code);

  bool success = false;
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      lastTemperature = doc["current_weather"]["temperature"] | NAN;
      lastWeatherCode = doc["current_weather"]["weathercode"] | -1;
      success = true;
      Serial.print("MiniFace idojaras OK: ");
      Serial.print(lastTemperature);
      Serial.println(" C");
    } else {
      Serial.println("MiniFace idojaras: JSON parse hiba");
    }
  }
  http.end();

  // UJ: ha nem sikerult, ne varjunk a teljes intervallumot az ujraprobalkozassal.
  // Korabban a lastWeatherFetch a hivas ELOTT allt be, igy egy sikertelen keres
  // utan is ugy viselkedett a kod, mintha sikerult volna -> sokaig nem volt adat.
  if (!success) {
    lastWeatherFetch = now - WEATHER_INTERVAL + 15000; // 15 mp mulva ujra
  }
}

// ================= GOMB AKCIOK (elore deklaralva) =================
void onPetShort();
void onPetLong();
void onGameShort();
void onGameLong();

void handleButton(ButtonState &btn, int pin, void (*onShort)(), void (*onLong)()) {
  bool isDown = (digitalRead(pin) == LOW);
  unsigned long now = millis();
  if (isDown && !btn.pressed) {
    btn.pressed = true; btn.pressStart = now; btn.longFired = false;
  } else if (isDown && btn.pressed) {
    if (!btn.longFired && now - btn.pressStart > LONG_PRESS_MS) {
      btn.longFired = true;
      onLong();
    }
  } else if (!isDown && btn.pressed) {
    btn.pressed = false;
    if (!btn.longFired) onShort();
  }
}

bool isEffectivelyAwake() {
  return (dayPhase != DAY_SLEEP) || manualWakeActive;
}

void startChargeHappy() {
  miniState = MF_CHARGE_HAPPY;
  chargeBouncePhase = 0; chargeBounceCount = 0; chargeBounceY = 0; chargeBounceVel = 0;
}

void startFeedHappy() {
  miniState = MF_FEED_HAPPY;
  feedPhase = 0;
  feedBallY = 70;
  feedFlashCount = 0;
  feedFlashOn = true;
}

void resolveGame(bool playerPickedHeads) {
  gameWaitingChoice = false;
  gameActive = false;
  shapePhase = 2; // gyors visszaallas a curious formabol

  bool actualHeads = random(0,2) == 0;
  bool playerWins = (playerPickedHeads == actualHeads);
  gameMessage = actualHeads ? "Fej!" : "Iras!";
  gameMessage += playerWins ? " Te nyertel!" : " En nyertem!";
  gameMessageUntil = millis() + 3000;

  if (playerWins) {
    miniState = MF_GAME_LOSE; // a jatekos nyert -> a MiniFace veszitett -> merges
    gameLoseStart = millis();
  } else {
    miniState = MF_GAME_WIN; // a jatekos veszitett -> a MiniFace nyert -> orom
    joyPhase=-1; joyTheta=0; joyDownY=0; joySpringPos=0; joySpringVel=0;
    gameWinHolding = false;
  }
}

void onPetShort() {
  if (gameSelectMode) { confirmGameSelect(); return; }
  if (gameWaitingChoice) { resolveGame(true); return; } // D26 = Fej
  if (reactionActive) { handleReactionPress(); return; }
  if (syncActive) { syncLastPetPress = millis(); handleSyncPress(); return; }
  bool blocked = (hungerState != HUNGER_IDLE) || (miniState == MF_SLEEP) || !isEffectivelyAwake();
  if (blocked) return;
  startChargeHappy(); // simogatas -> aranyos pattogo reakcio
}

void onPetLong() {
  if (hungerState == HUNGER_CIRCLE) {
    // toltes/etetes sikeres
    hungerState = HUNGER_IDLE;
    hungerTimerStart = millis();
    hungerElapsedMs = 0;
    hungerWaitDuration = randomHungerWait();
    startFeedHappy();
    return;
  }
  if (dayPhase == DAY_SLEEP && !manualWakeActive) {
    // meg alszik -> ez a nyomas ebreszti fel
    if (miniState == MF_SLEEP) {
      miniState = MF_WAKE; wakePhase = 0; wakeStartTime = millis();
      manualWakeActive = true;
      manualWakeUntil = millis() + MANUAL_WAKE_DURATION;
    }
    return;
  }
  if (manualWakeActive) {
    // mar ebren van ejszaka -> csak hosszabbitsuk meg az ebrenletet
    manualWakeUntil = millis() + MANUAL_WAKE_DURATION;
  }
}

void startCoinflipGame() {
  gameActive = true;
  gameWaitingChoice = true;
  gameStartTime = millis();
  gameMessage = "Fej vagy Iras?";
  gameMessageUntil = millis() + GAME_CHOICE_TIMEOUT;
  activeShapeType = SHAPE_CURIOUS; // "varakozo" jelzes
  shapePhase = 0; shapeBlend = 0;
}

// ---------------- REAKCIO TESZT ----------------
void startReactionGame() {
  gameActive = true;
  reactionActive = true;
  reactionPhase = REACT_WAITING;
  reactionWaitUntil = millis() + random(2000, 6000);
  isMoving = false;
  eye1X = eye1Y = eye2X = eye2Y = 0;
  activeShapeType = SHAPE_CURIOUS; shapePhase = 0; shapeBlend = 0;
  gameMessage = "Figyelj...";
  gameMessageUntil = millis() + 60000;
}

void finishReactionGame(bool cheated, unsigned long reactionMs) {
  reactionActive = false;
  gameActive = false;
  shapePhase = 2;
  eye1X = eye1Y = eye2X = eye2Y = 0;
  if (cheated) {
    gameMessage = "Csalas! Korai nyomas.";
    miniState = MF_GAME_LOSE; gameLoseStart = millis();
  } else {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f mp!", reactionMs / 1000.0);
    gameMessage = String(buf);
    miniState = MF_GAME_WIN;
    joyPhase=-1; joyTheta=0; joyDownY=0; joySpringPos=0; joySpringVel=0; gameWinHolding=false;
  }
  gameMessageUntil = millis() + 4000;
}

void updateReactionGame(unsigned long now) {
  if (!reactionActive) return;
  if (reactionPhase == REACT_WAITING) {
    if (now >= reactionWaitUntil) {
      reactionPhase = REACT_ARMED;
      reactionArmedTime = now;
      reactionSide = random(0,2) == 0; // false=bal, true=jobb
      eye1X = eye2X = reactionSide ? 22 : -22; // hirtelen "villanas" oldalra
      eye1Y = eye2Y = 0;
      gameMessage = "Nyomj most!";
      gameMessageUntil = now + REACT_ARMED_TIMEOUT;
    }
  } else if (reactionPhase == REACT_ARMED) {
    if (now - reactionArmedTime > REACT_ARMED_TIMEOUT) {
      gameMessage = "Tul lassu voltal!";
      finishReactionGame(true, 0); // idokifutas -> vesztes reakcio
    }
  }
}

void handleReactionPress() {
  unsigned long now = millis();
  if (reactionPhase == REACT_WAITING) {
    finishReactionGame(true, 0); // korai nyomas -> csalas
  } else if (reactionPhase == REACT_ARMED) {
    finishReactionGame(false, now - reactionArmedTime);
  }
}

// ---------------- SZEM-SZINKRON ----------------
void startSyncGame() {
  gameActive = true;
  syncActive = true;
  syncGameStart = millis();
  isMoving = false;
  eye1X = eye1Y = eye2X = eye2Y = 0;
  syncShapeLeft = (SyncShape)random(0, SYNC_SHAPE_COUNT);
  syncShapeRight = (SyncShape)random(0, SYNC_SHAPE_COUNT);
  syncLeftNextChange = millis() + random(SYNC_MIN_INTERVAL, SYNC_MAX_INTERVAL);
  syncRightNextChange = millis() + random(SYNC_MIN_INTERVAL, SYNC_MAX_INTERVAL);
  syncLastPetPress = 0; syncLastGamePress = 0;
  gameMessage = "Nyomd egyszerre, ha egyeznek!";
  gameMessageUntil = millis() + 60000;
}

void finishSyncGame(bool matched) {
  syncActive = false;
  gameActive = false;
  if (matched) {
    gameMessage = "Talalat! Egyeznek!";
    miniState = MF_GAME_WIN;
    joyPhase=-1; joyTheta=0; joyDownY=0; joySpringPos=0; joySpringVel=0; gameWinHolding=false;
  } else {
    gameMessage = "Nem egyeztek!";
    miniState = MF_GAME_LOSE; gameLoseStart = millis();
  }
  gameMessageUntil = millis() + 4000;
}

void updateSyncGame(unsigned long now) {
  if (!syncActive) return;
  if (now >= syncLeftNextChange) {
    syncShapeLeft = (SyncShape)random(0, SYNC_SHAPE_COUNT);
    syncLeftNextChange = now + random(SYNC_MIN_INTERVAL, SYNC_MAX_INTERVAL);
  }
  if (now >= syncRightNextChange) {
    syncShapeRight = (SyncShape)random(0, SYNC_SHAPE_COUNT);
    syncRightNextChange = now + random(SYNC_MIN_INTERVAL, SYNC_MAX_INTERVAL);
  }
  if (now - syncGameStart > SYNC_GAME_TIMEOUT) {
    gameMessage = "Lejart az ido!";
    finishSyncGame(false);
  }
}

void handleSyncPress() {
  finishSyncGame(syncShapeLeft == syncShapeRight);
}

// ---------------- JATEK-VALASZTO ----------------
void startGameSelect() {
  bool blocked = (hungerState != HUNGER_IDLE) || !isEffectivelyAwake() || gameActive || miniState != MF_NONE;
  if (blocked) return;
  gameActive = true;
  gameSelectMode = true;
  selectedGameType = GAME_COINFLIP;
  gameSelectUntil = millis() + GAME_SELECT_TIMEOUT;
  gameMessage = "Jatek: Fej vagy Iras";
  gameMessageUntil = gameSelectUntil;
  activeShapeType = SHAPE_CURIOUS; shapePhase = 0; shapeBlend = 0;
}

void cycleGameSelect() {
  selectedGameType = (GameType)((selectedGameType + 1) % GAME_COUNT);
  gameSelectUntil = millis() + GAME_SELECT_TIMEOUT;
  const char* names[] = {"Fej vagy Iras", "Reakcio teszt", "Szem-szinkron"};
  gameMessage = String("Jatek: ") + names[selectedGameType];
  gameMessageUntil = gameSelectUntil;
}

void confirmGameSelect() {
  gameSelectMode = false;
  bool stillOk = (hungerState == HUNGER_IDLE) && isEffectivelyAwake();
  if (!stillOk) { gameActive = false; shapePhase = 2; return; }
  if (selectedGameType == GAME_COINFLIP) startCoinflipGame();
  else if (selectedGameType == GAME_REACTION) startReactionGame();
  else startSyncGame();
}

void updateGameSelect(unsigned long now) {
  if (!gameSelectMode) return;
  if (now > gameSelectUntil) {
    gameSelectMode = false;
    gameActive = false;
    shapePhase = 2;
  }
}

void onGameShort() {
  if (gameSelectMode) { cycleGameSelect(); return; }
  if (gameWaitingChoice) { resolveGame(false); return; } // D27 = Iras
  if (reactionActive) { handleReactionPress(); return; }
  if (syncActive) { syncLastGamePress = millis(); handleSyncPress(); return; }
  startGameSelect();
}

void onGameLong() {
  if (dayPhase == DAY_SLEEP && !manualWakeActive) {
    // meg alszik -> ez a nyomas ebreszti fel, meg nem a menu fut le
    if (miniState == MF_SLEEP) {
      miniState = MF_WAKE; wakePhase = 0; wakeStartTime = millis();
      manualWakeActive = true;
      manualWakeUntil = millis() + MANUAL_WAKE_DURATION;
    }
    return;
  }
  if (manualWakeActive) {
    // mar ebren van ejszaka -> a menu is mukodjon, es hosszabbodjon meg az ebrenlet
    manualWakeUntil = millis() + MANUAL_WAKE_DURATION;
  }
  menuIndex = (menuIndex + 1) % 3;
  char buf[48];
  if (menuIndex == 0) {
    snprintf(buf, sizeof(buf), "Most %02d:%02d van.", currentHour, currentMinute);
    menuMessage = String(buf);
  } else if (menuIndex == 1) {
    menuMessage = kindWords[random(0, KIND_WORDS_COUNT)];
  } else {
    if (isnan(lastTemperature)) menuMessage = "Meg nem tudom az idojarast.";
    else { snprintf(buf, sizeof(buf), "Kint %.1f fok van.", lastTemperature); menuMessage = String(buf); }
  }
  menuMessageUntil = millis() + 4000;
}

// ================= MINIFACE INDITAS =================
// info-arc allapotai (idokijelzo es idojaras-kijelzo kozosen hasznalja)
int infoPhase = 0; // 0 = szemek eltunese, 1 = szoveg latszik, 2 = szemek visszanonek
float infoEyeScale = 1.0;
unsigned long infoPhaseTime = 0;
const int INFO_HOLD_DURATION = 4000;
const float INFO_FADE_SPEED = 0.08;

void triggerRandomEvent() {
  int totalWeight = 0;
  for (int i=0;i<EVT_COUNT;i++) totalWeight += eventWeights[i];
  int r = random(0, totalWeight);
  int cum = 0;
  int chosen = EVT_NONE;
  for (int i=0;i<EVT_COUNT;i++) {
    cum += eventWeights[i];
    if (r < cum) { chosen = i; break; }
  }

  switch (chosen) {
    case EVT_NONE:
      break; // nem tortenik semmi ebben a korben - tovabb nezelodik
    case EVT_SHAPE_CURIOUS:
      activeShapeType = SHAPE_CURIOUS; shapePhase = 0; shapeBlend = 0;
      break;
    case EVT_SHAPE_CUTE:
      activeShapeType = SHAPE_CUTE; shapePhase = 0; shapeBlend = 0;
      break;
    case EVT_WINK:
      miniState = MF_WINK; winkEyeIndex = random(0,2); winkStart = millis();
      break;
    case EVT_THINKING:
      miniState = MF_THINKING;
      thinkInPlace=false; thinkDotsVisible=0; thinkLoops=0;
      isMoving=true; targetX=LOOKAWAY_TARGET_X; targetY=LOOKAWAY_TARGET_Y;
      break;
    case EVT_JOY:
      miniState = MF_JOY;
      joyPhase=-1; joyTheta=0; joyDownY=0; joySpringPos=0; joySpringVel=0; isMoving=false;
      break;
    case EVT_DREAM:
      miniState = MF_DREAM;
      dreamInPlace=false; isMoving=true; targetX=LOOKAWAY_TARGET_X; targetY=LOOKAWAY_TARGET_Y;
      break;
    case EVT_CIRCLE_SEQ:
      miniState = MF_CIRCLE_SEQ;
      circleSeqStep = 0; circleSeqPhase = 0; circleSeqBlend = 0;
      break;
    case EVT_DART:
      miniState = MF_DART;
      dartStartTime = millis(); dartNextJump = millis();
      break;
    case EVT_SINK_REPEAT:
      miniState = MF_SINK_REPEAT;
      sinkCycle = 0; sinkPhase = 0; sinkY = 0;
      break;
    case EVT_EXPLODE:
      miniState = MF_EXPLODE;
      explodePhase = 0; explodeScale = 1.0; explodePhaseTime = millis();
      for (int i=0;i<8;i++) particles[i].active=false;
      break;
    case EVT_INFO_TIME:
      miniState = MF_INFO_TIME; infoPhase = 0; infoEyeScale = 1.0; isMoving = false;
      break;
    case EVT_INFO_WEATHER:
      miniState = MF_INFO_WEATHER; infoPhase = 0; infoEyeScale = 1.0; isMoving = false;
      break;
    case EVT_INFO_KIND:   // <-- UJ: random kedves uzenet
      infoKindMessage = kindWords[random(0, KIND_WORDS_COUNT)];
      miniState = MF_INFO_KIND; infoPhase = 0; infoEyeScale = 1.0; isMoving = false;
      break;
  }
}



bool updateInfoFace(unsigned long now) {
  if (infoPhase == 0) {
    infoEyeScale -= INFO_FADE_SPEED;
    if (infoEyeScale <= 0) { infoEyeScale = 0; infoPhase = 1; infoPhaseTime = now; }
    return false;
  } else if (infoPhase == 1) {
    if (now - infoPhaseTime > INFO_HOLD_DURATION) infoPhase = 2;
    return false;
  } else {
    infoEyeScale += INFO_FADE_SPEED;
    if (infoEyeScale >= 1) { infoEyeScale = 1; return true; }
    return false;
  }
}

// ================= MINIFACE FRISSITESEK =================
bool updateThinking(unsigned long now) {
  if (!thinkInPlace) {
    if (abs(targetX-eye1X)<1 && abs(targetY-eye1Y)<1 && abs(targetX-eye2X)<1 && abs(targetY-eye2Y)<1) {
      thinkInPlace=true; thinkPhaseTime=now; thinkDotsVisible=0;
    }
    return false;
  }
  if (now - thinkPhaseTime > THINK_DOT_INTERVAL) {
    thinkPhaseTime = now; thinkDotsVisible++;
    if (thinkDotsVisible > 3) { thinkDotsVisible = 0; thinkLoops++; }
  }
  return thinkLoops >= THINK_MAX_LOOPS;
}
bool updateDream(unsigned long now) {
  if (!dreamInPlace) {
    if (abs(targetX-eye1X)<1 && abs(targetY-eye1Y)<1 && abs(targetX-eye2X)<1 && abs(targetY-eye2Y)<1) {
      dreamInPlace=true; dreamPhaseTime=now; dreamIcon=random(0,7);
    }
    return false;
  }
  return (now - dreamPhaseTime) > DREAM_DURATION;
}
bool updateJoy() {
  if (joyPhase == -1) {
    eye1X += (0-eye1X)*0.25; eye1Y += (0-eye1Y)*0.25; eye2X += (0-eye2X)*0.25; eye2Y += (0-eye2Y)*0.25;
    if (abs(eye1X)<0.5 && abs(eye1Y)<0.5 && abs(eye2X)<0.5 && abs(eye2Y)<0.5) { eye1X=eye1Y=eye2X=eye2Y=0; joyPhase=0; }
    return false;
  } else if (joyPhase == 0) {
    joyTheta += JOY_THETA_STEP;
    if (joyTheta >= 360) { joyTheta=0; joyPhase=1; }
    return false;
  } else if (joyPhase == 1) {
    joyDownY += 4;
    if (joyDownY >= JOY_MAX_DOWN) { joyDownY=JOY_MAX_DOWN; joyPhase=2; joySpringPos=JOY_MAX_DOWN; joySpringVel=0; }
    return false;
  } else {
    float k=0.25, damping=0.72;
    float accel = (0-joySpringPos)*k;
    joySpringVel = (joySpringVel+accel)*damping;
    joySpringPos += joySpringVel;
    if (abs(joySpringPos)<0.5 && abs(joySpringVel)<0.3) { joySpringPos=0; return true; }
    return false;
  }
}
bool updateWink(unsigned long now) { return (now - winkStart) > WINK_DURATION; }

void updateSleepZZZ(unsigned long now) {
  if (now - sleepZTime > SLEEP_Z_INTERVAL) {
    sleepZTime = now;
    sleepZVisible++;
    if (sleepZVisible > 4) sleepZVisible = 0; // 3 utan egy uresebb korrel ujraindul
  }
}

bool updateWake(unsigned long now) {
  if (wakePhase == 0) {
    unsigned long elapsed = now - wakeStartTime;
    if (elapsed >= WAKE_SHAKE_DURATION) { wakePhase = 1; return false; }
    float decay = 1.0 - (float)elapsed / WAKE_SHAKE_DURATION;
    float osc = sin(elapsed * WAKE_FREQ) * WAKE_AMPLITUDE * decay;
    eye2WakeOffX=osc; eye2WakeOffY=-osc; eye1WakeOffX=-osc; eye1WakeOffY=-osc;
    return false;
  } else {
    eye1WakeOffX*=0.7; eye1WakeOffY*=0.7; eye2WakeOffX*=0.7; eye2WakeOffY*=0.7;
    if (abs(eye1WakeOffX)<0.3 && abs(eye1WakeOffY)<0.3 && abs(eye2WakeOffX)<0.3 && abs(eye2WakeOffY)<0.3) {
      eye1WakeOffX=eye1WakeOffY=eye2WakeOffX=eye2WakeOffY=0; return true;
    }
    return false;
  }
}

bool updateChargeHappy() {
  float k=0.35, damping=0.65;
  if (chargeBouncePhase == 0) { chargeBounceVel = -6; chargeBouncePhase = 1; }
  float accel = (0-chargeBounceY)*k;
  chargeBounceVel = (chargeBounceVel+accel)*damping;
  chargeBounceY += chargeBounceVel;
  if (chargeBouncePhase==1 && chargeBounceY > -0.5 && chargeBounceVel > -0.3 && chargeBounceVel < 0.3) {
    chargeBounceCount++;
    if (chargeBounceCount >= CHARGE_BOUNCE_TARGET) return true;
    chargeBounceVel = -6;
  }
  return false;
}

bool updateFeedHappy(unsigned long now) {
  if (feedPhase == 0) {
    feedBallY -= FEED_BALL_SPEED;
    if (feedBallY <= 0) {
      feedBallY = 0;
      feedPhase = 1;
      feedFlashCount = 0;
      feedFlashTime = now;
      feedFlashOn = true;
    }
    return false;
  } else {
    if (now - feedFlashTime > FEED_FLASH_INTERVAL) {
      feedFlashTime = now;
      feedFlashOn = !feedFlashOn;
      feedFlashCount++;
      if (feedFlashCount >= FEED_FLASH_TOGGLES) return true;
    }
    return false;
  }
}

bool updateCircleSeq(unsigned long now) {
  if (circleSeqPhase == 0) {
    circleSeqBlend += CIRCLE_SEQ_SPEED;
    if (circleSeqBlend >= 1) { circleSeqBlend=1; circleSeqPhase=1; circleSeqHoldStart=now; }
  } else if (circleSeqPhase == 1) {
    if (now - circleSeqHoldStart > CIRCLE_SEQ_HOLD) circleSeqPhase = 2;
  } else {
    circleSeqBlend -= CIRCLE_SEQ_SPEED;
    if (circleSeqBlend <= 0) {
      circleSeqBlend = 0;
      circleSeqStep++;
      if (circleSeqStep > 2) return true;
      circleSeqPhase = 0;
    }
  }
  return false;
}

bool updateExplode(unsigned long now) {
  if (explodePhase == 0) {
    explodeScale += 0.05;
    if (explodeScale >= EXPLODE_MAX_SCALE) {
      explodePhase = 1; explodePhaseTime = now;
      for (int i=0;i<8;i++) {
        particles[i].active = true;
        particles[i].x = 0; particles[i].y = 0;
        float ang = random(0,360) * PI / 180.0;
        float spd = random(20,45) / 10.0;
        particles[i].vx = cos(ang)*spd;
        particles[i].vy = sin(ang)*spd - 1.5;
      }
    }
    return false;
  } else if (explodePhase == 1) {
    explodeScale = 0;
    for (int i=0;i<8;i++) {
      if (particles[i].active) {
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy += 0.35;
      }
    }
    if (now - explodePhaseTime > 1400) { explodePhase = 2; explodeScale = 0.1; }
    return false;
  } else {
    explodeScale += 0.015;
    if (explodeScale >= 1.0) { explodeScale = 1.0; return true; }
    return false;
  }
}

bool updateDart(unsigned long now) {
  if (now - dartStartTime > DART_DURATION) {
    dartEye1X += (0-dartEye1X)*0.3; dartEye1Y += (0-dartEye1Y)*0.3;
    dartEye2X += (0-dartEye2X)*0.3; dartEye2Y += (0-dartEye2Y)*0.3;
    if (abs(dartEye1X)<0.5 && abs(dartEye1Y)<0.5 && abs(dartEye2X)<0.5 && abs(dartEye2Y)<0.5) return true;
    return false;
  }
  if (now >= dartNextJump) {
    dartEye1X = random(-14,15); dartEye1Y = random(-10,11);
    dartEye2X = random(-14,15); dartEye2Y = random(-10,11);
    dartNextJump = now + random(80, 180);
  }
  return false;
}

bool updateSinkRepeat() {
  float speedFactor = 1.0 - (sinkCycle * 0.35);
  if (speedFactor < 0.18) speedFactor = 0.18;
  float step = 6.0 / speedFactor;
  if (sinkPhase == 0) {
    sinkY += step;
    if (sinkY >= 90) { sinkPhase = 1; sinkY = -90; }
  } else {
    sinkY += step;
    if (sinkY >= 0) {
      sinkY = 0; sinkPhase = 0; sinkCycle++;
      if (sinkCycle >= SINK_MAX_CYCLES) return true;
    }
  }
  return false;
}

// ================= EHSEG =================
void updateHunger(unsigned long now) {
  bool asleep = (dayPhase == DAY_SLEEP && !manualWakeActive);
  if (hungerLastTick == 0) hungerLastTick = now;

  if (hungerState == HUNGER_IDLE) {
    if (!asleep) hungerElapsedMs += (now - hungerLastTick);
    hungerLastTick = now;
    if (hungerElapsedMs >= hungerWaitDuration) {
      hungerState = HUNGER_CIRCLE;
      hungerTimerStart = now;
      hungerShapeBlend = 0;
    }
  } else if (hungerState == HUNGER_CIRCLE) {
    hungerLastTick = now;
    if (!asleep) {
      if (hungerShapeBlend < 1) hungerShapeBlend += 0.08;
      if (now - hungerTimerStart > HUNGER_WINDOW) {
        hungerState = HUNGER_SAD;
        hungerTimerStart = now;
      }
    } else {
      hungerTimerStart = now; // amig alszik, ne jarjon le a varakozasi ablak
    }
  } else if (hungerState == HUNGER_SAD) {
    hungerLastTick = now;
    if (!asleep) {
      if (now - hungerTimerStart > HUNGER_SAD_DURATION) {
        hungerState = HUNGER_IDLE;
        hungerTimerStart = now;
        hungerElapsedMs = 0;
        hungerWaitDuration = randomHungerWait();
        hungerShapeBlend = 0;
      }
    } else {
      hungerTimerStart = now;
    }
  }
}

// ================= SZEM-FORMA IDLE RENDSZER =================
void updateShapeState(unsigned long now) {
  bool allowed = (hungerState==HUNGER_IDLE) && (miniState==MF_NONE) && !gameActive && (dayPhase==DAY_ACTIVE);
  if (!allowed) {
    if (shapePhase != -1 && shapePhase != 2) shapePhase = 2;
  }
  if (shapePhase == -1) return; // az inditast mostantol a kozponti esemeny-utemezo vegzi
  if (shapePhase == 0) {
    shapeBlend += SHAPE_BLEND_SPEEDS[activeShapeType];
    if (shapeBlend >= 1) { shapeBlend=1; shapePhase=1; shapeHoldStart=now; }
  } else if (shapePhase == 1) {
    if (!allowed || now - shapeHoldStart > SHAPE_HOLD_DURATIONS[activeShapeType]) shapePhase = 2;
  } else if (shapePhase == 2) {
    shapeBlend -= SHAPE_BLEND_SPEEDS[activeShapeType];
    if (shapeBlend <= 0) { shapeBlend=0; shapePhase=-1; }
  }
}

// ================= JATEK LOGIKA =================
void updateGame(unsigned long now) {
  if (!gameActive || !gameWaitingChoice) return;
  if (now - gameStartTime > GAME_CHOICE_TIMEOUT) {
    resolveGame(random(0,2) == 0); // nem valaszoltal idoben, veletlen tippel dontunk helyetted
  }
}

// ================= SETUP / LOOP =================
void setup() {
  // UJ: canvas foglalasa - ITT, amikor a MiniFace tenylegesen indul.
  // Ekkorra a launcher mar meghivta a Radar::stopAudio()-t, tehat a radio
  // pufferei felszabadultak, es van eleg egybefuggo hely a 41KB-nak.
  if (allocCanvas()) {
    Serial.println("OK: canvas puffer sikeresen lefoglalva");
  } else {
    Serial.println("KRITIKUS HIBA: a MiniFace canvas foglalasa meghiusult!");
    // UJ: irjuk ki a kijelzore is, hogy ne csak egy fagyott/fekete kepet lass.
    // A loop() ilyenkor nem rajzol (canvasPtr == nullptr vedelem), tehat ez a
    // uzenet meg is marad a kepernyon, amig masik programra nem valtasz.
    display.initR(INITR_BLACKTAB);
    display.setRotation(1);
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_RED);
    display.setTextSize(1);
    display.setCursor(5, 40);
    display.print("Nincs eleg memoria");
    display.setCursor(5, 55);
    display.print("a MiniFace-hez.");
    display.setCursor(5, 75);
    display.setTextColor(ST77XX_WHITE);
    display.print("Valassz masik programot");
  }

  // UJ: watchdog-feliratkozas - ez hianyzott, emiatt jottek a "task not found"
  // hibak, es ha barmi elakadt a loop()-ban, semmi nem menthette meg tisztan.
  // Ugyanaz a blokk, mint a Radar/Ora/Idojaras setup()-jaban.
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 60000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  pinMode(BTN_PET_FEED, INPUT_PULLUP);
  pinMode(BTN_GAME_MENU, INPUT_PULLUP);

  display.initR(INITR_BLACKTAB);
  display.setRotation(1);
  randomSeed(analogRead(0));

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-wifiStart < 10000) {
    delay(200);
    esp_task_wdt_reset();
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  hungerTimerStart = millis();
  hungerWaitDuration = randomHungerWait();
  hungerElapsedMs = 0;
  hungerLastTick = millis();
  lastEventCheck = millis();
  nextEventWait = random(EVENT_CHECK_MIN, EVENT_CHECK_MAX);
}

void loop() {
  esp_task_wdt_reset();
  // UJ: biztonsagi ellenorzes - ha nincs canvas, ne rajzoljunk (osszeomlas helyett
  // egyszeruen nem csinalunk semmit ebben a korben)
  if (canvasPtr == nullptr) return;
  unsigned long now = millis();

  updateClock();
  fetchWeatherIfDue();

  handleButton(btnPet, BTN_PET_FEED, onPetShort, onPetLong);
  handleButton(btnGame, BTN_GAME_MENU, onGameShort, onGameLong);

  bool hungerBlocking = (hungerState != HUNGER_IDLE);
  bool sleeping = (miniState == MF_SLEEP);

  updateHunger(now);

  if (manualWakeActive && now > manualWakeUntil) {
    manualWakeActive = false;
    if (dayPhase == DAY_SLEEP) {
      miniState = MF_SLEEP; sleepZVisible = 0; sleepZTime = now;
      eye1X = eye1Y = eye2X = eye2Y = 0;
      isMoving = false;
      if (hungerState != HUNGER_IDLE) {
        hungerState = HUNGER_IDLE; hungerElapsedMs = 0; hungerWaitDuration = randomHungerWait();
      }
    }
  }

  updateGame(now);
  updateGameSelect(now);
  updateReactionGame(now);
  updateSyncGame(now);

  if (miniState == MF_NONE && !hungerBlocking && !sleeping && !gameActive &&
      dayPhase == DAY_ACTIVE && shapePhase == -1 &&
      now - lastEventCheck > nextEventWait) {
    lastEventCheck = now; nextEventWait = random(EVENT_CHECK_MIN, EVENT_CHECK_MAX);
    triggerRandomEvent();
  }

  bool posControlled = (miniState==MF_JOY || miniState==MF_SLEEP || miniState==MF_WAKE ||
                         miniState==MF_CHARGE_HAPPY || miniState==MF_FEED_HAPPY || miniState==MF_EXPLODE ||
                         miniState==MF_DART || miniState==MF_SINK_REPEAT ||
                         miniState==MF_CIRCLE_SEQ || miniState==MF_GAME_WIN ||
                         miniState==MF_INFO_TIME || miniState==MF_INFO_WEATHER ||
                         miniState==MF_INFO_KIND);
  if (!posControlled && !hungerBlocking && !reactionActive && !syncActive) {
    if (!isMoving) {
      if (now - lastMoveTime > waitTime) {
        targetX = random(-25,26); targetY = random(-20,21);
        isMoving = true; speed1 = random(15,26)/100.0; speed2 = random(15,26)/100.0;
      }
    } else {
      eye1X += (targetX-eye1X)*speed1; eye1Y += (targetY-eye1Y)*speed1;
      eye2X += (targetX-eye2X)*speed2; eye2Y += (targetY-eye2Y)*speed2;
      curX=(eye1X+eye2X)/2.0; curY=(eye1Y+eye2Y)/2.0;
      if (miniState != MF_THINKING && miniState != MF_DREAM && abs(targetX-curX)<1 && abs(targetY-curY)<1) {
        isMoving=false; lastMoveTime=now; waitTime=random(800,3000);
      }
    }
  }

  updateShapeState(now);

  bool eye1Closed=false, eye2Closed=false;
  float eye1OffX=0, eye1OffY=0, eye2OffX=0, eye2OffY=0;
  float eye1Scale=1.0, eye2Scale=1.0;
  bool showThoughtBubble=false; int thoughtDots=0;
  bool showDreamBubble=false; int dreamIconToShow=0;
  bool showZZZ=false; int zzzCount=0;
  bool eye1ForceCircle=false, eye2ForceCircle=false;
  float circleBlendAmt = 0;

  if (hungerBlocking) {
    if (hungerState == HUNGER_CIRCLE) {
      eye1ForceCircle = true; eye2ForceCircle = true;
      circleBlendAmt = hungerShapeBlend;
    }
    // HUNGER_SAD: a shapeToUse resznel kulon kezelve lentebb
  } else if (miniState == MF_THINKING) {
    bool done = updateThinking(now);
    showThoughtBubble = thinkInPlace; thoughtDots = thinkDotsVisible;
    if (done) { miniState=MF_NONE; isMoving=false; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_DREAM) {
    bool done = updateDream(now);
    showDreamBubble = dreamInPlace; dreamIconToShow = dreamIcon;
    if (done) { miniState=MF_NONE; isMoving=false; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_GAME_WIN && gameWinHolding) {
    if (now - gameWinHoldStart > GAME_WIN_HOLD_DURATION) {
      gameWinHolding = false;
      miniState = MF_NONE; lastMoveTime=now; waitTime=random(500,1500);
    }
  } else if (miniState == MF_JOY || miniState == MF_GAME_WIN) {
    bool done = updateJoy();
    if (joyPhase == 0) {
      float rad = radians(joyTheta);
      eye2OffX = JOY_RADIUS*(1-cos(rad)); eye2OffY = -JOY_RADIUS*sin(rad);
      eye1OffX = -JOY_RADIUS*(1-cos(rad)); eye1OffY = -JOY_RADIUS*sin(rad);
    } else if (joyPhase == 1) { eye1OffY=joyDownY; eye2OffY=joyDownY; }
    else if (joyPhase == 2) { eye1OffY=joySpringPos; eye2OffY=joySpringPos; }
    if (done) {
      if (miniState == MF_GAME_WIN) { gameWinHolding = true; gameWinHoldStart = now; }
      else { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
    }
  } else if (miniState == MF_WINK) {
    if (winkEyeIndex==0) eye1Closed=true; else eye2Closed=true;
    if (updateWink(now)) miniState=MF_NONE;
  } else if (miniState == MF_SLEEP) {
    updateSleepZZZ(now);
    eye1Closed=true; eye2Closed=true;
    showZZZ = true; zzzCount = (sleepZVisible > 3) ? 0 : sleepZVisible;
  } else if (miniState == MF_WAKE) {
    bool done = updateWake(now);
    eye1OffX+=eye1WakeOffX; eye1OffY+=eye1WakeOffY; eye2OffX+=eye2WakeOffX; eye2OffY+=eye2WakeOffY;
    if (done) { miniState = MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_CHARGE_HAPPY) {
    bool done = updateChargeHappy();
    eye1OffY = chargeBounceY; eye2OffY = chargeBounceY;
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_FEED_HAPPY) {
    bool done = updateFeedHappy(now);
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_CIRCLE_SEQ) {
    bool done = updateCircleSeq(now);
    if (circleSeqStep == 0) { eye1ForceCircle = true; circleBlendAmt = circleSeqBlend; }
    else if (circleSeqStep == 1) { eye2ForceCircle = true; circleBlendAmt = circleSeqBlend; }
    else { eye1ForceCircle = true; eye2ForceCircle = true; circleBlendAmt = circleSeqBlend; }
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_EXPLODE) {
    bool done = updateExplode(now);
    eye1Scale = explodeScale; eye2Scale = explodeScale;
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_DART) {
    bool done = updateDart(now);
    eye1OffX = dartEye1X; eye1OffY = dartEye1Y;
    eye2OffX = dartEye2X; eye2OffY = dartEye2Y;
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_SINK_REPEAT) {
    bool done = updateSinkRepeat();
    eye1OffY = sinkY; eye2OffY = sinkY;
    if (done) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_GAME_LOSE) {
    if (now - gameLoseStart > GAME_LOSE_DURATION) { miniState=MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else if (miniState == MF_INFO_TIME || miniState == MF_INFO_WEATHER || miniState == MF_INFO_KIND) {
    bool done = updateInfoFace(now);
    eye1Scale = infoEyeScale; eye2Scale = infoEyeScale;
    if (done) { miniState = MF_NONE; lastMoveTime=now; waitTime=random(500,1500); }
  } else {
    if (!isBlinking && now - lastBlinkTime > random(3000,7000)) { isBlinking=true; lastBlinkTime=now; }
    else if (isBlinking && now - lastBlinkTime > 150) { isBlinking=false; }
    eye1Closed = isBlinking; eye2Closed = isBlinking;
  }

  // ================= RENDER =================
  canvas.fillScreen(BG_BLACK);

  int e1x = EYE1_BASE_X + (int)(eye1X+eye1OffX);
  int e1y = EYE_BASE_Y  + (int)(eye1Y+eye1OffY);
  int e2x = EYE2_BASE_X + (int)(eye2X+eye2OffX);
  int e2y = EYE_BASE_Y  + (int)(eye2Y+eye2OffY);

  ShapeParams shapeToUse = SHAPE_BASE;
  bool wantsFocused = (dayPhase != DAY_ACTIVE);
  if (wantsFocused) {
    shapeToUse = SHAPE_DEFS[SHAPE_FOCUSED];
  } else if (hungerState == HUNGER_SAD) {
    shapeToUse = SHAPE_DEFS[SHAPE_WORRIED];
  } else if (shapePhase != -1) {
    shapeToUse = lerpShape(SHAPE_BASE, SHAPE_DEFS[activeShapeType], shapeBlend);
  } else if (miniState == MF_CHARGE_HAPPY) {
    shapeToUse = SHAPE_DEFS[SHAPE_CUTE];
  } else if (miniState == MF_GAME_LOSE) {
    shapeToUse = SHAPE_DEFS[SHAPE_ANGRY];
  } else if (miniState == MF_GAME_WIN && gameWinHolding) {
    shapeToUse = SHAPE_DEFS[SHAPE_CUTE];
  }

  ShapeParams eye1Shape = shapeToUse;
  ShapeParams eye2Shape = shapeToUse;
  if (eye1ForceCircle) eye1Shape = lerpShape(shapeToUse, SHAPE_DEFS[SHAPE_CIRCLE], circleBlendAmt);
  if (eye2ForceCircle) eye2Shape = lerpShape(shapeToUse, SHAPE_DEFS[SHAPE_CIRCLE], circleBlendAmt);

  int e1cx = e1x + EYE_W/2, e1cy = e1y + EYE_H/2;
  int e2cx = e2x + EYE_W/2, e2cy = e2y + EYE_H/2;

  if (miniState == MF_FEED_HAPPY && feedPhase == 0) {
    int ballX = (e1cx + e2cx) / 2;
    int ballY = EYE_BASE_Y + EYE_H/2 + (int)feedBallY;
    canvas.fillCircle(ballX, ballY, 6, LEMON_YELLOW);
  }

  uint16_t eyeColor = NEON_BLUE;
  if (miniState == MF_FEED_HAPPY && feedPhase == 1 && feedFlashOn) eyeColor = LEMON_YELLOW;
  if (reactionActive) eyeColor = (reactionPhase == REACT_WAITING) ? REACT_RED : REACT_GREEN;

  if (syncActive) {
    drawSyncEye(e1cx, e1cy, syncShapeLeft, eyeColor);
    drawSyncEye(e2cx, e2cy, syncShapeRight, eyeColor);
  } else {
    drawEyeShaped(e1cx, e1cy, eye1Shape, eye1Closed, true, eye1Scale, eyeColor);
    drawEyeShaped(e2cx, e2cy, eye2Shape, eye2Closed, false, eye2Scale, eyeColor);
  }

  if (miniState == MF_EXPLODE && explodePhase >= 1) {
    for (int i=0;i<8;i++) {
      if (particles[i].active) {
        canvas.fillCircle((e1cx+e2cx)/2 + (int)particles[i].x, e1cy + (int)particles[i].y, 2, NEON_BLUE);
      }
    }
  }

  if (miniState == MF_INFO_TIME && infoPhase == 1) {
    canvas.setTextColor(NEON_BLUE);
    canvas.setTextSize(2);
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", currentHour, currentMinute);
    canvas.setCursor(38, 46);
    canvas.print(timeBuf);
    canvas.setTextSize(1);
    char dateBuf[24];
    snprintf(dateBuf, sizeof(dateBuf), "%s %d.", hungarianMonths[currentMonth-1], currentDay);
    canvas.setCursor(50, 74);
    canvas.print(dateBuf);
  } else if (miniState == MF_INFO_WEATHER && infoPhase == 1) {
    canvas.setTextColor(NEON_BLUE);
    canvas.setTextSize(1);
    canvas.setCursor(56, 36);
    canvas.print(CFG_CITY_NAME);
    canvas.setTextSize(2);
    char tempBuf[10];
    if (isnan(lastTemperature)) snprintf(tempBuf, sizeof(tempBuf), "--.-C");
    else snprintf(tempBuf, sizeof(tempBuf), "%.1fC", lastTemperature);
    canvas.setCursor(38, 52);
    canvas.print(tempBuf);
    canvas.setTextSize(1);
    String desc = weatherCodeToHungarian(lastWeatherCode);
    canvas.setCursor(80 - (int)(desc.length()*3), 78);
    canvas.print(desc);
  } else if (miniState == MF_INFO_KIND && infoPhase == 1) {   // <-- UJ
    drawKindMessage(infoKindMessage);
  }

  if (showThoughtBubble) drawThoughtBubbleDots(e2x+EYE_W, e2y, thoughtDots);
  if (showDreamBubble)   drawThoughtBubbleIcon(e2x+EYE_W, e2y, dreamIconToShow);
  if (showZZZ)            drawSleepZZZ(e2x+EYE_W-6, e2y-4, zzzCount);

  if (millis() < gameMessageUntil && gameMessage.length() > 0) {
    canvas.setTextSize(1);
    canvas.setTextColor(NEON_BLUE);
    canvas.setCursor(4, 116);
    canvas.print(gameMessage);
  } else if (millis() < menuMessageUntil) {
    canvas.setTextSize(1);
    canvas.setTextColor(NEON_BLUE);
    canvas.setCursor(4, 116);
    canvas.print(menuMessage);
  }

  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);

  int delays[] = {15, 30, 60};
  delay(delays[random(0,3)]);
}


} // namespace MiniFace


#endif
