// Simulator für Brain Candy: dasselbe Spiel wie auf dem Gerät.
//
//   bcsim --tour <ordner>     spielt jedes Level auf seinem Lösungsweg durch, prüft, dass es gelöst wird,
//                             speichert Bilder (PPM) und meldet Text außerhalb des Displays oder in den runden Ecken
//   bcsim                     Fenster (nur mit SDL2 gebaut: make -C sim sdl)
//                             Maus = Finger, X (halten) = schütteln, M (halten) = pusten/rufen, K = auf den Tisch klopfen,
//                             U (halten) = Gerät umdrehen, P = Bildschirmfoto, Esc = Ende
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game.h"

#ifdef WITH_SDL
#include <SDL.h>
#endif

using namespace dk;

struct MemStore : Store {
  std::map<std::string, int> kv;
  int get(const char* k, int def) override { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
  void set(const char* k, int v) override { kv[k] = v; }
};

static uint16_t fb[W * H];
static Scene scene;

static void renderAll() { for (int y = 0; y < H; y += 16) scene.rasterize(fb + y * W, y, 16); }

static void savePpm(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    const uint16_t p = fb[i];
    const uint8_t c[3] = {uint8_t(((p >> 11) & 31) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63), uint8_t((p & 31) * 255 / 31)};
    fwrite(c, 1, 3, f);
  }
  fclose(f);
}

// Nachgebildeter Beschleunigungssensor: Gerät liegt flach (z = 1 g) oder steht kopfüber (y = -1 g)
struct FakeImu {
  bool shaking = false, upside = false;
  float tiltX = 0, tiltY = 0;
  std::vector<uint32_t> knocks;
  uint32_t last = 0;
  void fill(Input& in, uint32_t ms) {
    in.nAcc = 0;
    if (!last) last = ms - 33;
    for (uint32_t t = last + 2; t <= ms && in.nAcc < 64; t += 2) {
      float x = tiltX, y = upside ? -1.f : tiltY, z = upside ? 0.f : 1.f;
      for (uint32_t k : knocks) {
        if (t < k) continue;
        const float u = (t - k) * 0.001f;
        if (u < 0.12f) { const float v = 0.5f * std::exp(-u / 0.012f) * std::sin(6.2831853f * 90 * u + 0.6f); z += v; x += 0.3f * v; }
      }
      if (shaking) { x += 1.8f * std::sin(t * 0.0314f); y += 0.8f * std::sin(t * 0.025f + 1); z += 0.4f * std::sin(t * 0.04f); }
      in.acc[in.nAcc++] = {x, y, z, t};
    }
    last = ms;
  }
};

struct Sim {
  Synth synth;
  MemStore store;
  bc::Game game{synth, store};
  FakeImu imu;
  uint32_t ms = 1000;
  float voice = 0;
  std::string dir;
  int shots = 0;

