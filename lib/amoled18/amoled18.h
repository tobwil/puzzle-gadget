// Board-Schicht für das Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2).
// Display CO5300 (QSPI), Touch CST816 (I²C 0x15), IMU QMI8658 (0x6B), IO-Expander TCA9554 (0x20), Codec ES8311 (0x18).
// Init-Sequenzen aus Hallen-Legenden Mini übernommen (dort auf der Hardware erprobt).
//
// Mit -DBOARD_AMOLED216: Waveshare ESP32-S3-Touch-AMOLED-2.16. Display CO5300 480×480 (Reset GPIO39),
// Touch CST9220 (I²C 0x5A, INT 11, RST 40), PMU AXP2101 (0x34), IMU QMI8658 (0x6B), Codec ES8311 (0x18),
// Mikrofon-ADC ES7210 (0x40). Kein IO-Expander. Das Spiel (368×448) liegt mittig im Panel.
#pragma once
#include <Arduino.h>
#include <ESP_I2S.h>

#include "Arduino_GFX_Library.h"

namespace amoled18 {

constexpr int LCD_W = 368, LCD_H = 448;   // Spielfläche
#ifdef BOARD_AMOLED216
constexpr int PANEL_W = 480, PANEL_H = 480;
#else
constexpr int PANEL_W = LCD_W, PANEL_H = LCD_H;
#endif
constexpr int OFF_X = (PANEL_W - LCD_W) / 2, OFF_Y = (PANEL_H - LCD_H) / 2;   // Lage der Spielfläche im Panel
constexpr int PIN_SDA = 15, PIN_SCL = 14, PIN_BOOT = 0, PIN_PA_EN = 46;

// I²C, IO-Expander, Display (schwarz, Helligkeit 0), Touch (ohne Auto-Schlaf), IMU
bool begin();

Arduino_DataBus* bus();
Arduino_CO5300* panel();

// Touch: Pixel der Spielfläche (ohne Rand). false bei I²C-Fehler oder ohne neue Daten.
bool readTouch(bool& down, int16_t& x, int16_t& y);
bool touchOk();

// IMU: Beschleunigung in g in Sensorachsen, ±8 g, 1 kHz, Tiefpass ~130 Hz
bool imuOk();
bool readAccel(float v[3]);

// Lautsprecher und Mikrofon: I2S + ES8311 + Verstärker. Danach mit i2s().write() Stereo-16-Bit schreiben,
// mit i2s().readBytes() das Mikrofon lesen (Stereo-16-Bit, das Mikrofon liegt auf dem linken Kanal).
bool audioBegin(uint32_t rate, int volume);
I2SClass& i2s();

inline bool bootPressed() { return digitalRead(PIN_BOOT) == LOW; }

// Tasten (AMOLED-2.16): Power-Taste über den AXP2101 (kurz/lang), Taste an GPIO18.
// pollButtons() regelmäßig aus genau einem Task aufrufen (liest den PMU per I²C alle 100 ms).
enum class Button : uint8_t { Idle, ScreenToggle, PowerOff };
Button pollButtons();
void powerOff();   // Gerät über den PMU ausschalten (einschalten: Power-Taste)
bool pmuOk();

// TF-Karte: SDMMC 1-Bit (CLK 2, CMD 1, D0 3, laut Waveshare-Beispiel), eingehängt unter /sd. false = keine Karte
bool sdBegin();

// Echtzeituhr PCF85063 (I²C 0x51): hält die Ortszeit (Sekunden seit 1970, ohne Zeitzone).
// rtcRead: false, wenn die Uhr fehlt oder nie gestellt wurde (Oszillator-Stopp-Bit).
bool rtcRead(int64_t& epoch);
bool rtcWrite(int64_t epoch);

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n);

}  // namespace amoled18
