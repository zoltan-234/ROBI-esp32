#ifndef SECRETS_H
#define SECRETS_H

// ============================================================================
//  SZEMELYES ADATOK - SABLON
// ============================================================================
//  1. Masold le ezt a fajlt UGYANEBBE a mappaba  secrets.h  neven.
//  2. A secrets.h-ba ird be a sajat adataidat.
//
//  A secrets.h benne van a .gitignore-ban, igy SOSEM kerul fel GitHubra.
//  Ezt a fajlt (secrets.example.h) ne ird at - ez csak a minta.
// ============================================================================

// ---- WiFi --------------------------------------------------------------
// Az eszkoz ehhez a halozathoz csatlakozik (otthoni WiFi vagy telefonos
// hotspot is lehet). Csak 2,4 GHz-es halozat mukodik - az ESP32 az 5 GHz-et
// nem latja.
#define SECRET_WIFI_SSID      "IDE_A_WIFI_NEVE"
#define SECRET_WIFI_PASSWORD  "IDE_A_WIFI_JELSZAVA"

// ---- OpenSky Network fiok (Repulo radar) - HAGYD URESEN ------------------
// Ures ("") ertekkel a radar fiok nelkul (anonim modon) kerdezi le a
// repuloket - ez a javasolt beallitas, 2 percenkenti frissitessel belefer
// az anonim keretbe.
// FIGYELEM: az OpenSky 2026. marcius 18. ota NEM fogadja el a
// felhasznalonev + jelszo belepest (csak OAuth2-t), ezert ha ide fiokadatot
// irsz, a radar valoszinuleg hibat kap. Csak a jovobeli OAuth2-tamogatashoz
// maradt meg ez a ket sor.
#define SECRET_OPENSKY_USER   ""
#define SECRET_OPENSKY_PASS   ""

#endif