  void frame(const std::vector<TouchSample>& touch = {}) {
    Input in;
    in.ms = ms;
    for (auto& t : touch) if (in.nTouch < 32) in.touch[in.nTouch++] = t;
    imu.fill(in, ms);
    in.voice = voice;
    game.tick(in);
    game.draw(scene);
    ms += 33;
  }
  void wait(float s) { for (int i = 0; i < int(s * 30); i++) frame(); }
  void tap(float x, float y) {
    frame({{true, int16_t(x), int16_t(y), ms}});
    frame({{false, int16_t(x), int16_t(y), ms}});
    wait(0.2f);
  }
  void drag(float x0, float y0, float x1, float y1, float secs = 0.6f) {
    const int n = int(secs * 30);
    frame({{true, int16_t(x0), int16_t(y0), ms}});
    for (int i = 1; i <= n; i++) frame({{true, int16_t(x0 + (x1 - x0) * i / n), int16_t(y0 + (y1 - y0) * i / n), ms}});
    frame({{false, int16_t(x1), int16_t(y1), ms}});
    wait(0.2f);
  }
  void hold(float x, float y, float secs) {
    for (int i = 0; i < int(secs * 30); i++) frame({{true, int16_t(x), int16_t(y), ms}});
    frame({{false, int16_t(x), int16_t(y), ms}});
  }
  void circle(float cx, float cy, float r, float secs) {
    const int n = int(secs * 30);
    for (int i = 0; i <= n; i++) {
      const float a = 6.2831853f * i / n;
      frame({{true, int16_t(cx + std::sin(a) * r), int16_t(cy - std::cos(a) * r), ms}});
    }
    frame({{false, int16_t(cx), int16_t(cy - r), ms}});
  }
  // Antwort auf der Zahlentastatur eintippen und mit OK bestätigen
  void type(const char* digits) {
    for (const char* c = digits; *c; c++) { const int k = *c == '0' ? 9 : *c - '1'; tap(bc::npKeyX(k), bc::npKeyY(k)); }
    tap(bc::npKeyX(11), bc::npKeyY(11));
  }
  void typeAnswer() { type(game.levelAnswer()); }
  int ans0() { return game.levelAnswer()[0] - '0'; }
  void slot3() { tap(72 + 112 * ans0(), 226); }                     // FindAnimal: Platz 0–2
  void cupTap() { tap(84 + 100 * ans0(), 250); }                    // Hütchenspiel
  void side() { tap(ans0() ? 268 : 100, 210); }                     // MoreLess: linke oder rechte Gruppe
  void popBalloons() {                                              // Balloons: Plätze laut Lösung antippen
    static const float P[6][2] = {{72, 186}, {154, 158}, {234, 190}, {306, 162}, {114, 274}, {262, 276}};
    for (const char* c = game.levelAnswer(); *c; c++) tap(P[*c - '0'][0], P[*c - '0'][1]);
  }
  void colorTap() { static const float P[4][2] = {{110, 160}, {258, 160}, {110, 272}, {258, 272}}; tap(P[ans0()][0], P[ans0()][1]); }
  void shapeTap() { tap(72 + 112 * ans0(), 220); }
  void sideTap() { tap(ans0() ? 262 : 110, 270); }
  void oddTap() { static const float P[4][2] = {{110, 170}, {258, 170}, {110, 282}, {258, 282}}; tap(P[ans0()][0], P[ans0()][1]); }
  void rub() { for (int i = 0; i < 4; i++) { drag(90, 240, 280, 240, 0.25f); drag(280, 250, 90, 250, 0.25f); } }
  void shot(const char* name) {
    renderAll();
    char p[256];
    std::snprintf(p, sizeof p, "%s/%02d_%s.ppm", dir.c_str(), shots++, name);
    savePpm(p);
  }
};

// Text-Prüfung: Rand und runde Ecken (Radius ~46 px)
static int nIssues = 0;
static void checkText(const char* s, float x0, float y0, float x1, float y1) {
  const float CR = 46;
  const float yt = y0 + 4, yb = y1 - 6;   // Zeilenhöhe hat Luft oben und unten
  char why[64] = "";
  if (x0 < 4 || x1 > W - 4 || yt < 2 || yb > H - 2) std::snprintf(why, sizeof why, "über den Rand");
  const float cx[2] = {CR, W - CR}, cy[2] = {CR, H - CR};
  for (float px : {x0, x1})
    for (float py : {yt, yb}) {
      const int i = px < CR ? 0 : px > W - CR ? 1 : -1, j = py < CR ? 0 : py > H - CR ? 1 : -1;
      if (i >= 0 && j >= 0 && (px - cx[i]) * (px - cx[i]) + (py - cy[j]) * (py - cy[j]) > CR * CR) std::snprintf(why, sizeof why, "in der runden Ecke");
    }
  if (why[0]) { printf("  TEXT %s: \"%s\" (%.0f,%.0f)-(%.0f,%.0f)\n", why, s, x0, y0, x1, y1); nIssues++; }
}

