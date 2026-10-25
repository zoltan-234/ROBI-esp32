/*
  ============================================================================
   R.O.B.I.  -  Radar · Ora · Barat · Idojaras
   ESP32 MULTI-PROGRAM LAUNCHER
  ============================================================================
   A 4 eredeti program VALTOZATLANUL fut tovabb, mindegyik sajat namespace-ben:

     D33  rovid nyomas  ->  Repulo radar   (repulo_radar.ino)
     D32  rovid nyomas  ->  Ora            (ora.ino)
     D35  rovid nyomas  ->  Idojaras       (idojaras.ino)
     D35  HOSSZU nyomas ->  MiniFace robot (MiniFace.ino)

   Barmelyik gomb megnyomasakor lefut egy 4 masodperces korkoros animacio
   (korkoros.ino), majd elindul a valasztott program (teljes ujrainditassal).

   A D26 / D27 gombok tovabbra is a programok sajat gombjai maradnak
   (MiniFace: simogatas/jatek, Ora: nagy ora mod) - azokhoz nem nyultunk.

  ----------------------------------------------------------------------------
   GOMB BEKOTES
   D33, D32, D25: gomb a GPIO es a GND koze. Belso felhuzo (INPUT_PULLUP)
   van bekapcsolva, kulso ellenallas nem kell.

   Megj.: eredetileg D35 volt az idojaras gombja, de a GPIO34..39 labakon
   NINCS belso felhuzo, ezert lett helyette D25.
  ============================================================================
*/

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <esp_task_wdt.h>

#include "SpinnerFrames.h"    // korkoros.ino animacios keretei
#include "RadarApp.h"         // repulo_radar.ino
#include "OraApp.h"           // ora.ino
#include "IdojarasApp.h"      // idojaras.ino
#include "MiniFaceApp.h"      // MiniFace.ino

// ---------------------------------------------------------------- GOMBOK ---
#define BTN_RADAR     33
#define BTN_ORA       32
#define BTN_IDOJARAS  25     // rovid = idojaras, hosszu = MiniFace

#define LONG_PRESS_MS 800    // ennyitol szamit hosszu nyomasnak

// --------------------------------------------------------------- KIJELZO ---
// Sajat kijelzo-objektum csak a spinnerhez (ugyanazok a labak).
#define LNC_TFT_CS   5
#define LNC_TFT_RST  4
#define LNC_TFT_DC   2
Adafruit_ST7735 lncTft = Adafruit_ST7735(LNC_TFT_CS, LNC_TFT_DC, LNC_TFT_RST);

// --------------------------------------------------------------- ALLAPOT ---
enum ProgId { PROG_NONE, PROG_RADAR, PROG_ORA, PROG_IDOJARAS, PROG_MINIFACE };

ProgId currentProg = PROG_NONE;
ProgId requestedProg = PROG_MINIFACE; // indulaskor ez fut le elsokent
                                      // (MiniFace: alig hasznal adatforgalmat,
                                      //  a radio nem indul el magatol)
unsigned long switchGuardUntil = 0;  // valtas utan eddig nem fogadunk gombot

struct Btn {
  uint8_t pin;
  bool wasDown = false;
  unsigned long downAt = 0;
  bool longFired = false;
  bool lastRaw = false;      // utolso nyers olvasas (LOW=true)
  uint8_t stableCount = 0;   // hany egymast koveto azonos minta volt
};

Btn bRadar    = { BTN_RADAR };
Btn bOra      = { BTN_ORA };
Btn bIdojaras = { BTN_IDOJARAS };

// ------------------------------------------------------------ WATCHDOG -----
// A "task not found" hibauzenetek elkerulesehez nyilvantartjuk, hogy a launcher
// task-ja epp fel van-e iratkozva a watchdogra. Az esp_task_wdt_reset() ugyanis
// NEM csendben ter vissza, ha nincs feliratkozva - hibauzenetet ir a soros portra.
bool wdtSubscribed = false;

inline void wdtKick() {
  if (wdtSubscribed) esp_task_wdt_reset();
}

// ------------------------------------------------ R.O.B.I. INDULO FELIRAT --
// UJ: csak bekapcsolaskor (az elso spinner alatt) jelenik meg, programvaltaskor nem.
bool bootTitlePending = true;

void drawBootTitle() {
  lncTft.setTextWrap(false);

  lncTft.setTextSize(2);
  lncTft.setTextColor(ST77XX_CYAN);
  lncTft.setCursor(33, 12);            // "R.O.B.I." = 8 karakter x 12 px, kozepre
  lncTft.print("R.O.B.I.");

  lncTft.setTextSize(1);
  lncTft.setTextColor(0x8410);         // szurke
  lncTft.setCursor(8, 104);            // 24 karakter x 6 px, kozepre
  lncTft.print("Radar Ora Barat Idojaras");
}

