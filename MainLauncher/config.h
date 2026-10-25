#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
//  MAINLAUNCHER - BEALLITASOK
// ============================================================================
//  Itt minden olyan erteket egy helyen talalsz, amit erdemes a sajat
//  helyzetedhez igazitani. A szemelyes adatok (WiFi, OpenSky fiok) NEM itt
//  vannak, hanem a secrets.h-ban.
// ============================================================================

// ---- Szemelyes adatok ------------------------------------------------------
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Hianyzik a secrets.h! Masold le a secrets.example.h fajlt secrets.h neven ugyanebbe a mappaba, es ird bele a WiFi adataidat."
#endif

// ---- WiFi --------------------------------------------------------------
// Ennyi ideig probal csatlakozni egy program indulaskor. Ha nem sikerul,
// a program "Nincs WiFi" kepernyot mutat, es a gombokkal tovabb lehet lepni.
#define CFG_WIFI_TIMEOUT_MS   10000

// ---- Helyszin (Idojaras, Ora, MiniFace idojaras) ----------------------------
// Koordinatat pl. Google Maps-bol szerezhetsz: jobb klikk a terkepen ->
// az elso sor a "szelesseg, hosszusag". Ket tizedesjegy bovan eleg.
// A varos neve a kijelzon jelenik meg - max. 8 karakter, ekezet nelkul.
#define CFG_CITY_NAME         "Budapest"
#define CFG_WEATHER_LAT       47.50
#define CFG_WEATHER_LON       19.04

// ---- Repulo radar terulete -----------------------------------------------
// Egy teglalap a terkepen: deli/nyugati (min) es eszaki/keleti (max) szele.
// Google Maps: jobb klikk a bal also sarokra -> LAMIN, LOMIN;
//              jobb klikk a jobb felso sarokra -> LAMAX, LOMAX.
// FIGYELEM: itt idezojelben kell megadni ("47.20"), az idojarasnal nem.
// Az alapertek Budapest es Liszt Ferenc repuloter (LHBP) kornyeke.
// Tul nagy terulet = sok repulo = keves memoria, ne nagyitsd meg nagyon.
#define CFG_RADAR_LAMIN       "47.20"
#define CFG_RADAR_LOMIN       "18.68"
#define CFG_RADAR_LAMAX       "47.68"
#define CFG_RADAR_LOMAX       "19.62"

// ---- ATC radio csatornak (Repulo radar 2. es 3. oldala) --------------------
// Formatum: { "nev a kijelzon", "frekvencia", "stream URL" },
// Uj link: https://www.liveatc.net/search/?icao=LHBP -> a csatorna melletti
// "Listen" gomb -> a felugro lejatszo oldal CIMSORABAN levo link kell.
// Csatornat hozza is adhatsz, el is vehetsz - a D27 gomb korbe lepked rajtuk.
// Minden sor vegen vesszo es \ , kiveve az utolsot.
#define CFG_ATC_CHANNELS \
  {"Approach 1", "122.980", "https://s1-bos.liveatc.net/lhbp_app_122980"}, \
  {"Tower",      "118.715", "https://s1-bos.liveatc.net/lhbp_twr_118715"}, \
  {"Ground",     "121.905", "https://s1-bos.liveatc.net/lhbp_gnd"}

// ---- Segedmakro (ne modositsd) ---------------------------------------------
// A szam-koordinatat szovegge alakitja az URL-ekhez: 47.50 -> "47.50"
#define CFG_STR_(x) #x
#define CFG_STR(x)  CFG_STR_(x)

#endif
