#include "amoled18.h"

#include <SD_MMC.h>
#include <Wire.h>

#include <ctime>

#include "es8311.h"
#ifdef BOARD_AMOLED216
#include "TouchDrvCSTXXX.hpp"
#include "es7210.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#endif

namespace amoled18 {

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_CS 12
#ifdef BOARD_AMOLED216
#define LCD_SCLK 38
#define LCD_RST 39
#define TP_INT 11
#define TP_RST 40
#define I2S_MCK 42
#define KEY3 18   // Taste an GPIO18 (gegen GND, 10k Pull-up)
#else
#define LCD_SCLK 11
#define LCD_RST GFX_NOT_DEFINED   // Reset über den IO-Expander
#define I2S_MCK 16
#endif
#define I2S_BCK 9
#define I2S_DI 10
#define I2S_WS 45
#define I2S_DO 8

static Arduino_DataBus* bus_ = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
#ifdef BOARD_AMOLED216
static Arduino_CO5300* panel_ = new Arduino_CO5300(bus_, LCD_RST, 0, PANEL_W, PANEL_H, 0, 0, 0, 0);
static TouchDrvCST92xx touch_;
static bool touchOk_ = false;
static XPowersPMU pmu_;
static bool pmuOk_ = false;
static Battery bat_;
static portMUX_TYPE batMux_ = portMUX_INITIALIZER_UNLOCKED;
#else
static Arduino_CO5300* panel_ = new Arduino_CO5300(bus_, LCD_RST, 0, LCD_W, LCD_H, 16, 0, 0, 0);   // Spaltenversatz 16
#endif
static I2SClass i2s_;
static bool imuOk_ = false;

Arduino_DataBus* bus() { return bus_; }
Arduino_CO5300* panel() { return panel_; }
I2SClass& i2s() { return i2s_; }
bool imuOk() { return imuOk_; }

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool begin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

#ifdef BOARD_AMOLED216
  bool ok = panel_->begin(80000000);
  bus_->beginWrite();
  bus_->writeC8D8(0x36, 0xA0);   // Ausrichtung wie in den Waveshare-Beispielen
  bus_->endWrite();
  panel_->setBrightness(0);
  panel_->fillScreen(0xFFFF);    // Rand um die Spielfläche: weiß wie der Spielhintergrund

  // Touch CST9220: Achsen wie im Waveshare-Beispiel (zum gedrehten Bild passend)
  // PMU: Power-Taste kurz/lang als Ereignis; nach 6 s Halten schaltet der Chip notfalls selbst ab
  pmuOk_ = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_SDA, PIN_SCL);
  if (pmuOk_) {
    pmu_.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu_.clearIrqStatus();
    pmu_.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
    pmu_.setLongPressPowerOFF();
    pmu_.enableLongPressShutdown();
    pmu_.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
    // Akku: Erkennung, Spannungsmessung und Ladestandsmessung (Fuel Gauge) an
    pmu_.enableBattDetection();
    pmu_.enableBattVoltageMeasure();
    pmu_.enableVbusVoltageMeasure();
    pmu_.enableGauge();
  }
  pinMode(KEY3, INPUT_PULLUP);

  touch_.setPins(TP_RST, TP_INT);
  touchOk_ = touch_.begin(Wire, CST92XX_SLAVE_ADDRESS);
  if (touchOk_) {
    touch_.setMaxCoordinates(PANEL_W, PANEL_H);
    touch_.setSwapXY(true);
    touch_.setMirrorXY(true, false);
  }
#else
  // IO-Expander: Display, Touch und Co. aus dem Reset holen
  i2cWriteReg(0x20, 0x03, 0xF8);   // P0..P2 als Ausgang
  i2cWriteReg(0x20, 0x01, 0x00);
  delay(20);
  i2cWriteReg(0x20, 0x01, 0x07);
  delay(60);

  bool ok = panel_->begin(80000000);
  panel_->fillScreen(0x0000);
  panel_->setBrightness(0);

  i2cWriteReg(0x15, 0xFE, 0x01);   // Touch: nicht automatisch schlafen
#endif

  // IMU: Softreset (sonst liefert sie alte Werte), dann ±8 g, 1 kHz, Tiefpass 13,37 % ODR
  uint8_t who = 0;
  i2cWriteReg(0x6B, 0x60, 0xB0);
  delay(20);
  if (i2cRead(0x6B, 0x00, &who, 1) && who == 0x05) {
    i2cWriteReg(0x6B, 0x02, 0x40);   // CTRL1: Adressen automatisch weiterzählen, Little Endian
    i2cWriteReg(0x6B, 0x03, 0x23);   // CTRL2: ±8 g, 1000 Hz
    i2cWriteReg(0x6B, 0x06, 0x07);   // CTRL5: Beschleunigungs-Tiefpass an, Modus 3
    i2cWriteReg(0x6B, 0x08, 0x01);   // CTRL7: Beschleunigung an
    delay(30);
    imuOk_ = true;
  }
  return ok;
}