// ------------------------------------------------- KORKOROS ATMENET (4 mp) --
void playSpinner(unsigned long durationMs) {
  lncTft.initR(INITR_BLACKTAB);
  lncTft.setRotation(1);
  lncTft.fillScreen(ST77XX_BLACK);
  if (bootTitlePending) drawBootTitle();   // UJ: a spinner csak a sajat 48x48-as teruletet torli

  int x = (lncTft.width()  - SPIN_W) / 2;
  int y = (lncTft.height() - SPIN_H) / 2;

  unsigned long start = millis();
  int frame = 0;

  while (millis() - start < durationMs) {
    lncTft.fillRect(x, y, SPIN_W, SPIN_H, ST77XX_BLACK);
    lncTft.drawBitmap(x, y, spinFrames[frame], SPIN_W, SPIN_H, ST77XX_WHITE);
    frame = (frame + 1) % SPIN_FRAME_COUNT;
    wdtKick();
    delay(SPIN_FRAME_DELAY);
  }
  lncTft.fillScreen(ST77XX_BLACK);
}

// ------------------------------------------------------- PROGRAM VALTAS ----
void leaveProgram(ProgId p) {
  // Ha MiniFace-t hagyunk el, szabaditsuk fel a 41KB-os canvas-t, hogy a
  // Radar radio-ja (Audio pufferek, TLS-kapcsolat) elferjen a heapben.
  if (p == PROG_MINIFACE) {
    MiniFace::freeCanvas();
  }

  // UJ: ha Radar-t hagyjuk el, allitsuk le a radio-streamet, kulonben a
  // nyitva maradt HTTPS-kapcsolat a kovetkezo program alatt is foglalja a
  // WiFi/TLS eroforrasokat, es osszeomlast/ujrainditast okozhat.
  // (Ez egyben fel is szabaditja a helyet a MiniFace canvas-anak.)
  if (p == PROG_RADAR) {
    Radar::stopAudio();
  }

  // A radar/ora/idojaras/miniface a sajat setup()-jaban feliratkozik a watchdogra.
  // Kilepeskor leiratkozunk, hogy a kovetkezo program tisztan induljon.
  // Csak akkor hivjuk, ha tenylegesen fel vagyunk iratkozva - kulonben
  // "delete_entry: task not found" hibat irna a soros portra.
  if (wdtSubscribed) {
    esp_task_wdt_delete(NULL);
    wdtSubscribed = false;
  }
}

void startProgram(ProgId p) {
  switch (p) {
    case PROG_RADAR:    Radar::setup();    break;
    case PROG_ORA:      Ora::setup();      break;
    case PROG_IDOJARAS: Idojaras::setup(); break;
    case PROG_MINIFACE: MiniFace::setup(); break;
    default: break;
  }
  // Mind a 4 program setup()-ja meghivja az esp_task_wdt_add(NULL)-t,
  // igy innentol fel vagyunk iratkozva.
  wdtSubscribed = true;
  currentProg = p;
}

void runProgram(ProgId p) {
  switch (p) {
    case PROG_RADAR:    Radar::loop();    break;
    case PROG_ORA:      Ora::loop();      break;
    case PROG_IDOJARAS: Idojaras::loop(); break;
    case PROG_MINIFACE: MiniFace::loop(); break;
    default: break;
  }
}

