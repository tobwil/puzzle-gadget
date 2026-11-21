// Brain Candy – Geräteseite für Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)
//
// Aufteilung:  Kern 1 = Spiel und Rastern (30 fps, loop()), Band für Band (16 Zeilen) in internen DMA-Puffern
//              Kern 0 = Display-Task (Bänder per QSPI schieben), Eingabe-Task (IMU ~500 Hz, Touch 100 Hz), Audio, Mikrofonpegel
//
// Serielle Befehle (115200 Baud, eine Zeile):
//   status         Spielstand, Neigung, Mikrofon
//   level N        Level N (1–36) starten
//   tipp X Y       Tippen an Display-Position X, Y
//   wisch X0 Y0 X1 Y1   Wischen
//   klopf N        N künstliche Schläge auf den Tisch
//   stufe N        Schwierigkeit 0 leicht, 1 normal, 2 schwer
//   laut N         Lautstärke 0 (stumm) bis 3
//   modus X        X = klein (16 Level, unter 5), kind (24 Level, 6–9) oder erwachsen (36 Level)
//   menu           Einstellungen öffnen
//   zufall         Zufallsmodus starten
//   taste          wie Power-Taste kurz: Display aus/an
//   sag KEY        Sprach-Clip abspielen, z. B. „sag de_t05“
//   reset          Spielstand löschen (Level, Tipps)
//   log            Zwei-Sekunden-Protokoll (fps, Zeiten) ein/aus
#include <Arduino.h>
#include <Preferences.h>

#include <algorithm>
#include <cmath>

#include "amoled18.h"
#include "fonts.h"
#include "game.h"
#include "voice_index.h"

using namespace dk;
namespace hw = amoled18;

// Lage der IMU auf der Platine → Bildschirmachsen (x rechts, y oben, z aus dem Bildschirm), wie im Kinderprojekt
#define IMU_X_SRC 0
#define IMU_X_SIGN 1
#define IMU_Y_SRC 1
#define IMU_Y_SIGN 1
#define IMU_Z_SRC 2
#define IMU_Z_SIGN 1

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

struct NvsStore : Store {
  Preferences prefs;
  void begin() { prefs.begin("braincandy", false); }
  int get(const char* k, int def) override { return prefs.getInt(k, def); }
  void set(const char* k, int v) override { prefs.putInt(k, v); }
};

static Synth synth;
static NvsStore store;

// Sprachausgabe: vorab erzeugte Clips (tools/mkvoice.py), eingebettet aus firmware/voice.bin
extern const uint8_t voiceStart[] asm("_binary_firmware_voice_bin_start");
struct ClipSpeaker : Speaker {
  void speak(const char* key) override {
    for (int i = 0; i < VOICE_N; i++)
      if (!strcmp(VOICE_CLIPS[i].key, key)) { synth.playClip(voiceStart + VOICE_CLIPS[i].off, VOICE_CLIPS[i].len); return; }
  }
};
static ClipSpeaker speaker;
static bc::Game* game;
static Scene scene;

// ---------------- Eingabe (Kern 0) ----------------
static portMUX_TYPE inMux = portMUX_INITIALIZER_UNLOCKED;
static TouchSample touchQ[32];
static int touchN = 0;
static TouchSample fakeTouch[24];
static int fakeTouchN = 0;
static AccelSample accQ[128];
static int accHead = 0, accCount = 0;
static volatile int touchPresses = 0;   // für „status“: wie oft der Finger aufgesetzt wurde
static volatile bool touchDown = false;
static volatile int buttonEvent = 0;   // hw::Button vom Eingabe-Task an loop()
static volatile int16_t touchX = 0, touchY = 0;