static int tour(const char* dir) {
  Scene::textHook = checkText;
  int fails = 0;
  for (int lang = 0; lang < 2; lang++) {
    Sim sim;
    sim.dir = dir;
    sim.shots = lang * 100;
    sim.store.set("bc_lang", lang);
    sim.game.setLang(lang);
    printf("== Sprache %s ==\n", lang ? "de" : "en");
    sim.wait(1);
    sim.shot("wer_spielt");
    sim.tap(299, 230);   // Erwachsen
    sim.wait(0.3f);
    for (int p = 0; p < 3; p++) {   // Intro beim ersten Start: drei Karten
      sim.shot(("intro" + std::to_string(p + 1)).c_str());
      sim.tap(184, 408);
      sim.wait(0.3f);
    }
    sim.shot("titel");
    sim.tap(296, 384);
    sim.wait(0.3f);
    sim.shot("levels");
    sim.tap(184, 408);   // Seite 13–24
    sim.wait(0.2f);
    sim.shot("levels2");
    sim.tap(36, 40);
    sim.tap(142, 384);   // Spielen: erst die Aufgabenkarte
    sim.wait(0.6f);
    sim.shot("aufgabenkarte");
    sim.tap(184, 358);
    using Fn = void (*)(Sim&);
    static const Fn SOLVE[bc::LEVELS] = {
        [](Sim& s) { s.drag(84, 318, 228, 236, 0.8f); s.drag(30, 318, 150, 318, 0.1f); s.drag(84, 318, 228, 236, 0.8f); },   // eingepackt (falsch), auspacken, füttern
        [](Sim& s) { s.tap(124, 296); s.wait(0.5f); s.tap(304, 208); },                   // erst die Maus (falsch), dann der Elefant
        [](Sim& s) { s.type("5"); s.drag(276, 186, 160, 140, 0.6f); s.wait(4.2f); s.type("6"); },   // 5 (falsch, Sperre), Becher weg, 6
        [](Sim& s) { s.tap(274, 260); s.drag(104, 282, 274, 250, 0.8f); s.tap(274, 260); },   // Tür auf, rein, zu
        [](Sim& s) { s.tap(184, 236); s.wait(0.3f); s.hold(184, 236, 7.8f); },            // Tippen reicht nicht, lange halten
        [](Sim& s) { s.drag(62, 146, 236, 200, 0.8f); s.wait(1.5f); },                    // Sonne zum Dino
        [](Sim& s) { s.circle(184, 232, 86, 1.6f); },                                     // Ring nachfahren
        [](Sim& s) { s.tap(184, 230); s.imu.shaking = true; s.wait(1.2f); s.imu.shaking = false; },
        [](Sim& s) { s.drag(184, 82, 184, 150, 0.6f); },                                  // Aufgabentext wegziehen
        [](Sim& s) { s.tap(184, 172); s.voice = 0.8f; s.wait(1.4f); s.voice = 0; },       // pusten
        [](Sim& s) { for (int i = 0; i < 2; i++) { s.imu.knocks.push_back(s.ms + 30); s.wait(0.3f); } },   // klopfen
        [](Sim& s) { s.tap(184, 160); s.imu.upside = true; s.wait(1.0f); s.imu.upside = false; },          // umdrehen
        [](Sim& s) { s.type("10"); s.wait(4.2f); s.typeAnswer(); },                       // Ball: 10 (Falle), dann 5
        [](Sim& s) { s.typeAnswer(); },                                                   // Maschinen
        [](Sim& s) { s.typeAnswer(); },                                                   // Seerosen
        [](Sim& s) {                                                                      // Stroop-Sprint: 10 richtige
          for (int i = 0; i < 10; i++) { const int k = s.game.levelAnswer()[0] - '0'; s.tap(k % 2 ? 266 : 102, k / 2 ? 321 : 273); }
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Socken
        [](Sim& s) { s.wait(2.6f); s.typeAnswer(); },                                     // 7 Ziffern merken
        [](Sim& s) { s.typeAnswer(); },                                                   // Durchschnittstempo
        [](Sim& s) { s.tap(102, 261); s.wait(4.2f); s.tap(184, 311); },                   // Kisten: ÄPFEL (falsch), GEMISCHT
        [](Sim& s) { s.typeAnswer(); },                                                   // Alter
        [](Sim& s) { s.wait(8.2f); s.typeAnswer(); },                                     // Kettenrechnung
        [](Sim& s) {                                                                      // Labyrinth: rechts, runter, links oben an der Wand
          s.imu.tiltX = -0.5f; s.wait(2.0f);
          s.imu.tiltX = 0; s.imu.tiltY = 0.5f; s.wait(1.6f);
          s.imu.tiltX = 0.5f; s.imu.tiltY = -0.25f; s.wait(2.6f);
          s.imu.tiltX = s.imu.tiltY = 0;
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Ritter
        [](Sim& s) { s.typeAnswer(); },                                                   // Gurken
        [](Sim& s) { s.typeAnswer(); },                                                   // Wochentag
        [](Sim& s) {                                                                      // Farbfolge
          s.wait(5.4f);
          for (const char* c = s.game.levelAnswer(); *c; c++) { const int k = *c - '0'; s.tap(k % 2 ? 238 : 134, k / 2 ? 256 : 170); }
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Schnecke
        [](Sim& s) { s.typeAnswer(); },                                                   // magisches Quadrat
        [](Sim& s) { s.typeAnswer(); },                                                   // Ziffer 1
        [](Sim& s) { s.typeAnswer(); },                                                   // Monty Hall
        [](Sim& s) { s.typeAnswer(); },                                                   // Schachbrett
        [](Sim& s) { s.typeAnswer(); },                                                   // Seil
        [](Sim& s) { s.typeAnswer(); },                                                   // Uhrzeiger
        [](Sim& s) { s.typeAnswer(); },                                                   // Buchstaben
        [](Sim& s) { s.typeAnswer(); },                                                   // Look-and-say
    };
    for (int i = 0; i < bc::LEVELS; i++) {
      sim.game.startLevel(i);
      sim.wait(0.6f);
      sim.shot(("lvl" + std::to_string(i + 1) + "_start").c_str());
      SOLVE[i](sim);
      sim.wait(0.15f);
      sim.shot(("lvl" + std::to_string(i + 1) + "_aktion").c_str());
      sim.wait(1.6f);
      const bool ok = (sim.game.doneMask() >> i) & 1;
      printf("Level %2d: %s\n", i + 1, ok ? "gelöst" : "NICHT GELÖST");
      if (!ok) fails++;
      if (i == 0) sim.shot("geloest");
    }
    // Tipp und Überspringen
    sim.game.startLevel(6);
    sim.wait(0.3f);
    sim.tap(W - 30 - 25, 381);
    sim.wait(0.4f);
    sim.shot("tipp");
    sim.tap(184, 340);
    sim.tap(W - 36, 40);
    sim.wait(0.4f);
    sim.shot("ueberspringen");
    sim.tap(W - 108, 344);
    // Ende
    sim.game.setDifficulty(0);   // Leicht: Karte mit Werkzeugen
    sim.game.enterLevel(7);
    sim.wait(0.6f);
    sim.shot("leicht_karte");
    sim.game.setDifficulty(2);   // Schwer: Stoppuhr
    sim.game.startLevel(2);
    sim.wait(2.0f);
    sim.shot("schwer_lvl3");
    sim.drag(276, 186, 160, 140, 0.6f); sim.type("6");
    sim.wait(2.0f);
    sim.shot("schwer_geloest");
    sim.tap(184, 358);
    sim.game.enterLevel(2);
    sim.wait(0.6f);
    sim.shot("schwer_karte");
    sim.game.setDifficulty(1);
    sim.game.startLevel(bc::LEVELS - 1);
    sim.wait(0.3f);
    sim.typeAnswer();
    sim.wait(3.0f);
    sim.tap(184, 360);
    sim.wait(0.6f);
    sim.shot("ende");

    // ---- Kinder ----
    sim.tap(184, 300);
    sim.tap(90, 259);    // Titelbild: Knopf „Erwachsen“ führt zur Auswahl
    sim.wait(0.3f);
    sim.shot("wer_spielt_wieder");
    sim.tap(184, 230);   // Kind
    sim.wait(0.3f);
    sim.shot("kind_titel");
    if (sim.game.mode() != bc::Game::KID || sim.game.difficulty() != 0) { printf("Kinder-Modus nicht gewählt\n"); fails++; }
    sim.tap(296, 384);
    sim.wait(0.3f);
    sim.shot("kind_levels");
    sim.tap(36, 40);
    static const Fn KSOLVE[bc::KID_LEVELS - 12] = {
        [](Sim& s) { s.type("2"); s.wait(2.3f); s.typeAnswer(); },                        // Schafe: 2 (Falle), dann 3
        [](Sim& s) { s.tap(102, 261); s.wait(2.3f); s.tap(184, 311); },                   // Federn (falsch), GLEICH
        [](Sim& s) { s.wait(3.6f); s.typeAnswer(); },                                     // 4 Ziffern merken
        [](Sim& s) { s.typeAnswer(); },                                                   // Beine
        [](Sim& s) { s.type("1"); s.wait(2.3f); s.typeAnswer(); },                        // Wettrennen
        [](Sim& s) { s.typeAnswer(); },                                                   // Zahlenreihe
        [](Sim& s) {                                                                      // Farbfolge (4)
          s.wait(4.5f);
          for (const char* c = s.game.levelAnswer(); *c; c++) { const int k = *c - '0'; s.tap(k % 2 ? 238 : 134, k / 2 ? 256 : 170); }
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Eier
        [](Sim& s) { s.tap(102, 261); },                                                  // Handtuch
        [](Sim& s) {                                                                      // Labyrinth mit zwei Löchern
          s.imu.tiltX = -0.5f; s.wait(2.0f);
          s.imu.tiltX = 0; s.imu.tiltY = 0.5f; s.wait(1.6f);
          s.imu.tiltX = 0.5f; s.imu.tiltY = -0.25f; s.wait(2.6f);
          s.imu.tiltX = s.imu.tiltY = 0;
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Monate
        [](Sim& s) { s.tap(266, 261); },                                                  // Tom
        [](Sim& s) { s.tap(184, 311); },                                                  // Hahn: gar nicht
        [](Sim& s) { s.wait(4.4f); s.cupTap(); },                                         // Hütchenspiel mit Tauschen
        [](Sim& s) { s.tap(102, 261); },                                                  // Kamm
        [](Sim& s) { s.type("6"); s.wait(2.3f); s.typeAnswer(); },                        // Spinne: 6 (Falle), 8
        [](Sim& s) { s.typeAnswer(); },                                                   // 5er-Reihe
        [](Sim& s) { s.type("2"); s.wait(2.3f); s.typeAnswer(); },                        // Äpfel: 2 (Falle), 3
        [](Sim& s) { s.wait(3.0f); s.typeAnswer(); },                                     // 5 Ziffern merken
        [](Sim& s) { s.tap(184, 311); },                                                  // Löffel
        [](Sim& s) { s.typeAnswer(); },                                                   // Busse
        [](Sim& s) { s.tap(184, 311); },                                                  // Ben
        [](Sim& s) { s.tap(118, 192); s.wait(0.3f); s.popBalloons(); },                   // 1–6: erst falsch (2), dann der Reihe nach
        [](Sim& s) { s.typeAnswer(); },                                                   // ungerade Reihe
        [](Sim& s) { s.tap(266, 261); },                                                  // Mittwoch
        [](Sim& s) {                                                                      // Farbfolge (5)
          s.wait(4.8f);
          for (const char* c = s.game.levelAnswer(); *c; c++) { const int k = *c - '0'; s.tap(k % 2 ? 238 : 134, k / 2 ? 256 : 170); }
        },
        [](Sim& s) { s.tap(102, 311); },                                                  // Karotte
        [](Sim& s) { s.typeAnswer(); },                                                   // Hasen
        [](Sim& s) {                                                                      // Stroop: 6 richtige
          for (int i = 0; i < 6; i++) { const int k = s.game.levelAnswer()[0] - '0'; s.tap(k % 2 ? 266 : 102, k / 2 ? 321 : 273); }
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Würfel
        [](Sim& s) { s.typeAnswer(); },                                                   // fallende Reihe
        [](Sim& s) { s.tap(102, 261); },                                                  // Pinguin
        [](Sim& s) {                                                                      // Labyrinth mit drei Löchern
          s.imu.tiltX = -0.5f; s.wait(2.0f);
          s.imu.tiltX = 0; s.imu.tiltY = 0.5f; s.wait(1.6f);
          s.imu.tiltX = 0.5f; s.imu.tiltY = -0.25f; s.wait(2.6f);
          s.imu.tiltX = s.imu.tiltY = 0;
        },
        [](Sim& s) { s.typeAnswer(); },                                                   // Zahlenrätsel
        [](Sim& s) { s.typeAnswer(); },                                                   // Mama
        [](Sim& s) { s.type("10"); s.wait(2.3f); s.typeAnswer(); },                       // Verdoppeln: 10 (Falle), 8
    };
    for (int i = 0; i < bc::KID_LEVELS; i++) {
      sim.game.startLevel(i);
      sim.wait(0.6f);
      sim.shot(("kind" + std::to_string(i + 1) + "_start").c_str());
      if (i < 12) SOLVE[i](sim); else KSOLVE[i - 12](sim);
      sim.wait(0.15f);
      sim.shot(("kind" + std::to_string(i + 1) + "_aktion").c_str());
      sim.wait(1.6f);
      const bool ok = (sim.game.doneMask() >> i) & 1;
      printf("Kind %2d: %s\n", i + 1, ok ? "gelöst" : "NICHT GELÖST");
      if (!ok) fails++;
    }
    sim.game.enterLevel(12);
    sim.wait(0.6f);
    sim.shot("kind_karte");
    // Display aus/an mitten im Level: erst „Wer spielt?“, gleicher Modus geht im Level weiter
    sim.tap(184, 358);
    sim.game.wake(sim.ms);
    sim.wait(0.3f);
    sim.tap(184, 230);
    sim.wait(0.3f);
    if (std::strncmp(sim.game.debug(), "screen 2 modus kind level 13 overlay 0", 38)) { printf("Aufwecken: %s\n", sim.game.debug()); fails++; }
    sim.game.wake(sim.ms);   // anderer Modus: zum Titelbild der Erwachsenen
    sim.wait(0.3f);
    sim.tap(299, 230);
    sim.wait(0.3f);
    if (std::strncmp(sim.game.debug(), "screen 0 modus erwachsen", 24)) { printf("Moduswechsel: %s\n", sim.game.debug()); fails++; }
    sim.shot("titel_erwachsen");

    // ---- Kleine (unter 5) ----
    sim.tap(90, 259);
    sim.wait(0.3f);
    sim.tap(69, 230);
    sim.wait(0.3f);
    if (sim.game.mode() != bc::Game::SMALL) { printf("Klein-Modus nicht gewählt\n"); fails++; }
    sim.shot("klein_titel");
    static const Fn SSOLVE[bc::SMALL_LEVELS] = {
        [](Sim& s) { s.drag(84, 318, 228, 236, 0.8f); },                                   // Bonbon ins Maul
        [](Sim& s) { s.colorTap(); },                                                       // gelb
        [](Sim& s) { s.imu.shaking = true; s.wait(1.2f); s.imu.shaking = false; },          // schütteln
        [](Sim& s) { s.tap(184 - 64 * 2 + 29 + 64, 297); s.wait(0.4f); s.tap(184 - 64 * 2 + 29 + 128, 297); },   // 2 (falsch), 3
        [](Sim& s) { s.sideTap(); },                                                        // großer Elefant
        [](Sim& s) { s.drag(62, 146, 236, 200, 0.8f); s.wait(1.5f); },                      // Sonne
        [](Sim& s) { s.voice = 0.8f; s.wait(2.0f); s.voice = 0; },                          // pusten (das Level hört erst nach 1 s hin)
        [](Sim& s) { s.shapeTap(); },                                                       // Kreis
        [](Sim& s) { s.hold(184, 236, 4.4f); },                                             // Finger aufs Ei
        [](Sim& s) { s.tap(184 + 64, 297); },                                               // 4 Mäuse
        [](Sim& s) { s.voice = 0.8f; s.wait(0.8f); s.voice = 0; },                          // rufen
        [](Sim& s) { s.colorTap(); },                                                       // rot
        [](Sim& s) { s.circle(184, 232, 86, 1.6f); },                                       // Ring
        [](Sim& s) { s.sideTap(); },                                                        // kleine Maus
        [](Sim& s) { s.shapeTap(); },                                                       // Dreieck
        [](Sim& s) { s.drag(184, 180, 184, 290, 0.6f); },                                   // Fledermaus unter den Ast
        [](Sim& s) { s.slot3(); },                                                          // Eule
        [](Sim& s) { s.colorTap(); },                                                       // blau
        [](Sim& s) { s.popBalloons(); },                                                    // alle Ballons
        [](Sim& s) { s.tap(56 + 64, 297); },                                                // 2 Bonbons
        [](Sim& s) { s.sideTap(); },                                                        // kleiner Elefant
        [](Sim& s) { s.drag(84, 300, 270, 280, 0.8f); },                                    // Maus zum Käse
        [](Sim& s) { s.shapeTap(); },                                                       // Quadrat
        [](Sim& s) { s.rub(); },                                                            // Dino kitzeln
        [](Sim& s) { s.side(); },                                                           // mehr Bonbons
        [](Sim& s) { s.slot3(); },                                                          // Hase
        [](Sim& s) { s.wait(1.6f); s.cupTap(); },                                           // Becher merken
        [](Sim& s) { s.colorTap(); },                                                       // grün
        [](Sim& s) { s.tap(56 + 64 * 4, 297); },                                            // 5 Sterne
        [](Sim& s) { s.drag(84, 300, 270, 280, 0.8f); },                                    // Hund in die Hütte
        [](Sim& s) { s.sideTap(); },                                                        // große Maus
        [](Sim& s) { s.tap(154, 146); s.wait(0.3f); s.popBalloons(); },                     // erst blau (falsch), dann die roten
        [](Sim& s) { s.rub(); },                                                            // Hund streicheln
        [](Sim& s) { s.side(); },                                                           // weniger Mäuse
        [](Sim& s) { s.slot3(); },                                                          // Fledermaus
        [](Sim& s) { s.oddTap(); },                                                         // Maus unter Bonbons
    };
    for (int i = 0; i < bc::SMALL_LEVELS; i++) {
      sim.game.startLevel(i);
      sim.wait(0.6f);
      sim.shot(("klein" + std::to_string(i + 1) + "_start").c_str());
      SSOLVE[i](sim);
      sim.wait(0.15f);
      sim.shot(("klein" + std::to_string(i + 1) + "_aktion").c_str());
      sim.wait(1.6f);
      const bool ok = (sim.game.doneMask() >> i) & 1;
      printf("Klein %2d: %s\n", i + 1, ok ? "gelöst" : "NICHT GELÖST");
      if (!ok) fails++;
    }
    sim.game.enterLevel(3);
    sim.wait(0.6f);
    sim.shot("klein_karte");
    sim.tap(184, 358);

    // ---- Zufallsmodus: in jeder Altersstufe viele gewürfelte Aufgaben; Tastatur-Aufgaben werden gelöst ----
    for (int m : {bc::Game::SMALL, bc::Game::KID, bc::Game::ADULT}) {
      sim.game.setMode(m);
      int solved = 0, padN = 0;
      for (int r = 0; r < 40; r++) {
        sim.game.startRandom();
        sim.wait(0.5f);
        sim.tap(184, 358);   // LOS
        if (r < 4) sim.shot(("zufall" + std::to_string(m) + "_" + std::to_string(r)).c_str());
        if (sim.game.levelHasPad()) {
          padN++;
          sim.wait(9.0f);    // Merk- und Kettenphasen abwarten
          sim.typeAnswer();
          sim.wait(1.6f);
          if (sim.game.streak() > solved) solved++;
          else printf("Zufall %d: \"%s\" nicht gelöst (%s)\n", m, sim.game.levelAnswer(), sim.game.debug());
        }
      }
      printf("Zufall Modus %d: %d von %d Tastatur-Aufgaben gelöst\n", m, solved, padN);
      if (solved != padN) fails++;
    }
    sim.game.setMode(bc::Game::SMALL);
    sim.game.startLevel(0);

    // ---- Einstellungen: von oben wischen ----
    sim.drag(184, 10, 184, 200, 0.4f);
    sim.wait(0.3f);
    sim.shot("einstellungen");
    sim.tap(311, 64 + 22);   // heller
    sim.tap(203, 114 + 22);  // leiser
    sim.tap(40, 264 + 22);   // alles auf 0: erst nachfragen …
    sim.wait(0.2f);
    sim.shot("einstellungen_nachfrage");
    sim.tap(40, 264 + 22);   // … dann wirklich
    sim.wait(0.3f);
    if (std::strncmp(sim.game.debug(), "screen 5", 8) || sim.game.doneMask() != 0) { printf("Zurücksetzen: %s\n", sim.game.debug()); fails++; }
    if (sim.game.brightLevel() != 3) { printf("Helligkeit: %d\n", sim.game.brightLevel()); fails++; }
  }
  printf("%d Level nicht gelöst, %d Textprobleme\n", fails, nIssues);
  return fails || nIssues ? 1 : 0;
}

#ifdef WITH_SDL
static int window() {
  SDL_Init(SDL_INIT_VIDEO);
  const int Z = 2;
  SDL_Window* win = SDL_CreateWindow("Brain Candy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * Z, H * Z, 0);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
  Sim sim;
  bool run = true, mouse = false;
  int shotN = 0;
  while (run) {
    std::vector<TouchSample> touch;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) run = false;
      if (e.type == SDL_MOUSEBUTTONDOWN) { mouse = true; touch.push_back({true, int16_t(e.button.x / Z), int16_t(e.button.y / Z), sim.ms}); }
      if (e.type == SDL_MOUSEMOTION && mouse) touch.push_back({true, int16_t(e.motion.x / Z), int16_t(e.motion.y / Z), sim.ms});
      if (e.type == SDL_MOUSEBUTTONUP) { mouse = false; touch.push_back({false, int16_t(e.button.x / Z), int16_t(e.button.y / Z), sim.ms}); }
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_k) sim.imu.knocks.push_back(sim.ms);
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p) { renderAll(); savePpm("bcsim-" + std::to_string(shotN++) + ".ppm"); }
    }
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    sim.imu.shaking = k[SDL_SCANCODE_X];
    sim.imu.upside = k[SDL_SCANCODE_U];
    sim.voice = k[SDL_SCANCODE_M] ? 0.8f : 0;
    sim.ms = SDL_GetTicks() + 1000;
    sim.frame(touch);
    renderAll();
    SDL_UpdateTexture(tex, nullptr, fb, W * 2);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    SDL_Delay(20);
  }
  SDL_Quit();
  return 0;
}
#endif

int main(int argc, char** argv) {
  if (argc > 2 && !std::strcmp(argv[1], "--tour")) return tour(argv[2]);
  if (argc > 1 && !std::strcmp(argv[1], "--voice")) {   // Sätze für tools/mkvoice.py
    bc::Game::voiceLines([](const char* k, const char* t) { printf("%s\t%s\n", k, t); });
    return 0;
  }
#ifdef WITH_SDL
  return window();
#else
  printf("Aufruf: bcsim --tour <ordner>   (Fenster: make -C sim sdl)\n");
  return 1;
#endif
}