// ----------------------------------------------------------- GOMB OLVASAS --
// short: elengedeskor, ha nem volt hosszu
// long : mar nyomva tartas kozben tuzel
void pollButton(Btn &b, ProgId onShort, ProgId onLong) {
  unsigned long now = millis();

  // Kozvetlenul program-valtas utan ne fogadjunk el gombot: a spinner alatt
  // nem volt mintavetel, es a gomb meg lehet lenyomva / a vonal tranziens.
  if (now < switchGuardUntil) {
    b.lastRaw = (digitalRead(b.pin) == LOW);
    b.stableCount = 0;
    b.wasDown = b.lastRaw;   // ha nyomva van, "mar lenyomottnak" vesszuk -> uj el kell
    return;
  }

  // --- TOBBSZOROS MEGEROSITES (debounce zaj ellen) ---
  // Egyetlen zajos LOW minta NEM valt tobbet programot: csak akkor tekintjuk
  // lenyomottnak, ha several egymast koveto olvasas mind ugyanaz.
  bool raw = (digitalRead(b.pin) == LOW);
  if (raw == b.lastRaw) {
    if (b.stableCount < 255) b.stableCount++;
  } else {
    b.stableCount = 0;
    b.lastRaw = raw;
  }
  // csak stabil (>=4 azonos minta) allapotot fogadunk el
  if (b.stableCount < 4) return;
  bool down = raw;

  if (down && !b.wasDown) {                 // lenyomas kezdete
    b.wasDown = true;
    b.downAt = now;
    b.longFired = false;
  }
  else if (down && b.wasDown) {             // nyomva tartas
    if (!b.longFired && onLong != PROG_NONE && (now - b.downAt) > LONG_PRESS_MS) {
      b.longFired = true;
      requestedProg = onLong;
    }
  }
  else if (!down && b.wasDown) {            // elengedes
    b.wasDown = false;
    if (!b.longFired && (now - b.downAt) > 50) {   // 50 ms pergesmentesites
      requestedProg = onShort;
    }
  }
}

void pollAllButtons() {
  pollButton(bRadar,    PROG_RADAR,    PROG_NONE);
  pollButton(bOra,      PROG_ORA,      PROG_NONE);
  pollButton(bIdojaras, PROG_IDOJARAS, PROG_MINIFACE);
}

// ================================================================= SETUP ====
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== R.O.B.I. - ESP32 Multi-Program Launcher ===");

  // ---- DIAGNOSZTIKA: kiirjuk, miert indult(ujra) az ESP32 ----
  // Ha legkozelebb "magatol" a radarra valt, nezd meg a Serial Monitor-t:
  // ha itt POWERON all, tenyleg csak most dugtad be / kapcsoltad be.
  // Ha BROWNOUT, TASK_WDT, PANIC vagy INT_WDT all, az ESP32 osszeomlott/
  // ujraindult MiniFace kozben, es EZERT ugrott a (kemenyen bedrotozott
  // alapertelmezett) radar programra - nem gombhiba volt.
  esp_reset_reason_t r = esp_reset_reason();
  Serial.print("Reset ok: ");
  switch (r) {
    case ESP_RST_POWERON:   Serial.println("POWERON (tapfesz. bekapcsolas)"); break;
    case ESP_RST_SW:        Serial.println("SW (szoftveres ujrainditas)"); break;
    case ESP_RST_PANIC:     Serial.println("PANIC (osszeomlas! pl. null-pointer)"); break;
    case ESP_RST_INT_WDT:   Serial.println("INT_WDT (interrupt watchdog)"); break;
    case ESP_RST_TASK_WDT:  Serial.println("TASK_WDT (feladat-watchdog lejart)"); break;
    case ESP_RST_WDT:       Serial.println("WDT (egyeb watchdog)"); break;
    case ESP_RST_BROWNOUT:  Serial.println("BROWNOUT (tapfeszultseg-kieses!)"); break;
    default:                Serial.printf("egyeb (%d)\n", (int)r); break;
  }

  pinMode(BTN_RADAR, INPUT_PULLUP);
  pinMode(BTN_ORA,   INPUT_PULLUP);
  pinMode(BTN_IDOJARAS, INPUT_PULLUP);   // D25: van belso felhuzo, nem kell kulso ellenallas

  // requestedProg = PROG_MINIFACE, ezert a loop elso koreben a MiniFace indul
}

// ================================================================== LOOP ====
void loop() {
  wdtKick();

  // 1) van program-valtasi keres? (ugyanaz a gomb = ujrainditas)
  if (requestedProg != PROG_NONE) {
    ProgId target = requestedProg;
    requestedProg = PROG_NONE;

    Serial.printf("-> program valtas: %d\n", (int)target);

    leaveProgram(currentProg);
    playSpinner(4000);          // <- a korkoros animacio, 4 masodperc
    bootTitlePending = false;   // UJ: a R.O.B.I. felirat csak az elso inditaskor
    startProgram(target);

    // ha a spinner alatt vegig nyomva tartottad a gombot, ne induljon ujra
    bRadar.wasDown = bOra.wasDown = bIdojaras.wasDown = false;
    bRadar.longFired = bOra.longFired = bIdojaras.longFired = true;
    bRadar.stableCount = bOra.stableCount = bIdojaras.stableCount = 0;
    switchGuardUntil = millis() + 400;   // 0,4 mp belepesi vedelem
  }

  // 2) az aktualis program egy kore (valtozatlan eredeti kod)
  runProgram(currentProg);

  // 3) gombok figyelese
  pollAllButtons();
}