static void inputTask(void*) {
  bool wasDown = false;
  int16_t lx = 0, ly = 0;
  uint32_t lastTouch = 0;
  for (;;) {
    const uint32_t now = millis();
    const hw::Button be = hw::pollButtons();
    if (be != hw::Button::Idle) buttonEvent = int(be);
    float v[3];
    if (hw::readAccel(v)) {
      AccelSample s{IMU_X_SIGN * v[IMU_X_SRC], IMU_Y_SIGN * v[IMU_Y_SRC], IMU_Z_SIGN * v[IMU_Z_SRC], now};
      portENTER_CRITICAL(&inMux);
      accQ[(accHead + accCount) % 128] = s;
      if (accCount < 128) accCount++;
      else accHead = (accHead + 1) % 128;
      portEXIT_CRITICAL(&inMux);
    }
    if (now - lastTouch >= 10) {
      lastTouch = now;
      bool down = false;
      int16_t x = lx, y = ly;
      if (hw::readTouch(down, x, y) && (down || wasDown)) {
        if (!down) { x = lx; y = ly; }
        TouchSample s{down, x, y, now};
        portENTER_CRITICAL(&inMux);
        if (touchN < 32) touchQ[touchN++] = s;
        else touchQ[31] = s;
        portEXIT_CRITICAL(&inMux);
        if (down && !wasDown) touchPresses = touchPresses + 1;
        wasDown = down;
        lx = x; ly = y;
        touchDown = down; touchX = x; touchY = y;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ---------------- Display (Kern 0) ----------------
// Bänder sind so breit wie das Panel: Auf dem 2.16 liegt die Spielfläche mittig, der Rand bleibt weiß.
// Ein Fenster, das nicht bei Spalte 0 beginnt, zeigte dort einen gestrichelten Streifen.
static constexpr int BAND = 16, NBUF = 4, PW = hw::PANEL_W;
struct Band { uint16_t* buf; int16_t y; };
static QueueHandle_t freeQ, fullQ;
static volatile int brightness = 0, curBrightness = -1;
static volatile bool panelWant = true;   // false: Panel per Taste ausgeschaltet
static bool panelIs = true;

// Nur Bänder, die sich seit dem letzten Bild geändert haben, gehen zum Panel (jedes mit eigenem Fenster).
// Das Panel hat keinen TE-Ausgang; ein ständig neu geschriebenes Vollbild flackerte beim Auslesen.
static void displayTask(void*) {
  auto* gfx = hw::panel();
  for (;;) {
    Band b;
    xQueueReceive(fullQ, &b, portMAX_DELAY);
    if (panelIs != panelWant) { panelIs = panelWant; if (panelIs) gfx->displayOn(); else gfx->displayOff(); }
    if (curBrightness != brightness) { curBrightness = brightness; gfx->setBrightness(curBrightness); }
    if (!b.buf) continue;   // nur Helligkeit setzen
    gfx->startWrite();
    gfx->writeAddrWindow(0, hw::OFF_Y + b.y, PW, BAND);
    hw::bus()->writeBytes(reinterpret_cast<uint8_t*>(b.buf), PW * BAND * 2);
    gfx->endWrite();
    xQueueSend(freeQ, &b.buf, portMAX_DELAY);
  }
}

static uint32_t bandHash[H / BAND];   // 0 = unbekannt, Band wird auf jeden Fall gesendet
static uint32_t hashBand(const uint16_t* p, int n) {
  uint32_t h = 2166136261u;
  const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
  for (int i = 0; i < n / 2; i++) h = (h ^ w[i]) * 16777619u;
  return h | 1;
}

// ---------------- Audio und Mikrofonpegel (Kern 0) ----------------
// Der Verstärker (PA_EN) läuft nur, solange etwas klingt, und geht 2 s danach aus (spart im Ruhezustand Strom).
static void audioTask(void*) {
  static int16_t mono[256], stereo[512];
  bool ampOn = true;
  uint32_t quietAt = millis();
  for (;;) {
    if (synth.busy()) {
      quietAt = millis();
      if (!ampOn) { hw::amp(true); ampOn = true; }
    } else if (ampOn && millis() - quietAt > 2000) {
      hw::amp(false);
      ampOn = false;
    }
    synth.render(mono, 256);
    for (int i = 0; i < 256; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    hw::i2s().write(reinterpret_cast<uint8_t*>(stereo), sizeof stereo);
  }
}

// Pusten/Rufen: nur was nah am Gerät laut ist zählt. Der Pegel muss deutlich über dem Grundrauschen UND über
// einer festen Schwelle liegen; Gespräche im Raum (Büro: um −40 dBFS) lösen nichts aus.
static constexpr float MIC_ABS_DB = -30;   // darunter nie (Gerede in ein paar Metern Abstand)
static constexpr float MIC_FULL_DB = -12;  // so laut: voller Pegel (Pusten direkt aufs Mikrofon)
static volatile float voiceLevel = 0, micDb = -120, micPeak = -120;
static void micTask(void*) {   // nur der Pegel (Pusten, Rufen), nichts wird gespeichert
  static int16_t buf[512];
  float floorDb = 0, level = 0;
  int count = 0;
  for (;;) {
    const size_t n = hw::i2s().readBytes(reinterpret_cast<char*>(buf), sizeof buf) / 4;
    if (!n) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    double sum = 0;
    for (size_t i = 0; i < n; i++) { const float v = buf[2 * i]; sum += double(v) * v; }
    const float db = 10 * log10f(float(sum / n) + 1e-3f) - 90.3f;
    micDb = db;
    if (db > micPeak) micPeak = db;
    if (++count < 60) floorDb = count == 1 ? db : floorDb + 0.2f * (db - floorDb);
    else floorDb += (db < floorDb ? 0.2f : 0.002f) * (db - floorDb);
    const float start = std::max(MIC_ABS_DB, floorDb + 15);
    const float above = (db - start) / std::max(6.f, MIC_FULL_DB - start);
    const float target = above < 0 ? 0 : above > 1 ? 1 : above;
    level += (target - level) * (target > level ? 0.5f : 0.1f);
    voiceLevel = level;
  }
}

// ---------------- Serielle Befehle ----------------
static bool logOn = false;
static int fakeKnocks = 0;
static uint32_t fakeNext = 0;

static void pollSerial() {
  static char line[128];
  static int n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < int(sizeof line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    int a, b, c2, d;
    if (!strcmp(line, "status")) {
      Serial.println(game->debug());
      Serial.printf("mikrofon %.1f dBFS (spitze %.1f), pegel %.2f, imu %s, touch %s\n", float(micDb), float(micPeak), float(voiceLevel), hw::imuOk() ? "ok" : "fehlt", hw::touchOk() ? "ok" : "fehlt");
      Serial.printf("finger %s %d %d, aufgesetzt %d mal, display %s, pmu %s\n", touchDown ? "liegt" : "weg", int(touchX), int(touchY),
                    int(touchPresses), panelWant ? "an" : "aus (Taste)", hw::pmuOk() ? "ok" : "fehlt");
      const hw::Battery b = hw::battery();
      Serial.printf("akku %d %%, %d mV, %s%s, sparmodus %s, cpu %lu MHz\n", b.pct, b.mv, b.usb ? "usb" : "akku", b.charging ? ", lädt" : "",
                    game->saving() ? "an" : "aus", (unsigned long)getCpuFrequencyMhz());
    } else if (sscanf(line, "level %d", &a) == 1) {
      game->startLevel(a - 1);
      Serial.printf("level %d\n", a);
    } else if (sscanf(line, "tipp %d %d", &a, &b) == 2) {
      portENTER_CRITICAL(&inMux);
      if (touchN < 30) { touchQ[touchN++] = {true, int16_t(a), int16_t(b), millis()}; touchQ[touchN++] = {false, int16_t(a), int16_t(b), millis() + 80}; }
      portEXIT_CRITICAL(&inMux);
    } else if (sscanf(line, "wisch %d %d %d %d", &a, &b, &c2, &d) == 4 && fakeTouchN == 0) {
      const uint32_t t0 = millis();
      for (int k = 0; k <= 8; k++) fakeTouch[fakeTouchN++] = {true, int16_t(a + (c2 - a) * k / 8), int16_t(b + (d - b) * k / 8), t0 + 33u * k};
      fakeTouch[fakeTouchN++] = {false, int16_t(c2), int16_t(d), t0 + 33u * 9};
    } else if (!strncmp(line, "klopf", 5)) {
      fakeKnocks = atoi(line + 5) > 0 ? atoi(line + 5) : 1;
      fakeNext = millis();
    } else if (sscanf(line, "stufe %d", &a) == 1) {
      game->setDifficulty(a);
      Serial.printf("stufe %d (0 leicht, 1 normal, 2 schwer)\n", game->difficulty());
    } else if (sscanf(line, "laut %d", &a) == 1) {
      game->setVol(a);
      Serial.printf("laut %d (0 stumm … 3 laut)\n", game->volume());
    } else if (!strncmp(line, "modus ", 6)) {
      const char* m = line + 6;
      game->setMode(!strcmp(m, "kind") ? bc::Game::KID : !strcmp(m, "klein") ? bc::Game::SMALL : bc::Game::ADULT);
      game->skipWho();
      static const char* const N[3] = {"erwachsen", "kind", "klein"};
      Serial.printf("modus %s\n", N[game->mode()]);
    } else if (!strcmp(line, "mikro")) {   // Spitzenpegel seit dem letzten Aufruf (zum Einstellen der Schwellen)
      Serial.printf("mikrofon spitze %.1f dBFS, jetzt %.1f dBFS, pegel %.2f\n", float(micPeak), float(micDb), float(voiceLevel));
      micPeak = -120;
    } else if (!strcmp(line, "taste")) {   // wie Power-Taste kurz (Display aus/an)
      buttonEvent = int(hw::Button::ScreenToggle);
    } else if (!strcmp(line, "zufall")) {
      game->startRandom();
      Serial.printf("zufall: %s\n", game->levelAnswer() ? game->levelAnswer() : "-");
    } else if (!strcmp(line, "menu")) {
      game->openSettings();
    } else if (!strncmp(line, "sag ", 4)) {
      speaker.speak(line + 4);
    } else if (!strcmp(line, "reset")) {
      game->resetProgress();
      Serial.println("spielstand gelöscht");
    } else if (!strcmp(line, "log")) {
      logOn = !logOn;
      Serial.printf("protokoll %s\n", logOn ? "an" : "aus");
    }
  }
}

// ---------------- Start ----------------
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // liest niemand mit, darf print nicht blockieren
  if (!hw::begin()) Serial.println("Display-Init fehlgeschlagen");
  Serial.printf("IMU %s, Touch %s\n", hw::imuOk() ? "ok" : "fehlt", hw::touchOk() ? "ok" : "fehlt");
  const bool audio = hw::audioBegin(Synth::RATE, 75);
  Serial.printf("Audio %s\n", audio ? "ok" : "fehlt");
  store.begin();
  bc::seedRandom(esp_random());   // jedes Spiel anders gemischt
  game = new bc::Game(synth, store);
  game->speaker = &speaker;

  freeQ = xQueueCreate(NBUF, sizeof(uint16_t*));
  fullQ = xQueueCreate(NBUF + 1, sizeof(Band));
  for (int i = 0; i < NBUF; i++) {
    uint16_t* b = static_cast<uint16_t*>(heap_caps_malloc(PW * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!b) Serial.println("Zu wenig RAM für die Bänder");
    else for (int k = 0; k < PW * BAND; k++) b[k] = 0xFFFF;   // weißer Rand
    xQueueSend(freeQ, &b, 0);
  }
  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 4, nullptr, 0);
  if (audio) {
    xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 0);
  }
  Serial.println("Brain Candy bereit");
}

void loop() {
  static uint32_t next = millis(), lastLog = 0, frames = 0, tickUs = 0, rasterUs = 0;
  static bool blank = false;
  static Input in;

  pollSerial();
  // Tasten: Display aus/an (Spiel pausiert, Touch wird ignoriert) und Ausschalten
  static bool forcedOff = false;
  const int be = buttonEvent;
  // Akku: ans Spiel (Anzeige, Sparmodus, Warnung); bei 3 % ohne Kabel 20 s lang: geordnet ausschalten.
  // Zusätzlich unter 3,4 V: die Ladestandsmessung muss sich nach dem Einschalten erst einlernen und darf
  // nicht allein abschalten.
  const hw::Battery bat = hw::battery();
  game->setBattery(bat.pct, bat.charging, bat.usb);
  static uint32_t emptySince = 0;
  if (bat.pct >= 0 && bat.pct <= 3 && bat.mv > 0 && bat.mv < 3400 && !bat.usb) { if (!emptySince) emptySince = millis(); }
  else emptySince = 0;
  // Lange dunkel (Taste oder Zeitablauf) und nicht am Kabel: ganz ausschalten. Einschalten: Power-Taste.
  static uint32_t darkSince = 0;
  const bool dark = forcedOff || game->screenOff();
  if (!dark || bat.usb) darkSince = 0;
  else if (!darkSince) darkSince = millis();
  const uint32_t idleOffMs = (game->saving() ? 3 : 10) * 60000UL;
  const bool autoOff = hw::pmuOk() && ((emptySince && millis() - emptySince > 20000) || (darkSince && millis() - darkSince > idleOffMs));
  if (autoOff) Serial.println(emptySince ? "akku leer" : "lange dunkel");
  // Dunkel: Prozessor auf 80 MHz (Peripherie läuft mit 80 MHz APB unverändert weiter), hell: 240 MHz
  static bool slow = false;
  if (dark != slow) { slow = dark; setCpuFrequencyMhz(dark ? 80 : 240); }
  const bool menuOff = game->takePowerOff() || autoOff;   // Ausschalten aus dem Einstellungsmenü oder automatisch
  if (be || menuOff) {
    buttonEvent = 0;
    const int ev = menuOff ? int(hw::Button::PowerOff) : be;
    if (ev == int(hw::Button::ScreenToggle)) {
      forcedOff = !forcedOff;
      panelWant = !forcedOff;
      if (!forcedOff) { game->wake(millis()); memset(bandHash, 0, sizeof bandHash); }
      Serial.printf("display %s\n", forcedOff ? "aus" : "an");
    }
    if (ev == int(hw::Button::PowerOff)) {
      Serial.println("ausschalten");
      brightness = 0;
      panelWant = false;
      Band b{nullptr, 0};
      xQueueSend(fullQ, &b, portMAX_DELAY);
      delay(200);
      hw::powerOff();
    }
  }
  if (forcedOff) {
    portENTER_CRITICAL(&inMux);
    touchN = 0;
    accCount = 0;
    portEXIT_CRITICAL(&inMux);
    brightness = 0;
    Band b{nullptr, 0};
    xQueueSend(fullQ, &b, portMAX_DELAY);
    delay(50);
    return;
  }
  const uint32_t t0 = micros();
  {   // fällige künstliche Berührungen einreihen
    const uint32_t now = millis();
    int k = 0;
    portENTER_CRITICAL(&inMux);
    while (k < fakeTouchN && fakeTouch[k].ms <= now && touchN < 32) touchQ[touchN++] = fakeTouch[k++];
    portEXIT_CRITICAL(&inMux);
    if (k) { memmove(fakeTouch, fakeTouch + k, sizeof(TouchSample) * (fakeTouchN - k)); fakeTouchN -= k; }
  }
  portENTER_CRITICAL(&inMux);
  in.nTouch = touchN;
  memcpy(in.touch, touchQ, sizeof(TouchSample) * touchN);
  touchN = 0;
  while (accCount > 64) { accHead = (accHead + 1) % 128; accCount--; }
  in.nAcc = accCount;
  for (int i = 0; i < accCount; i++) in.acc[i] = accQ[(accHead + i) % 128];
  accHead = (accHead + accCount) % 128;
  accCount = 0;
  portEXIT_CRITICAL(&inMux);
  // künstliche Schläge: kurz abklingendes Zittern wie auf der Tischplatte
  static uint32_t fakeStart = 0;
  if (fakeKnocks > 0 && millis() >= fakeNext) { fakeStart = millis(); fakeNext += 250; fakeKnocks--; }
  if (fakeStart)
    for (int i = 0; i < in.nAcc; i++) {
      const float u = (in.acc[i].ms - fakeStart) * 0.001f;
      if (in.acc[i].ms >= fakeStart && u < 0.1f) in.acc[i].z += 0.4f * std::exp(-u / 0.012f) * std::sin(6.2831853f * 90 * u + 0.6f);
    }
  in.boot = hw::bootPressed();
  in.voice = synth.clipPlaying() ? 0 : voiceLevel;   // die eigene Stimme soll keine Mikrofon-Level lösen
  in.ms = millis();

  game->tick(in);
  game->draw(scene);
  const uint32_t t1 = micros();
  tickUs += t1 - t0;

  brightness = game->brightness();
  if (game->screenOff()) {
    if (!blank) { scene.reset(); scene.bg = {0, 0, 0}; blank = true; memset(bandHash, 0, sizeof bandHash); }
    else {
      Band b{nullptr, 0};
      xQueueSend(fullQ, &b, portMAX_DELAY);
      goto pace;
    }
  } else blank = false;

  for (int y = 0; y < H; y += BAND) {
    uint16_t* buf;
    xQueueReceive(freeQ, &buf, portMAX_DELAY);
    uint32_t h;
    if (hw::OFF_X == 0) { scene.rasterize(buf, y, BAND, true); h = hashBand(buf, W * BAND); }   // Panel erwartet Big Endian
    else {
      static uint16_t narrow[W * BAND];
      scene.rasterize(narrow, y, BAND, true);
      h = hashBand(narrow, W * BAND);
      if (h != bandHash[y / BAND]) for (int r = 0; r < BAND; r++) memcpy(buf + r * PW + hw::OFF_X, narrow + r * W, W * 2);
    }
    if (h == bandHash[y / BAND]) { xQueueSend(freeQ, &buf, portMAX_DELAY); continue; }   // unverändert: nicht senden
    bandHash[y / BAND] = h;
    Band b{buf, int16_t(y)};
    xQueueSend(fullQ, &b, portMAX_DELAY);
  }
  {   // Helligkeit auch ohne geänderte Bänder nachführen
    Band b{nullptr, 0};
    xQueueSend(fullQ, &b, portMAX_DELAY);
  }
  rasterUs += micros() - t1;
  frames++;

pace:
  if (logOn && millis() - lastLog > 2000) {
    const float sec = (millis() - lastLog) / 1000.f;
    Serial.printf("fps %.1f  spiel %lu us  raster %lu us  formen %d  heap %u\n", frames / sec, (unsigned long)(frames ? tickUs / frames : 0),
                  (unsigned long)(frames ? rasterUs / frames : 0), scene.size(), ESP.getFreeHeap());
    Serial.println(game->debug());
    lastLog = millis(); frames = 0; tickUs = 0; rasterUs = 0;
  } else if (!logOn && millis() - lastLog > 2000) {
    lastLog = millis(); frames = 0; tickUs = 0; rasterUs = 0;
  }
  next += game->screenOff() ? 100 : 33;   // dunkel: 10 Durchläufe je Sekunde reichen (Touch weckt)
  const int32_t wait = int32_t(next - millis());
  if (wait > 0) delay(wait);
  else next = millis();
}