#ifdef BOARD_AMOLED216
bool pmuOk() { return pmuOk_; }
void powerOff() { if (pmuOk_) pmu_.shutdown(); }

Button pollButtons() {
  Button ev = Button::Idle;
  // GPIO18: entprellt, Ereignis beim Drücken
  static bool stable = false, raw = false;
  static uint32_t rawSince = 0;
  const bool now = digitalRead(KEY3) == LOW;
  if (now != raw) { raw = now; rawSince = millis(); }
  if (raw != stable && millis() - rawSince > 30) {
    stable = raw;
    if (stable) ev = Button::ScreenToggle;
  }
  // Power-Taste: Interrupt-Status des PMU abfragen
  static uint32_t lastPmu = 0;
  if (pmuOk_ && millis() - lastPmu >= 100) {
    lastPmu = millis();
    pmu_.getIrqStatus();
    if (pmu_.isPekeyLongPressIrq()) ev = Button::PowerOff;
    else if (pmu_.isPekeyShortPressIrq()) ev = Button::ScreenToggle;
    pmu_.clearIrqStatus();
  }
  // Akku alle 2 s (im selben Task, damit sich keine I²C-Zugriffe überschneiden)
  static uint32_t lastBat = 0;
  if (pmuOk_ && (!lastBat || millis() - lastBat >= 2000)) {
    lastBat = millis();
    Battery b;
    b.usb = pmu_.isVbusIn();
    b.charging = pmu_.isCharging();
    if (pmu_.isBatteryConnect()) {
      const int p = pmu_.getBatteryPercent();
      b.pct = p >= 0 && p <= 100 ? p : -1;
      b.mv = pmu_.getBattVoltage();
    }
    // Beim Laden meldet der AXP2101 den Akku manchmal kurz als fehlend: letzten Wert 30 s halten
    static uint32_t validAt = 0;
    if (b.pct >= 0) validAt = millis();
    else if (millis() - validAt < 30000) { b.pct = bat_.pct; b.mv = bat_.mv; }
    portENTER_CRITICAL(&batMux_);
    bat_ = b;
    portEXIT_CRITICAL(&batMux_);
  }
  return ev;
}

Battery battery() {
  portENTER_CRITICAL(&batMux_);
  const Battery b = bat_;
  portEXIT_CRITICAL(&batMux_);
  return b;
}

bool touchOk() { return touchOk_; }

bool readTouch(bool& down, int16_t& x, int16_t& y) {
  // CST9220-Protokoll wie in SensorLib (TouchDrvCST92xx::getPoint), aber mit drei Ergebnissen:
  // kein neuer Datensatz (ACK-Byte fehlt) -> false, Zustand bleibt; losgelassen; Finger liegt.
  // getPoint() meldet „kein neuer Datensatz“ genauso wie „losgelassen“ – ein still liegender Finger
  // wurde dadurch ständig losgelassen und neu aufgesetzt (Ei-Level, Halten, Ziehen).
  if (!touchOk_) return false;
  uint8_t b[15] = {};
  Wire.beginTransmission(CST92XX_SLAVE_ADDRESS);
  Wire.write(0xD0);
  Wire.write(0x00);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(uint8_t(CST92XX_SLAVE_ADDRESS), uint8_t(sizeof b)) != sizeof b) return false;
  for (auto& v : b) v = Wire.read();
  Wire.beginTransmission(CST92XX_SLAVE_ADDRESS);   // Datensatz quittieren
  Wire.write(0xD0);
  Wire.write(0x00);
  Wire.write(0xAB);
  Wire.endTransmission(true);
  if (b[6] != 0xAB) return false;   // nichts Neues
  const uint8_t n = b[5] & 0x7F;
  down = n > 0 && n <= 2 && (b[0] & 0x0F) == 0x06;
  if (down) {
    const int16_t rx = int16_t((b[1] << 4) | (b[3] >> 4)), ry = int16_t((b[2] << 4) | (b[3] & 0x0F));
    int16_t px = ry, py = rx;   // Achsen tauschen, dann x spiegeln (wie setSwapXY(true), setMirrorXY(true, false))
    px = int16_t(PANEL_W - px);
    x = px - OFF_X;
    y = py - OFF_Y;
    if (x < 0) x = 0; else if (x >= LCD_W) x = LCD_W - 1;
    if (y < 0) y = 0; else if (y >= LCD_H) y = LCD_H - 1;
  }
  return true;
}
#else
bool pmuOk() { return false; }
void powerOff() {}
Button pollButtons() { return Button::Idle; }
Battery battery() { return {}; }

