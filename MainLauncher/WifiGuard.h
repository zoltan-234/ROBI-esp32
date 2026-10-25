#ifndef WIFI_GUARD_H
#define WIFI_GUARD_H

// ============================================================================
//  WiFi vedelem - csatlakozas idokorlattal + "Nincs WiFi" kepernyo
// ============================================================================
//  A Radar, Ora es Idojaras program eredetileg vegtelen ciklusban varta a
//  WiFi-t, igy halozat nelkul beragadt (es a launcher gombjai sem mukodtek).
//  Ezek a fuggvenyek ezt elozik meg: CFG_WIFI_TIMEOUT_MS utan feladjak, a
//  program pedig a "Nincs WiFi" kepernyot mutatja.
// ============================================================================

#include <WiFi.h>
#include <Adafruit_ST7735.h>
#include <esp_task_wdt.h>
#include "config.h"

// Csatlakozas idokorlattal. true = sikerult.
inline bool wifiConnectWithTimeout(const char* ssid, const char* pass) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CFG_WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " WiFi OK" : " WiFi nem elerheto");
  return WiFi.status() == WL_CONNECTED;
}

// "Nincs WiFi" kepernyo (160x128, rotation 1)
inline void drawNoWifiScreen(Adafruit_ST7735 &t, const char* progName) {
  t.fillScreen(ST77XX_BLACK);

  t.setTextSize(1);
  t.setTextColor(ST77XX_CYAN);
  t.setCursor(5, 8);
  t.print(progName);

  t.setTextSize(2);
  t.setTextColor(ST77XX_RED);
  t.setCursor(10, 40);
  t.print("Nincs WiFi");

  t.setTextSize(1);
  t.setTextColor(ST77XX_WHITE);
  t.setCursor(5, 72);
  t.print("Halozat: ");
  t.print(SECRET_WIFI_SSID);

  t.setTextColor(0x8410);  // szurke
  t.setCursor(5, 96);
  t.print("Ujraproba: ugyanez a gomb");
  t.setCursor(5, 108);
  t.print("Szemek: D25 hosszan");
}

#endif
