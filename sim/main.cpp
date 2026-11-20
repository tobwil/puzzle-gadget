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
  std::vector<uint32_t> knocks;
  uint32_t last = 0;
  void fill(Input& in, uint32_t ms) {
    in.nAcc = 0;
    if (!last) last = ms - 33;
    for (uint32_t t = last + 2; t <= ms && in.nAcc < 64; t += 2) {
      float x = 0, y = upside ? -1.f : 0.f, z = upside ? 0.f : 1.f;
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
    sim.shot("titel");
    sim.tap(296, 384);
    sim.wait(0.3f);
    sim.shot("levels");
    sim.tap(36, 40);
    sim.tap(142, 384);   // Spielen
    using Fn = void (*)(Sim&);
    static const Fn SOLVE[bc::LEVELS] = {
        [](Sim& s) { s.drag(84, 318, 228, 236, 0.8f); },                                   // Bonbon ins Maul
        [](Sim& s) { s.tap(124, 296); s.wait(0.5f); s.tap(304, 208); },                   // erst die Maus (falsch), dann der Elefant
        [](Sim& s) { s.tap(160, 318); s.drag(276, 236, 300, 160, 0.6f); s.tap(208, 318); },   // 4 (falsch), Becher weg, 5
        [](Sim& s) { s.tap(274, 260); s.drag(104, 282, 274, 250, 0.8f); s.tap(274, 260); },   // Tür auf, rein, zu
        [](Sim& s) { s.tap(184, 236); s.wait(0.3f); s.hold(184, 236, 3.8f); },            // Tippen reicht nicht, halten
        [](Sim& s) { s.drag(62, 146, 236, 200, 0.8f); s.wait(1.5f); },                    // Sonne zum Dino
        [](Sim& s) { s.circle(184, 232, 86, 1.6f); },                                     // Ring nachfahren
        [](Sim& s) { s.tap(184, 230); s.imu.shaking = true; s.wait(1.2f); s.imu.shaking = false; },
        [](Sim& s) { s.drag(184, 82, 184, 150, 0.6f); },                                  // Aufgabentext wegziehen
        [](Sim& s) { s.tap(184, 172); s.voice = 0.8f; s.wait(1.4f); s.voice = 0; },       // pusten
        [](Sim& s) { for (int i = 0; i < 2; i++) { s.imu.knocks.push_back(s.ms + 30); s.wait(0.3f); } },   // klopfen
        [](Sim& s) { s.tap(184, 160); s.imu.upside = true; s.wait(1.0f); s.imu.upside = false; },          // umdrehen
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
    sim.game.startLevel(11);
    sim.wait(0.3f);
    sim.imu.upside = true; sim.wait(1.0f); sim.imu.upside = false;
    sim.wait(3.0f);
    sim.tap(184, 360);
    sim.wait(0.6f);
    sim.shot("ende");
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
#ifdef WITH_SDL
  return window();
#else
  printf("Aufruf: bcsim --tour <ordner>   (Fenster: make -C sim sdl)\n");
  return 1;
#endif
}