bool touchOk() { return true; }

bool readTouch(bool& down, int16_t& x, int16_t& y) {
  uint8_t t[5];
  if (!i2cRead(0x15, 0x02, t, 5)) return false;
  down = (t[0] & 0x0F) > 0;
  if (down) {
    x = ((t[1] & 0x0F) << 8) | t[2];
    y = ((t[3] & 0x0F) << 8) | t[4];
  }
  return true;
}
#endif

bool readAccel(float v[3]) {
  uint8_t d[6];
  if (!imuOk_ || !i2cRead(0x6B, 0x35, d, 6)) return false;
  for (int i = 0; i < 3; i++) v[i] = int16_t(d[2 * i] | (d[2 * i + 1] << 8)) / 4096.f;   // ±8 g
  return true;
}

void amp(bool on) { digitalWrite(PIN_PA_EN, on ? HIGH : LOW); }

bool audioBegin(uint32_t rate, int volume) {
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, HIGH);
  i2s_.setPins(I2S_BCK, I2S_WS, I2S_DO, I2S_DI, I2S_MCK);
  if (!i2s_.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {false, false, true, int(rate * 256), int(rate)};
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_voice_volume_set(es, volume, nullptr);
#ifdef BOARD_AMOLED216
  // Mikrofone hängen am ES7210 (Slave, Standard-I2S, 16 Bit); MIC1/MIC2 landen links/rechts auf I2S_DI
  audio_hal_codec_config_t cfg = {};
  cfg.adc_input = AUDIO_HAL_ADC_INPUT_ALL;
  cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
  cfg.i2s_iface.mode = AUDIO_HAL_MODE_SLAVE;
  cfg.i2s_iface.fmt = AUDIO_HAL_I2S_NORMAL;
  cfg.i2s_iface.samples = AUDIO_HAL_16K_SAMPLES;   // passt zu Synth::RATE
  cfg.i2s_iface.bits = AUDIO_HAL_BIT_LENGTH_16BITS;
  if (rate != 16000) return false;
  esp_err_t r = es7210_adc_init(&Wire, &cfg);
  r |= es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
  r |= es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2), GAIN_24DB);
  r |= es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);
  if (r != ESP_OK) return false;
#else
  es8311_microphone_config(es, false);              // analoges Mikrofon
  es8311_microphone_gain_set(es, ES8311_MIC_GAIN_24DB);
#endif
  return true;
}

// ---------------- TF-Karte ----------------

bool sdBegin() {
#ifdef BOARD_AMOLED216
  return false;   // TF-Karte läuft dort im SPI-Modus (CS 41); bisher nicht eingebunden
#endif
  SD_MMC.setPins(2, 1, 3);
  if (!SD_MMC.begin("/sd", true, false, SDMMC_FREQ_DEFAULT, 5)) return false;
  return SD_MMC.cardType() != CARD_NONE;
}

// ---------------- Echtzeituhr PCF85063 ----------------

static constexpr uint8_t RTC_ADDR = 0x51;
static inline uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static inline uint8_t bin2bcd(int v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

bool rtcRead(int64_t& epoch) {
  uint8_t r[7];
  if (!i2cRead(RTC_ADDR, 0x04, r, 7)) return false;
  if (r[0] & 0x80) return false;   // Oszillator stand still: Zeit ungültig
  tm t{};
  t.tm_sec = bcd2bin(r[0] & 0x7F);
  t.tm_min = bcd2bin(r[1] & 0x7F);
  t.tm_hour = bcd2bin(r[2] & 0x3F);
  t.tm_mday = bcd2bin(r[3] & 0x3F);
  t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
  t.tm_year = bcd2bin(r[6]) + 100;
  if (t.tm_year < 124) return false;   // vor 2024: nie gestellt
  epoch = int64_t(mktime(&t));        // ohne gesetzte Zeitzone rechnet mktime in UTC, die Uhr hält Ortszeit
  return true;
}

bool rtcWrite(int64_t epoch) {
  const time_t tt = time_t(epoch);
  tm t{};
  gmtime_r(&tt, &t);
  const uint8_t v[7] = {bin2bcd(t.tm_sec), bin2bcd(t.tm_min), bin2bcd(t.tm_hour), bin2bcd(t.tm_mday), uint8_t(t.tm_wday),
                        bin2bcd(t.tm_mon + 1), bin2bcd(t.tm_year - 100)};
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(v, 7);
  return Wire.endTransmission() == 0;
}

}  // namespace amoled18
