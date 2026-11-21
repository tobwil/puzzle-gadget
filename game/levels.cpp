// Die Level. Jedes Level ist ein kleines Trickrätsel: Die naheliegende Antwort ist selten die richtige.
// Mechaniken: ziehen, tippen, halten, Kreis malen, schütteln, klopfen, pusten (Mikrofon), umdrehen (IMU),
// und Teile der Oberfläche selbst (Aufgabentext, „Deko“) sind Teil des Rätsels.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "ui.h"

namespace bc {

// Zufall für alle Level: auf dem Gerät aus esp_random() gesät, im Simulator fest (reproduzierbare Tour)
static uint32_t gRnd = 0x12345678u;
void seedRandom(uint32_t s) { gRnd = s ? s : 0x12345678u; }
uint32_t rnd() {   // xorshift32
  gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5;
  return gRnd;
}

namespace {

constexpr float CX = W / 2.f;
// zufällige Reihenfolge 0..n-1
void shuffle(int* a, int n) { for (int i = 0; i < n; i++) a[i] = i; for (int i = n - 1; i > 0; i--) std::swap(a[i], a[rndi(i + 1)]); }

// Gegenstand, den man mit dem Finger verschieben kann
struct Drag {
  float x = 0, y = 0, gx = 0, gy = 0;
  bool on = false;
  void set(float px, float py) { x = px; y = py; on = false; }
  bool grab(const Frame& f, float r) {
    if (f.pressed && dist(f.x, f.y, x, y) < r) { on = true; gx = x - f.x; gy = y - f.y; }
    return on;
  }
  void move(const Frame& f, float x0 = 30, float y0 = 110, float x1 = W - 30, float y1 = 345) {
    if (on && f.down) { x = clampf(f.x + gx, x0, x1); y = clampf(f.y + gy, y0, y1); }
  }
  bool drop(const Frame& f) {
    if (on && f.released) { on = false; return true; }
    return false;
  }
};

// Gelöst melden, kurz nachdem die Erfolgs-Animation begonnen hat
struct Win {
  float at = -1;
  bool sent = false;
  void start(const Frame& f) { if (at < 0) at = f.t; }
  bool on() const { return at >= 0; }
  float since(const Frame& f) const { return at < 0 ? 0 : f.t - at; }
  void tick(const Frame& f, Host& h, float delay) { if (at >= 0 && !sent && f.t - at >= delay) { sent = true; h.solve(); } }
  void reset() { at = -1; sent = false; }
};

// Sperre nach falscher Antwort: 4 s, 8 s, 16 s … (höchstens 30 s), damit Durchprobieren nicht lohnt
struct Lock {
  int fails = 0;
  float until = -1;
  bool on(const Frame& f) const { return f.t < until; }
  float left(const Frame& f) const { return until - f.t; }
  // Kinder: 2 s, 3 s, 4 s … höchstens 5 s
  void fail(const Frame& f, bool kids = false) {
    fails++;
    until = f.t + (kids ? std::fmin(5.f, 1.f + fails) : std::fmin(30.f, 4.f * float(1 << std::min(fails - 1, 4))));
  }
  void reset() { fails = 0; until = -1; }
};

// Zahlentastatur (Lage in ui.h): Anzeige, darunter „1 2 3 4 5 6“ und „7 8 9 0 < OK“
struct NumPad {
  char buf[10] = {};
  int n = 0, lastKey = -1;
  float keyT = -10, wrongT = -10;
  void clear() { n = 0; buf[0] = 0; }
  static int hit(float x, float y) {
    for (int k = 0; k < 12; k++)
      if (std::fabs(x - npKeyX(k)) < NP_KW / 2 + 3 && std::fabs(y - npKeyY(k)) < NP_KH / 2 + 3) return k;
    return -1;
  }
  // true, wenn OK mit einer Eingabe gedrückt wurde
  bool tick(const Frame& f, Host& h, bool locked) {
    if (!f.tap || locked) return false;
    const int k = hit(f.x, f.y);
    if (k < 0) return false;
    lastKey = k;
    keyT = f.t;
    h.sfx(SFX_CLICK);
    if (k < 10) {
      if (n < 8) { buf[n++] = char(k == 9 ? '0' : '1' + k); buf[n] = 0; }
    } else if (k == 10) {
      if (n) buf[--n] = 0;
    } else {
      return n > 0;
    }
    return false;
  }
  bool is(const char* ans) const { return std::strcmp(buf, ans) == 0; }
  void wrong(const Frame& f) { wrongT = f.t; clear(); }
  void draw(Scene& s, const Frame& f, const Lock& lock, bool won) const {
    const bool locked = lock.on(f);
    const float sh = f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
    s.rrect(CX - 110 + sh, NP_DY, 220, 36, 12, won ? INK : locked ? G1 : hex(0xE9FDFE));
    if (locked) {
      char b[16];
      std::snprintf(b, sizeof b, "%d s", int(std::ceil(lock.left(f))));
      s.text(fonts::heavy, b, CX, NP_DY + 2, G4, 1, Align::Center);
    } else {
      s.text(fonts::heavy, n ? buf : "?", CX + sh, NP_DY + 2, won ? WHITE : n ? INK : G3, 1, Align::Center);
    }
    static const char* L[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "<", "OK"};
    const float a = locked ? 0.35f : 1;
    for (int k = 0; k < 12; k++) {
      const bool down = k == lastKey && f.t - keyT < 0.12f;
      const float x = npKeyX(k) - NP_KW / 2, y = npKeyY(k) - NP_KH / 2;
      s.rrect(x, y, NP_KW, NP_KH, 12, k == 11 ? INK : down ? BTN_DOWN : BTN, a);
      if (k == 11) s.text(fonts::label, L[k], npKeyX(k), y + 14, WHITE, a, Align::Center);
      else s.text(fonts::num, L[k], npKeyX(k), y + 10, INK, a, Align::Center);
    }
  }
};

// Zzz in Handschrift, steigt auf
void zzz(Scene& s, float x, float y, float t, Col c) {
  for (int i = 0; i < 3; i++) {
    const float u = std::fmod(t * 0.5f + i / 3.f, 1.f);
    s.text(fonts::hand, i == 1 ? "Z" : "z", x + u * 26 + i * 4, y - u * 44, c, std::sin(u * PIF), Align::Center);
  }
}

// ======================= 1: Füttere den Dino (ziehen) =======================
class FeedDino : public Level {
  Drag candy;
  bool eaten = false, wrapped = true;
  const bool wrap0;   // false: Bonbon liegt schon ausgepackt da (Kleine)
  float unwrapT = -10;
  float eatT = 0;
  Win win;
  static constexpr float HX = 252, HY = 206, K = 0.92f;
  float mouthX() const { return HX - 30 * K; }
  float mouthY() const { return HY + 30 * K; }

 public:
  explicit FeedDino(bool wrap = true) : wrap0(wrap) {}
  Txt task() const override { return {"Feed the dinosaur", "Füttere den Dino"}; }
  Txt hint() const override { return {"Nobody eats candy with the wrapper. Swipe quickly across it to unwrap it.", "Niemand isst Bonbons mit Papier. Wisch schnell darüber, um es auszupacken."}; }
  void reset() override { candy.set(84, 318); eaten = false; wrapped = wrap0; unwrapT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (eaten) return;
    if (wrapped && !candy.on && f.down && std::fabs(f.vx) > 500 && dist(f.x, f.y, candy.x, candy.y) < 40) {
      wrapped = false; unwrapT = f.t; h.sfx(SFX_WHOOSH); h.say(candy.x + 40, candy.y - 50, {"Rip!", "Ritsch!"});
    }
    if (candy.grab(f, 44)) {
      if (f.pressed) h.sfx(SFX_POP);
      candy.move(f);
      if (candy.drop(f)) {
        if (dist(candy.x, candy.y, mouthX(), mouthY()) < 58) {
          if (wrapped) { h.say(mouthX(), mouthY() - 90, {"With the wrapper? Yuck!", "Mit Papier? Igitt!"}); h.sfx(SFX_NOPE); candy.set(84, 318); }
          else { eaten = true; eatT = f.t; h.sfx(SFX_CHOMP); win.start(f); }
        } else h.sfx(SFX_CLICK);
      }
    } else if (f.tap && dist(f.x, f.y, HX + 10, HY - 10) < 70) {
      h.say(f.x, f.y - 30, {"RAWR!", "RAWR!"});
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Cyan-Schnörkel hinter dem Kopf, wie beim App-Symbol
    const float sc[] = {150, 300, 168, 196, 214, 140, 290, 138, 336, 190, 344, 300, 330, 350};
    scribble(s, sc, 7, 10, CYAN, 0.9f);
    float open = 0.55f + 0.08f * std::sin(f.t * 2.2f);
    if (candy.on) open = 0.55f + 0.45f * clampf(1 - dist(candy.x, candy.y, mouthX(), mouthY()) / 200, 0, 1);
    if (eaten) { const float u = f.t - eatT; open = u < 0.7f ? 0.25f * std::fabs(std::sin(u * 14)) : 0; }
    const float blink = std::fmod(f.t, 4.f) < 0.12f ? 1 : 0;
    drawDinoHead(s, HX, HY, K, open, blink);
    sparkle(s, 60, 150, 11, INK, 1, 0.3f);
    sparkle(s, 330, 128, 8, INK, 1, 0.1f);
    sparkle(s, 112, 232, 7, INK);
    if (!eaten) {
      if (wrapped) drawCandy(s, candy.x, candy.y, candy.on ? 1.15f : 1.f);
      else {
        const float k = candy.on ? 1.15f : 1.f;
        s.ellipse(candy.x, candy.y, 15 * k, 13 * k, PINK, HOTPINK);
        s.ellipse(candy.x - 5 * k, candy.y - 5 * k, 5 * k, 3 * k, WHITE, 0.6f);
        const float u = f.t - unwrapT;   // Papierfetzen fliegen weg
        if (u < 0.8f) for (int i = 0; i < 2; i++) tri(s, candy.x + (i ? 1 : -1) * (20 + u * 90), candy.y - u * 60, candy.x + (i ? 1 : -1) * (34 + u * 90), candy.y - 8 - u * 60, candy.x + (i ? 1 : -1) * (26 + u * 90), candy.y + 10 - u * 60, PINK, 1 - u / 0.8f);
      }
    } else {
      const float u = f.t - eatT;
      if (u < 0.25f) s.ellipse(mouthX(), mouthY(), 15 * (1 - u * 4), 13 * (1 - u * 4), PINK, HOTPINK);
      for (int i = 0; i < 3; i++) {   // kleine Herzen steigen auf
        const float v = clampf((u - 0.3f - i * 0.15f) / 1.2f, 0, 1);
        if (v <= 0 || v >= 1) continue;
        const float hx = HX - 40 + i * 34, hy = HY - 70 - v * 50;
        s.circle(hx - 5, hy, 6, HOTPINK, 1 - v);
        s.circle(hx + 5, hy, 6, HOTPINK, 1 - v);
        tri(s, hx - 11, hy + 2, hx + 11, hy + 2, hx, hy + 14, HOTPINK, 1 - v);
      }
    }
  }
};

// ======================= 2: Das größte Tier (Perspektive) =======================
class Biggest : public Level {
  Win win;
  static constexpr float EX = 304, EY = 208;
 public:
  Txt task() const override { return {"Tap the biggest animal", "Tippe auf das größte Tier"}; }
  Txt hint() const override { return {"Real size counts, not size on screen. Look far away!", "Es zählt die echte Größe, nicht die auf dem Bild. Schau in die Ferne!"}; }
  void reset() override { win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.2f);
    if (win.on() || !f.tap) return;
    if (dist(f.x, f.y, EX, EY) < 36) { win.start(f); h.sfx(SFX_MAGIC); h.say(EX - 40, EY - 40, {"TOOT!", "TÖRÖÖ!"}, HOTPINK); }
    else if (dist(f.x, f.y, 124, 296) < 90) { h.sfx(SFX_NOPE); h.say(f.x, f.y - 40, {"Nope, it's just close!", "Nein, die ist nur nah dran!"}); }
    else if (dist(f.x, f.y, 250, 250) < 45) h.nope(f.x, f.y - 30);
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Horizont mit Hügeln
    s.ellipse(110, 232, 150, 26, G1, G2);
    s.ellipse(300, 230, 120, 20, G1, G2);
    s.capsule(24, 222, 344, 222, 1.2f, G3);
    s.circle(66, 150, 24, YELLOW);   // Sonne
    const float u = ease(win.since(f) / 0.9f);
    drawRabbit(s, 252, 262, 0.55f * (1 - 0.3f * u));
    drawElephant(s, lerpf(EX, 220, u), lerpf(EY, 230, u), lerpf(0.2f, 1.25f, u));
    drawMouse(s, lerpf(124, 70, u), lerpf(298, 320, u), lerpf(1.25f, 0.5f, u), true);
    if (u > 0.5f) for (int i = 0; i < 3; i++) sparkle(s, 140 + i * 90, 140 + (i % 2) * 30, 10, CYAN, (u - 0.5f) * 2, f.t);
  }
};

// ======================= 3: Wie viele Bonbons? (Becher anheben, Antwort tippen) =======================
class CountCandy : public Level {
  Drag cup;
  Win win;
  NumPad pad;
  Lock lock;
 public:
  Txt task() const override { return {"How many candies are there?", "Wie viele Bonbons gibt es?"}; }
  Txt hint() const override { return {"Not everything is in plain sight. Move the cup!", "Nicht alles liegt offen da. Schieb den Becher weg!"}; }
  const char* answer() const override { return "6"; }
  bool hasPad() const override { return true; }
  void reset() override { cup.set(276, 186); win.reset(); pad.clear(); lock.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.7f);
    if (win.on()) return;
    if (cup.grab(f, 48)) {
      cup.move(f, 44, 130, W - 44, 186);
      cup.drop(f);
      return;
    }
    if (pad.tick(f, h, lock.on(f))) {
      if (pad.is("6")) { win.start(f); h.sfx(SFX_WIN); }
      else { lock.fail(f, kids); pad.wrong(f); h.nope(CX, NP_DY - 30); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    static const float C[6][2] = {{70, 150}, {150, 132}, {128, 198}, {56, 206}, {274, 172}, {278, 204}};   // die letzten zwei unter dem Becher
    static const Col CC[6] = {PINK, CYAN, HOTPINK, YELLOW, PURPLE, MINT};
    for (int i = 0; i < 6; i++) drawCandy(s, C[i][0], C[i][1], i < 4 ? 0.85f : 0.62f, CC[i]);
    // Becher (umgedreht): Körper, Rand unten, Henkel
    const float x = cup.x, y = cup.y;
    s.arc(x + 34, y - 4, 16, 6, 0.3f, 2.8f, G4);
    s.rrect(x - 34, y - 42, 68, 80, 12, G3);
    s.ellipse(x, y - 4, 32, 38, G2, G4, 0.6f);
    s.ellipse(x, y + 36, 40, 9, G2, G4);
    s.ellipse(x, y - 42, 30, 6, G4, G3);
    s.ellipse(x - 16, y - 10, 5, 22, WHITE, 0.5f);
    pad.draw(s, f, lock, win.on());
  }
};

// ======================= 4: Elefant in den Kühlschrank (drei Schritte) =======================
class Fridge : public Level {
  Drag ele;
  bool open = false, inside = false;
  float doorT = -10;
  Win win;
  static constexpr float FX = 222, FY = 126, FW = 104, FH = 222;
  bool onFridge(float x, float y) const { return x > FX - 6 && x < FX + FW + 24 && y > FY && y < FY + FH; }
 public:
  Txt task() const override { return {"Put the elephant in the fridge", "Steck den Elefanten in den Kühlschrank"}; }
  Txt hint() const override { return {"Three steps: open the door, put it in, close the door.", "Drei Schritte: Tür auf, Elefant rein, Tür zu."}; }
  void reset() override { ele.set(104, 282); open = inside = false; doorT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.8f);
    if (win.on()) return;
    if (!inside && ele.grab(f, 56)) {
      ele.move(f, 60, 170, 300, 300);
      if (ele.drop(f)) {
        if (onFridge(ele.x, ele.y)) {
          if (open) { inside = true; h.sfx(SFX_POP); }
          else { h.say(ele.x, ele.y - 70, {"The door is closed!", "Die Tür ist zu!"}); h.sfx(SFX_NOPE); ele.set(104, 282); }
        }
      }
      return;
    }
    if (f.tap && onFridge(f.x, f.y)) {
      open = !open;
      doorT = f.t;
      h.sfx(SFX_CLICK);
      if (!open && inside) { win.start(f); h.sfx(SFX_WIN); }
      else if (!open && !inside) h.say(CX - 40, 200, {"Hmm, and now?", "Hmm, und jetzt?"});
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Kühlschrank: Gefrierfach oben, Haupttür unten
    s.rrect(FX, FY, FW, FH, 14, G2);
    s.rrect(FX + 3, FY + 3, FW - 6, FH - 6, 12, hex(0xF7F7F7));
    s.capsule(FX + 4, FY + 64, FX + FW - 4, FY + 64, 1.6f, G3);
    s.capsule(FX + FW - 16, FY + 22, FX + FW - 16, FY + 46, 3, G4);
    if (open) {
      s.rrect(FX + 8, FY + 70, FW - 16, FH - 78, 8, hex(0xE6FAFF));
      for (int i = 1; i < 3; i++) s.capsule(FX + 10, FY + 70 + i * 48, FX + FW - 10, FY + 70 + i * 48, 1.5f, hex(0xB8E4EE));
      s.glow(FX + FW / 2, FY + 90, 60, hex(0xFFFFFF), 0.6f);
      s.rrect(FX + FW + 2, FY + 64, 18, FH - 60, 6, G3);   // offene Tür, schräg von der Seite
      s.capsule(FX + FW + 11, FY + 90, FX + FW + 11, FY + 130, 2.6f, G5);
    } else {
      s.capsule(FX + FW - 16, FY + 84, FX + FW - 16, FY + 132, 3, G4);
    }
    if (inside) drawElephant(s, FX + FW / 2 + 4, FY + 160, 0.48f);
    else drawElephant(s, ele.x, ele.y, ele.on ? 0.78f : 0.82f);
    if (inside && !open) {   // Elefant schaut durch die Tür nicht mehr raus
      s.rrect(FX + 3, FY + 67, FW - 6, FH - 70, 12, hex(0xF7F7F7));
      s.capsule(FX + FW - 16, FY + 84, FX + FW - 16, FY + 132, 3, G4);
    }
    if (win.on()) for (int i = 0; i < 3; i++) sparkle(s, FX - 20 + i * 70, FY - 4 + (i % 2) * 24, 10, CYAN, 1, f.t * 2);
    (void)doorT;
  }
};

// ======================= 5: Brüte das Ei aus (halten) =======================
class Hatch : public Level {
  float heat = 0, wobT = -10;
  Win win;
  const float secs;   // so lange muss der Finger liegen
  static constexpr float EX = CX, EY = 236;
 public:
  explicit Hatch(float s = 7.0f) : secs(s) {}
  Txt task() const override { return {"Hatch the egg", "Lass das Küken schlüpfen"}; }
  Txt hint() const override { return {"Eggs need warmth. Keep your finger on it for a while.", "Eier brauchen Wärme. Lass deinen Finger eine Weile drauf."}; }
  void reset() override { heat = 0; wobT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.1f);
    if (win.on()) return;
    const bool onEgg = f.down && dist(f.x, f.y, EX, EY) < 70;
    const float before = heat;
    if (onEgg && f.hold > 0.35f) heat += f.dt / secs;
    else heat = std::fmax(0.f, heat - f.dt * 0.08f);
    if (int(before * 5) != int(heat * 5) && heat > before) h.sfx(SFX_CRACK);
    if (f.tap && dist(f.x, f.y, EX, EY) < 70) { wobT = f.t; h.say(EX + 70, EY - 80, {"Not yet…", "Noch nicht…"}); }
    if (heat >= 1) { win.start(f); h.sfx(SFX_WIN); h.say(EX, EY - 120, {"Hi!", "Hallo!"}, HOTPINK); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Nest
    s.ellipse(EX, 314, 112, 26, G4, G5);
    for (int i = 0; i < 9; i++) {
      const float a = i * 0.7f;
      s.capsule(EX - 108 + i * 24, 300 + std::sin(a) * 6, EX - 80 + i * 24, 320 - std::cos(a) * 6, 3.5f, i % 2 ? G5 : G4);
    }
    if (!win.on()) {
      const float wob = f.t - wobT < 0.6f ? std::sin((f.t - wobT) * 30) * (0.6f - (f.t - wobT)) : heat > 0.5f ? std::sin(f.t * 25) * 0.15f * heat : 0;
      if (f.down && dist(f.x, f.y, EX, EY) < 70) s.glow(f.x, f.y, 60, ORANGE, 0.25f);
      drawEgg(s, EX, EY, 1.2f, heat, wob);
    } else {
      const float u = win.since(f);
      drawDino(s, EX, 262 - ease(u / 0.5f) * 20, 0.55f, 1);
      // untere Schale mit Zacken
      s.ellipse(EX, 292, 56, 30, hex(0xFBF8F0), hex(0xD8D0C0));
      for (int i = 0; i < 6; i++) tri(s, EX - 52 + i * 21, 276, EX - 38 + i * 21, 276, EX - 45 + i * 21, 260, hex(0xFBF8F0));
      // obere Schale fliegt weg
      const float v = clampf(u / 0.8f, 0, 1);
      s.ellipse(EX + v * 90, 190 - v * 60, 40, 26, hex(0xFFFDF8), hex(0xEAE3D6), 1 - v);
      for (int i = 0; i < 4; i++) sparkle(s, EX - 110 + i * 70, 150 + (i % 2) * 40, 10, i % 2 ? CYAN : INK, 1, u * 2);
    }
  }
};

// ======================= 6: Dem Dino ist kalt (die „Deko“-Sonne ziehen) =======================
class ColdDino : public Level {
  Drag sun;
  float warm = 0;
  Win win;
  static constexpr float DX = 214, DY = 282;
 public:
  Txt task() const override { return {"The dinosaur is freezing", "Dem Dino ist kalt"}; }
  Txt hint() const override { return {"That yellow circle is not just decoration…", "Der gelbe Kreis ist nicht nur Deko…"}; }
  void reset() override { sun.set(62, 146); warm = 0; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (sun.grab(f, 40)) { sun.move(f, 40, 120, W - 40, 330); sun.drop(f); }
    else if (f.tap && dist(f.x, f.y, DX, DY - 10) < 70 && !win.on()) h.say(DX + 30, DY - 110, {"Brrrr!", "Brrrr!"}, hex(0x00A8C0));
    const bool near = dist(sun.x, sun.y, DX + 30, DY - 60) < 110;
    warm = clampf(warm + (near ? f.dt / 1.2f : -f.dt / 3.f), 0, 1);
    if (warm >= 1 && !win.on()) { win.start(f); h.sfx(SFX_WIN); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    const float cold = win.on() ? 0 : 1 - warm;
    // Schneeflocken
    for (int i = 0; i < 9; i++) {
      const float sx = 40 + std::fmod(i * 83.f, 300.f), sy = 120 + std::fmod(i * 47.f + f.t * (22 + i * 3), 230.f);
      sparkle(s, sx + std::sin(f.t + i) * 6, sy, 6 + (i % 3) * 2, CYAN, cold, i);
    }
    s.ellipse(CX, 352, 160, 16, G1, G2);   // Boden mit Schnee
    const float jit = cold > 0.1f ? std::sin(f.t * 70) * 2.2f * cold : 0;
    drawDino(s, DX + jit, DY, 0.95f, win.on() ? 1 : 0);
    if (cold > 0.1f) {   // Zitterstriche
      for (int i = 0; i < 3; i++) {
        s.capsule(DX - 70, DY - 30 + i * 14, DX - 84, DY - 34 + i * 14, 2, CYAN, cold);
        s.capsule(DX + 92, DY - 66 + i * 14, DX + 106, DY - 70 + i * 14, 2, CYAN, cold);
      }
      s.text(fonts::hand, "brrr", DX + 74, DY - 120, hex(0x00A8C0), cold, Align::Center);
    }
    if (warm > 0.05f || win.on()) rays(s, sun.x, sun.y, 40, 52, 10, 3, YELLOW, win.on() ? 1 : warm, f.t * 0.8f);
    s.circle(sun.x, sun.y, sun.on ? 33 : 30, YELLOW);
  }
};

// ======================= 7: Öffne das Portal (Kreis nachfahren) =======================
class Portal : public Level {
  bool tracing = false;
  float last = 0, start = 0, sum = 0, fadeSum = 0, fadeT = -10;
  Win win;
  static constexpr float PX = CX, PY = 232, R = 86;
 public:
  Txt task() const override { return {"Open the portal", "Öffne das Portal"}; }
  Txt hint() const override { return {"Draw it open: trace the whole ring with your finger.", "Mal es auf: Fahr mit dem Finger einmal um den ganzen Ring."}; }
  void reset() override { tracing = false; sum = 0; fadeT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.4f);
    if (win.on()) return;
    const float d = dist(f.x, f.y, PX, PY);
    const float an = std::atan2(f.x - PX, -(f.y - PY));
    if (f.down && d > 46 && d < 132) {
      if (!tracing) { tracing = true; last = start = an; sum = 0; }
      float da = an - last;
      if (da > PIF) da -= TAU;
      if (da < -PIF) da += TAU;
      sum += da;
      last = an;
      if (std::fabs(sum) > TAU * 0.92f) { win.start(f); h.sfx(SFX_MAGIC); tracing = false; }
    } else if (tracing && (f.released || !f.down || d <= 46 || d >= 132)) {
      tracing = false;
      fadeSum = sum; fadeT = f.t;
      if (std::fabs(sum) > PIF) h.say(PX, PY + 120, {"Almost!", "Fast!"}, PURPLE);
    }
    if (f.tap && std::fabs(dist(f.x, f.y, PX, PY) - R) < 30) h.say(f.x, f.y - 40, {"Locked…", "Verschlossen…"});
  }
  void draw(Scene& s, const Frame& f, int) override {
    const float u = win.since(f);
    if (win.on()) {   // offenes Portal: Wirbel
      const float g = ease(u / 0.8f);
      s.glow(PX, PY, (R + 40) * g, PURPLE, 0.55f);
      s.circle(PX, PY, (R - 12) * g, hex(0x1B0B3A));
      for (int i = 0; i < 4; i++) {
        const float rr = (18 + i * 16) * g, a0 = f.t * (2.5f - i * 0.4f) + i;
        s.arc(PX, PY, rr, 4, a0, a0 + 3.6f, i % 2 ? CYAN : PINK, 0.9f);
      }
      s.glow(PX, PY, 40 * g, CYAN, 0.6f);
    }
    // Steinring mit Runen
    s.ring(PX, PY, R, 24, G3);
    s.ring(PX, PY, R - 11, 3, G4);
    s.ring(PX, PY, R + 11, 3, G4);
    for (int i = 0; i < 12; i++) {
      const float a = i * TAU / 12, sx = std::sin(a), cy = -std::cos(a);
      s.capsule(PX + sx * (R - 5), PY + cy * (R - 5), PX + sx * (R + 5), PY + cy * (R + 5), 2, win.on() ? CYAN : G5);
    }
    // Spur des Fingers
    auto trail = [&](float s0, float sw, float a) {
      if (std::fabs(sw) < 0.05f) return;
      const float a0 = sw > 0 ? s0 : s0 + sw, a1 = sw > 0 ? s0 + sw : s0;
      s.arc(PX, PY, R, 10, a0, a1, CYAN, a);
    };
    if (tracing) trail(start, sum, 0.95f);
    else if (f.t - fadeT < 0.6f) trail(start, fadeSum, 1 - (f.t - fadeT) / 0.6f);
    if (win.on()) for (int i = 0; i < 5; i++) sparkle(s, PX + std::sin(i * 1.3f + u) * 130, PY + std::cos(i * 1.7f) * 110, 8 + i, i % 2 ? CYAN : INK, 1, u * 3 + i);
  }
};

// ======================= 8: Weck die Eule (schütteln) =======================
class Owl : public Level {
  float shakeT = 0, wobT = -10;
  Win win;
  static constexpr float OX = CX, OY = 230;
 public:
  int tools() const override { return TOOL_SHAKE; }
  Txt task() const override { return {"Wake up the owl", "Weck die Eule auf"}; }
  Txt hint() const override { return {"Owls are deep sleepers. Give the whole device a good shake!", "Eulen schlafen tief. Schüttel das ganze Gerät kräftig!"}; }
  void reset() override { shakeT = 0; wobT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on()) return;
    if (f.shaking) shakeT += f.dt;
    shakeT += f.knocks * 0.12f;
    if (f.tap && dist(f.x, f.y, OX, OY) < 80) { wobT = f.t; h.say(OX, OY + 110, {"Zzz… 5 more minutes", "Zzz… noch 5 Minuten"}); h.sfx(SFX_CLICK); }
    if (shakeT > 0.6f) { win.start(f); h.sfx(SFX_HOOT); h.say(OX, OY - 130, {"WHO?!", "WER?!"}, HOTPINK); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Ast
    s.capsule(14, 324, 354, 316, 9, G5);
    s.capsule(270, 318, 320, 296, 4, G5);
    s.capsule(80, 322, 50, 300, 4, G5);
    for (float lx : {44.f, 312.f, 330.f}) s.ellipse(lx, lx < 100 ? 296 : 290, 16, 8, G4, G5);
    const float w = f.t - wobT < 0.5f ? std::sin((f.t - wobT) * 40) * 6 * (0.5f - (f.t - wobT)) : 0;
    const float sh = f.shaking ? std::sin(f.t * 60) * 4 : 0;
    const float awake = win.on() ? ease(win.since(f) / 0.3f) : 0;
    drawOwl(s, OX + w + sh, OY, 1.0f, awake, f.t);
    if (!win.on()) zzz(s, OX + 70, OY - 70, f.t, CYAN);
    else rays(s, OX, OY - 30, 96, 112, 12, 2.5f, YELLOW, 1, 0.1f);
  }
};

// ======================= 9: Wo ist die Maus? (Aufgabentext wegziehen) =======================
class HiddenMouse : public Level {
  float tx = 0, ty = 0, gx = 0, gy = 0;
  bool dragging = false;
  Win win;
  static constexpr float MX = CX, MY = 82;
 public:
  Txt task() const override { return {"Where is the mouse?", "Wo ist die Maus?"}; }
  Txt hint() const override { return {"Have you looked behind the words?", "Hast du schon hinter den Wörtern nachgesehen?"}; }
  bool ownsTask() const override { return true; }
  void reset() override { tx = ty = 0; dragging = false; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (f.pressed && f.y > 60 + ty && f.y < 106 + ty && std::fabs(f.x - CX - tx) < 120) { dragging = true; gx = tx - f.x; gy = ty - f.y; }
    if (dragging && f.down) { tx = clampf(f.x + gx, -150, 150); ty = clampf(f.y + gy, -20, 260); }
    if (dragging && f.released) dragging = false;
    if (!win.on() && (std::fabs(tx) > 70 || ty > 38)) { win.start(f); h.sfx(SFX_SQUEAK); h.say(MX, MY + 46, {"Squeak! Found me!", "Piep! Gefunden!"}, HOTPINK); }
    if (dragging || win.on() || !f.tap) return;
    if (dist(f.x, f.y, 184, 270) < 60) h.say(f.x, f.y - 50, {"Just cheese.", "Nur Käse."});
    else if (dist(f.x, f.y, 300, 318) < 30) h.say(f.x - 40, f.y - 50, {"Nobody home.", "Keiner da."});
  }
  void draw(Scene& s, const Frame& f, int lang) override {
    // Fußleiste, Mauseloch, Käse
    s.capsule(14, 340, 354, 340, 2, G3);
    s.rrect(282, 302, 36, 38, 18, G6);
    s.rrect(282, 322, 36, 18, 0, G6);
    tri(s, 110, 296, 258, 296, 258, 236, YELLOW);
    s.rrect(110, 296, 148, 26, 4, hex(0xF5D800));
    static const float HOLES[][3] = {{170, 284, 7}, {214, 270, 9}, {238, 300, 5}, {140, 308, 6}};
    for (auto& c : HOLES) s.circle(c[0], c[1], c[2], hex(0xE0BC00));
    // Maus ganz hinter dem Text versteckt
    const float pk = win.on() ? std::fabs(std::sin(win.since(f) * 6)) * 3 : 0;
    drawMouse(s, MX, MY - pk, 0.42f, true);
    // Aufgabentext auf weißem Schild, das die Maus verdeckt
    const char* t = tr(task(), lang);
    const float tw = Scene::textW(fonts::body, t);
    s.rrect(CX + tx - tw / 2 - 12, 62 + ty, tw + 24, 40, 10, WHITE);
    if (dragging) s.rrect(CX + tx - tw / 2 - 12, 62 + ty, tw + 24, 40, 10, CYAN, 0.12f);
    s.text(fonts::body, t, CX + tx, TASK_Y + ty, INK, 1, Align::Center);
  }
};

// ======================= 10: Puste die Kerzen aus (Mikrofon) =======================
class Candles : public Level {
  bool lit[3] = {true, true, true};
  float outT[3] = {0, 0, 0};
  float blowT = 0;
  Win win;
  static constexpr float CXS[3] = {134, 184, 234};
  static constexpr float FY = 172;
 public:
  int tools() const override { return TOOL_MIC | TOOL_FINGER; }
  Txt task() const override { return {"Make a wish!", "Wünsch dir was!"}; }
  Txt hint() const override { return {"Blow the candles out – really blow at the device. (A fast swipe works too.)", "Puste die Kerzen aus – wirklich aufs Gerät pusten. (Schnelles Wischen geht auch.)"}; }
  void reset() override { for (int i = 0; i < 3; i++) { lit[i] = true; outT[i] = 0; } blowT = 0; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on()) return;
    auto out = [&](int i) { if (lit[i]) { lit[i] = false; outT[i] = f.t; h.sfx(SFX_WHOOSH); } };
    if (f.voice > 0.35f) {
      blowT += f.dt;
      if (blowT > 0.35f) { for (int i = 0; i < 3; i++) if (lit[i]) { out(i); break; } blowT = 0; }
    }
    if (f.down && std::fabs(f.vx) > 600 && f.y > FY - 40 && f.y < FY + 50)
      for (int i = 0; i < 3; i++) if (std::fabs(f.x - CXS[i]) < 26) out(i);
    if (f.tap) for (int i = 0; i < 3; i++)
      if (lit[i] && std::fabs(f.x - CXS[i]) < 22 && f.y > FY - 30 && f.y < FY + 24) { h.say(f.x, f.y - 40, {"Ouch, hot!", "Autsch, heiß!"}, ORANGE); h.sfx(SFX_NOPE); }
    if (!lit[0] && !lit[1] && !lit[2]) { win.start(f); h.sfx(SFX_WIN); h.say(CX, 120, {"Happy birthday!", "Alles Gute!"}, HOTPINK); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, 334, 128, 15, G2, G3);
    s.rrect(CX - 100, 236, 200, 92, 16, hex(0xF6EEE4));
    s.rrect(CX - 100, 276, 200, 16, 0, PINK, 0.85f);
    s.ellipse(CX, 238, 100, 16, PINK, HOTPINK);
    for (int i = 0; i < 9; i++) s.circle(CX - 88 + i * 22, 250 + (i % 3) * 5, 7 + (i % 2) * 2, PINK);
    static const Col SPR[] = {CYAN, YELLOW, PURPLE, WHITE};
    for (int i = 0; i < 12; i++) {
      const float sx = CX - 80 + std::fmod(i * 61.f, 160.f), sy = 300 + std::fmod(i * 23.f, 20.f);
      s.capsule(sx, sy, sx + 5, sy + 3, 1.6f, SPR[i % 4]);
    }
    static const Col CC[3] = {CYAN, YELLOW, HOTPINK};
    const float lean = clampf(f.voice * 1.5f, 0, 1);
    for (int i = 0; i < 3; i++) {
      const float x = CXS[i];
      s.rrect(x - 7, 186, 14, 52, 5, CC[i]);
      for (int k = 0; k < 3; k++) s.capsule(x - 6, 196 + k * 15, x + 6, 190 + k * 15, 1.8f, WHITE, 0.8f);
      s.capsule(x, 186, x, 178, 1.3f, G6);
      if (lit[i]) {
        const float fl = std::sin(f.t * 13 + i * 2) * 1.5f, lx = lean * 8 + fl * 0.5f;
        s.glow(x + lx, FY, 34, AMBER, 0.45f);
        s.ellipse(x + lx, FY - 2, 8, 14 + fl, YELLOW, ORANGE);
        s.ellipse(x + lx * 0.6f, FY + 2, 3.5f, 6, WHITE, YELLOW);
      } else {
        const float u = f.t - outT[i];
        for (int k = 0; k < 4; k++) {
          const float v = std::fmod(u * 0.6f + k * 0.25f, 1.f);
          if (u < 3) s.circle(x + std::sin(v * 6 + k) * 6, FY - v * 60, 4 + v * 8, G3, (1 - v) * 0.6f * (1 - u / 3));
        }
      }
    }
    if (win.on()) for (int i = 0; i < 4; i++) sparkle(s, 70 + i * 76, 140 + (i % 2) * 26, 9, i % 2 ? CYAN : INK, 1, f.t);
  }
};

// ======================= 11: Ruf den Hund (klopfen oder rufen) =======================
class CallDog : public Level {
  float callT = 0, wobT = -10;
  int knocks = 0;
  Win win;
  static constexpr float KX = 292, KY = 282;
 public:
  int tools() const override { return TOOL_KNOCK | TOOL_MIC; }
  Txt task() const override { return {"Call the dog", "Ruf den Hund"}; }
  Txt hint() const override { return {"Dogs come running when someone knocks. Knock on the table – or just call it!", "Hunde kommen angerannt, wenn es klopft. Klopf auf den Tisch – oder ruf ihn einfach!"}; }
  void reset() override { callT = 0; knocks = 0; wobT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.6f);
    if (win.on()) return;
    knocks += f.knocks;
    if (f.voice > 0.4f) callT += f.dt;
    if (f.tap && dist(f.x, f.y, KX, KY + 30) < 60) { wobT = f.t; h.say(KX - 60, KY - 90, {"Zzz… (deep sleep)", "Zzz… (tiefer Schlaf)"}); }
    else if (f.tap && f.y > 120 && f.y < 345) h.nope(f.x, f.y - 30);
    if (knocks >= 2 || callT > 0.35f) { win.start(f); h.sfx(SFX_WOOF); h.say(KX - 30, KY - 110, {"WOOF!", "WUFF!"}, HOTPINK); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, 344, 170, 12, G1, G2);
    // Hütte
    s.rrect(KX - 52, KY - 40, 104, 96, 6, G2);
    for (int i = 0; i < 5; i++) s.capsule(KX - 50, KY - 24 + i * 18, KX + 50, KY - 24 + i * 18, 1.2f, G3);
    tri(s, KX - 66, KY - 36, KX + 66, KY - 36, KX, KY - 96, G5);
    s.rrect(KX - 22, KY + 2, 44, 54, 20, G6);
    s.rrect(KX - 22, KY + 30, 44, 26, 0, G6);
    // Napf mit Knochen
    s.ellipse(96, 330, 34, 10, G4, G5);
    s.rrect(64, 314, 64, 16, 6, G3);
    s.capsule(80, 310, 112, 304, 4, G1);
    for (float bx : {80.f, 112.f}) { s.circle(bx - 2, (bx < 100 ? 310 : 304) - 3, 4.5f, G1); s.circle(bx + 2, (bx < 100 ? 310 : 304) + 3, 4.5f, G1); }
    const float u = win.since(f);
    if (!win.on()) {
      const float w = f.t - wobT < 0.4f ? std::sin((f.t - wobT) * 40) * 2 : 0;
      drawDog(s, KX + 6 + w, KY + 52, 0.55f, 0, f.t, true);
      zzz(s, KX - 20, KY - 4, f.t, CYAN);
    } else {
      const float v = ease(u / 1.0f);
      drawDog(s, lerpf(KX + 6, 170, v), KY + 52 - 10 * v - std::fabs(std::sin(u * 10)) * 8 * (1 - v), 0.7f, 1 - v, f.t, false);
      if (v >= 1) rays(s, 170, 300, 80, 92, 10, 2.5f, YELLOW, 1, f.t * 0.5f);
    }
  }
};

// ======================= 12: Hilf der Fledermaus beim Schlafen (umdrehen) =======================
class Bat : public Level {
  Drag bat;
  float flipT = 0;
  bool hanging = false;
  Win win;
  static constexpr float BRY = 230, K = 0.9f;
  float standY() const { return BRY - 8 - 40 * K; }
  float hangY() const { return BRY + 8 + 40 * K; }
 public:
  int tools() const override { return TOOL_TILT | TOOL_FINGER; }
  Txt task() const override { return {"Help the bat fall asleep", "Hilf der Fledermaus beim Einschlafen"}; }
  Txt hint() const override { return {"Bats only sleep upside down. Turn the device over – or hang it under the branch.", "Fledermäuse schlafen nur kopfüber. Dreh das Gerät um – oder häng sie unter den Ast."}; }
  void reset() override { bat.set(CX, standY()); flipT = 0; hanging = false; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.2f);
    if (hanging) return;
    flipT = f.tiltY < -0.5f ? flipT + f.dt : 0;
    if (flipT > 0.5f) { hanging = true; bat.set(CX, hangY()); }
    if (!hanging && bat.grab(f, 60)) {
      bat.move(f, 90, 120, W - 90, 320);
      if (bat.drop(f)) {
        if (bat.y > BRY + 10) { hanging = true; bat.set(CX, hangY()); }
        else bat.set(CX, standY());
      }
    } else if (f.tap && !hanging) h.say(CX, 120, {"I can't sleep like this…", "So kann ich nicht schlafen…"});
    if (hanging) { win.start(f); h.sfx(SFX_WIN); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    // Mond und Ast
    s.circle(300, 140, 26, YELLOW);
    s.circle(312, 132, 24, WHITE);
    s.capsule(24, BRY, 344, BRY - 4, 8, G5);
    s.capsule(300, BRY - 4, 336, BRY - 30, 3.5f, G5);
    s.capsule(60, BRY, 36, BRY + 22, 3.5f, G5);
    for (float lx : {336.f, 30.f}) s.ellipse(lx, lx > 100 ? BRY - 34 : BRY + 26, 12, 7, G4, G5);
    const float sleep = hanging ? 1 : 0;
    drawBat(s, bat.x, bat.y, K * (bat.on ? 1.05f : 1), sleep, hanging);
    if (hanging) zzz(s, CX + 50, hangY() + 10, f.t, CYAN);
    for (int i = 0; i < 3; i++) sparkle(s, 60 + i * 50, 130 + (i % 2) * 22, 6, INK, 1, i);
  }
};


// ======================= 13–36: harte Rätsel =======================
// Quiz: die Frage ist der Aufgabentext. Antwort über die Zahlentastatur (answer) oder Auswahlknöpfe (opt).
// Falsche Antworten sperren immer länger. Mit showFor gibt es vorher eine Merk-Phase, die nach einem Fehler wiederkommt.
struct QuizSpec {
  Txt q, hint;
  const char* answer;          // Ziffernfolge; nullptr: Auswahl
  int n, correct;              // Auswahl
  Txt opt[4];
  const char* trap;            // typische Falschantwort …
  Txt trapSay;                 // … mit eigenem Zuruf
  void (*art)(Scene&, float t, float won, int lang);
  float showFor;
};

class Quiz : public Level {
  const QuizSpec& q;
  Win win;
  NumPad pad;
  Lock lock;
  float phase0 = 0, wrongT = -10;
  int picked = -1, wrong = -1;
  static constexpr float GW = 152, GH = 42, GY0 = 240, GY1 = 290;
  void box(int i, float& x, float& y) const {
    const bool alone = q.n == 3 && i == 2;
    x = alone ? CX - GW / 2 : i % 2 ? CX + 6 : CX - 6 - GW;
    y = i / 2 ? GY1 : GY0;
  }
  void fail(const Frame& f, Host& h, const char* given) {
    lock.fail(f, kids);
    pad.wrong(f);
    wrongT = f.t;
    if (q.showFor > 0) phase0 = lock.until;
    if (q.trap && given && !std::strcmp(given, q.trap)) { h.say(CX, NP_DY - 30, q.trapSay); h.sfx(SFX_NOPE); }
    else h.nope(CX, NP_DY - 30);
  }

 public:
  explicit Quiz(const QuizSpec& s) : q(s) {}
  Txt task() const override { return q.q; }
  Txt hint() const override { return q.hint; }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return q.answer; }
  bool hasPad() const override { return q.answer != nullptr; }
  void reset() override { win.reset(); pad.clear(); lock.reset(); phase0 = 0; picked = wrong = -1; wrongT = -10; }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on()) return;
    if (f.t - phase0 < q.showFor || lock.on(f)) return;
    if (q.answer) {
      if (pad.tick(f, h, false)) {
        if (pad.is(q.answer)) { win.start(f); h.sfx(SFX_WIN); }
        else { char given[10]; std::strcpy(given, pad.buf); fail(f, h, given); }
      }
      return;
    }
    if (!f.tap) return;
    for (int i = 0; i < q.n; i++) {
      float x, y;
      box(i, x, y);
      if (f.x < x - 4 || f.x > x + GW + 4 || f.y < y - 4 || f.y > y + GH + 4) continue;
      if (i == q.correct) { picked = i; win.start(f); h.sfx(SFX_WIN); }
      else { wrong = i; fail(f, h, nullptr); }
    }
  }
  void draw(Scene& s, const Frame& f, int lang) override {
    const float won = win.on() ? ease(win.since(f) / 0.5f) : 0;
    const float tt = f.t - phase0;
    if (q.art) q.art(s, tt, won, lang);
    if (q.showFor > 0 && tt >= 0 && tt < q.showFor) return;   // Merk-Phase: noch keine Eingabe
    if (q.answer) pad.draw(s, f, lock, win.on());
    else {
      const bool locked = lock.on(f);
      for (int i = 0; i < q.n; i++) {
        float x, y;
        box(i, x, y);
        const bool ok = picked == i;
        const float sh = (i == wrong && f.t - wrongT < 0.4f) ? std::sin((f.t - wrongT) * 50) * 4 : 0;
        const float a = locked ? 0.35f : 1;
        s.rrect(x + sh, y, GW, GH, 12, ok ? INK : BTN, a);
        const char* t = tr(q.opt[i], lang);
        const auto& fnt = Scene::textW(fonts::heavy, t) <= GW - 14 ? fonts::heavy : fonts::body;
        s.text(fnt, t, x + GW / 2 + sh, y + (&fnt == &fonts::heavy ? 5 : 9), ok ? WHITE : INK, a, Align::Center);
      }
      if (locked) {
        char b[16];
        std::snprintf(b, sizeof b, "%d s", int(std::ceil(lock.left(f))));
        s.text(fonts::heavy, b, CX, GY1 + GH + 6, G4, 1, Align::Center);
      }
    }
    if (win.on()) for (int i = 0; i < 4; i++) sparkle(s, 50 + i * 90, 210 + (i % 2) * 18, 9, i % 2 ? CYAN : INK, won, f.t * 2 + i);
  }
};

// ---- Bilder ----
void artChain(Scene& s, float t, float won, int lang) {
  static const char* OP[6] = {"", "×", "–", ":", "+", "×"};
  static const char* NUM[6] = {"7", "6", "9", "3", "15", "2"};
  const float cy = 160;
  if (t < 0) { s.text(fonts::heavy, "…", CX, cy - 16, G3, 1, Align::Center); return; }
  const int i = int(t / 1.3f);
  if (i < 6) {
    const float u = std::fmod(t, 1.3f) / 1.3f;
    s.circle(CX, cy, 56 + 4 * std::sin(u * PIF), i % 2 ? YELLOW : MINT);
    const float nw = Scene::textW(fonts::huge, NUM[i]), ow = OP[i][0] ? Scene::textW(fonts::heavy, OP[i]) + 8 : 0;
    const float x0 = CX - (nw + ow) / 2;
    if (ow > 0) s.text(fonts::heavy, OP[i], x0, cy - 16, INK);
    s.text(fonts::huge, NUM[i], x0 + ow, cy - 28, INK);
    for (int k = 0; k < 6; k++) s.circle(CX - 40 + k * 16, 212, 4, k <= i ? INK : G2);
  } else {
    s.circle(CX, cy, 56, won > 0 ? CYAN : G1);
    s.text(fonts::huge, "?", CX, cy - 28, won > 0 ? INK : G3, 1, Align::Center);
    (void)lang;
  }
}

void artMagic(Scene& s, float t, float won, int) {
  static const char* N[9] = {"4", "9", "2", "3", "5", "7", "8", "1", "?"};
  const float c = 38, g = 4, x0 = CX - (3 * c + 2 * g) / 2, y0 = 104;
  for (int i = 0; i < 9; i++) {
    const float x = x0 + (i % 3) * (c + g), y = y0 + (i / 3) * (c + g);
    const bool q = i == 8;
    s.rrect(x, y, c, c, 8, q ? (won > 0 ? CYAN : YELLOW) : G1);
    s.text(fonts::num, q && won > 0 ? "6" : N[i], x + c / 2, y + 6, INK, 1, Align::Center);
  }
  (void)t;
}

void artChess(Scene& s, float t, float won, int) {
  const float c = 11, x0 = CX - 4 * c, y0 = 128;
  s.rrect(x0 - 4, y0 - 4, 8 * c + 8, 8 * c + 8, 4, G5);
  for (int i = 0; i < 64; i++) s.rrect(x0 + (i % 8) * c, y0 + (i / 8) * c, c, c, 0, (i % 8 + i / 8) % 2 ? G4 : hex(0xF4F4F4));
  const int k = 1 + int(t * 0.8f) % 8;   // ein wachsendes Quadrat zeigt, dass alle Größen zählen
  s.capsule(x0, y0, x0 + k * c, y0, 1.6f, won > 0 ? CYAN : HOTPINK);
  s.capsule(x0, y0 + k * c, x0 + k * c, y0 + k * c, 1.6f, won > 0 ? CYAN : HOTPINK);
  s.capsule(x0, y0, x0, y0 + k * c, 1.6f, won > 0 ? CYAN : HOTPINK);
  s.capsule(x0 + k * c, y0, x0 + k * c, y0 + k * c, 1.6f, won > 0 ? CYAN : HOTPINK);
}

const QuizSpec Q_BAT{
    {"A bat and a ball cost €1.10 in total. The bat costs €1 more than the ball. How many cents does the ball cost?",
     "Schläger und Ball kosten zusammen 1,10 €. Der Schläger kostet 1 € mehr als der Ball. Wie viele Cent kostet der Ball?"},
    {"Ball = x, bat = x + 1. Together: 2x + 1 = 1.10.", "Ball = x, Schläger = x + 1. Zusammen: 2x + 1 = 1,10."},
    "5", 0, 0, {}, "10", {"Then the bat would cost €1.10!", "Dann kostet der Schläger 1,10 €!"}, nullptr, 0};
const QuizSpec Q_MACHINES{
    {"5 machines need 5 minutes to make 5 parts. How many minutes do 100 machines need for 100 parts?",
     "5 Maschinen brauchen 5 Minuten für 5 Teile. Wie viele Minuten brauchen 100 Maschinen für 100 Teile?"},
    {"How long does ONE machine need for ONE part?", "Wie lange braucht EINE Maschine für EIN Teil?"},
    "5", 0, 0, {}, "100", {"Not so fast!", "Nicht so schnell!"}, nullptr, 0};
const QuizSpec Q_LILY{
    {"Water lilies double their area every day. After 48 days the lake is covered. After how many days was half of it covered?",
     "Seerosen verdoppeln ihre Fläche jeden Tag. Nach 48 Tagen ist der See bedeckt. Nach wie vielen Tagen war er zur Hälfte bedeckt?"},
    {"Think backwards from day 48.", "Denk vom Tag 48 aus rückwärts."},
    "47", 0, 0, {}, "24", {"Doubling, not even growth!", "Verdoppeln, nicht gleichmäßig!"}, nullptr, 0};
const QuizSpec Q_SOCKS{
    {"10 black and 10 white socks lie in the dark. How many must you take to be sure of having TWO BLACK ones?",
     "Im Dunkeln liegen 10 schwarze und 10 weiße Socken. Wie viele musst du ziehen, um sicher ZWEI SCHWARZE zu haben?"},
    {"Worst case: you draw all the white ones first.", "Schlimmster Fall: Du ziehst zuerst alle weißen."},
    "12", 0, 0, {}, "3", {"A pair – but black?", "Ein Paar – aber schwarz?"}, nullptr, 0};
const QuizSpec Q_SPEED{
    {"A car drives 60 km at 30 km/h and the same 60 km back at 90 km/h. What is its average speed in km/h?",
     "Ein Auto fährt 60 km mit 30 km/h hin und dieselben 60 km mit 90 km/h zurück. Wie hoch ist das Durchschnittstempo in km/h?"},
    {"Total distance 120 km. How many hours in total?", "Gesamtstrecke 120 km. Wie viele Stunden insgesamt?"},
    "45", 0, 0, {}, "60", {"Average the time, not the speeds!", "Rechne mit der Zeit, nicht mit den Tempi!"}, nullptr, 0};
const QuizSpec Q_BOXES{
    {"Three boxes: APPLES, PEARS, MIXED. Every label is wrong. From which box must you take one fruit to relabel all correctly?",
     "Drei Kisten: ÄPFEL, BIRNEN, GEMISCHT. Jedes Schild ist falsch. Aus welcher Kiste musst du eine Frucht ziehen, um alle richtig zu beschriften?"},
    {"The MIXED box is not mixed – it holds only one kind.", "Die Kiste GEMISCHT ist nicht gemischt – sie enthält nur eine Sorte."},
    nullptr, 3, 2, {{"APPLES", "ÄPFEL"}, {"PEARS", "BIRNEN"}, {"MIXED", "GEMISCHT"}}, nullptr, {}, nullptr, 0};
const QuizSpec Q_AGE{
    {"A father is four times as old as his son. In 20 years he will be only twice as old. How old is the father today?",
     "Ein Vater ist viermal so alt wie sein Sohn. In 20 Jahren ist er nur noch doppelt so alt. Wie alt ist der Vater heute?"},
    {"Son = s: 4s + 20 = 2 × (s + 20)", "Sohn = s: 4s + 20 = 2 × (s + 20)"},
    "40", 0, 0, {}, nullptr, {}, nullptr, 0};
const QuizSpec Q_CHAIN{
    {"Calculate along in your head!", "Rechne im Kopf mit!"},
    {"7 × 6 = 42, then step by step. Restart shows it again.", "7 × 6 = 42, dann Schritt für Schritt. Neustart zeigt es noch einmal."},
    "52", 0, 0, {}, nullptr, {}, artChain, 7.8f};
const QuizSpec Q_KNIGHTS{
    {"Knights always tell the truth, knaves always lie. A says: “We are both knaves.” How many of A and B are knights?",
     "Ritter sagen immer die Wahrheit, Schurken lügen immer. A sagt: „Wir sind beide Schurken.“ Wie viele von A und B sind Ritter?"},
    {"Could A be a knight? Then the sentence would be true …", "Kann A ein Ritter sein? Dann wäre der Satz wahr …"},
    "1", 0, 0, {}, "0", {"Then A would be telling the truth …", "Dann hätte A ja die Wahrheit gesagt …"}, nullptr, 0};
const QuizSpec Q_CUCUMBER{
    {"1 kg of cucumbers is 99 % water. After drying, the water is only 98 %. How many grams do they weigh now?",
     "1 kg Gurken bestehen zu 99 % aus Wasser. Nach dem Trocknen sind es nur noch 98 %. Wie viele Gramm wiegen sie jetzt?"},
    {"10 g are not water. After drying they make up 2 % of the total.", "10 g sind kein Wasser. Nach dem Trocknen sind sie 2 % vom Ganzen."},
    "500", 0, 0, {}, "990", {"Look at the dry mass!", "Achte auf die Trockenmasse!"}, nullptr, 0};
const QuizSpec Q_WEEKDAY{
    {"Today is Wednesday. Which weekday is it in 100 days? (1 = Monday … 7 = Sunday)",
     "Heute ist Mittwoch. Welcher Wochentag ist in 100 Tagen? (1 = Montag … 7 = Sonntag)"},
    {"100 days = 14 weeks + 2 days.", "100 Tage = 14 Wochen + 2 Tage."},
    "5", 0, 0, {}, nullptr, {}, nullptr, 0};
const QuizSpec Q_SNAIL{
    {"A snail sits in a 10 m deep well. By day it climbs 3 m, at night it slides back 2 m. On which day does it reach the top?",
     "Eine Schnecke sitzt in einem 10 m tiefen Brunnen. Tagsüber klettert sie 3 m, nachts rutscht sie 2 m ab. An welchem Tag ist sie oben?"},
    {"After 7 days and nights it is at 7 m …", "Nach 7 Tagen und Nächten ist sie bei 7 m …"},
    "8", 0, 0, {}, "10", {"On the last day it doesn't slide back!", "Am letzten Tag rutscht sie nicht mehr ab!"}, nullptr, 0};
const QuizSpec Q_MAGIC{
    {"Which number is missing?", "Welche Zahl fehlt?"},
    {"Add up every row, column and diagonal.", "Addiere jede Zeile, Spalte und Diagonale."},
    "6", 0, 0, {}, nullptr, {}, artMagic, 0};
const QuizSpec Q_ONES{
    {"You write down all numbers from 1 to 100. How many times do you write the digit 1?",
     "Du schreibst alle Zahlen von 1 bis 100 auf. Wie oft schreibst du dabei die Ziffer 1?"},
    {"Count units digits, tens digits – and 100.", "Zähl Einer, Zehner – und die 100."},
    "21", 0, 0, {}, "20", {"Don't forget 100!", "Vergiss die 100 nicht!"}, nullptr, 0};
const QuizSpec Q_MONTY{
    {"Three doors, a car behind one. You pick door 1, the host opens door 3: a goat. Your chance of winning in % if you switch? (rounded)",
     "Drei Türen, hinter einer ein Auto. Du wählst Tür 1, der Moderator öffnet Tür 3: Ziege. Gewinnchance in %, wenn du wechselst? (gerundet)"},
    {"Your first pick wins in only 1 of 3 cases.", "Deine erste Wahl gewinnt nur in 1 von 3 Fällen."},
    "67", 0, 0, {}, "50", {"The host knows where the car is!", "Der Moderator weiß, wo das Auto ist!"}, nullptr, 0};
const QuizSpec Q_CHESS{
    {"How many squares of all sizes are on a chessboard?", "Wie viele Quadrate aller Größen hat ein Schachbrett?"},
    {"1×1: 64, 2×2: 49, 3×3: 36 … add up the square numbers.", "1×1: 64, 2×2: 49, 3×3: 36 … zähl die Quadratzahlen zusammen."},
    "204", 0, 0, {}, "64", {"There are bigger ones too!", "Es gibt auch größere!"}, artChess, 0};
const QuizSpec Q_ROPE{
    {"A rope lies tight around the equator. You make it 1 m longer and lift it evenly all round. How many cm gap appear? (rounded)",
     "Ein Seil liegt straff um den Äquator. Es wird 1 m länger und überall gleich angehoben. Wie viele cm Abstand entstehen? (gerundet)"},
    {"The circumference grows by 2π × height: 2π × h = 100 cm.", "Der Umfang wächst um 2π × Höhe: 2π × h = 100 cm."},
    "16", 0, 0, {}, "0", {"Surprisingly much!", "Überraschend viel!"}, nullptr, 0};
const QuizSpec Q_CLOCK{
    {"How often do the hour and minute hands cover each other between noon and midnight? (not counting 12:00 itself)",
     "Wie oft stehen Stunden- und Minutenzeiger genau übereinander, zwischen 12 Uhr mittags und Mitternacht? (12:00 nicht mitgezählt)"},
    {"Between 11 and 1 o'clock it happens only at 12:00.", "Zwischen 11 und 1 Uhr passiert es nur um 12:00."},
    "10", 0, 0, {}, "11", {"Without 12:00!", "Ohne 12:00!"}, nullptr, 0};
const QuizSpec Q_LETTERS{
    {"How many letters does the answer to this question have?", "Wie viele Buchstaben hat die Antwort auf diese Frage?"},
    {"Write the number out as a word.", "Schreib die Zahl als Wort."},
    "4", 0, 0, {}, nullptr, {}, nullptr, 0};
const QuizSpec Q_LOOKSAY{
    {"What comes next? 1 – 11 – 21 – 1211 – 111221 – ?", "Was kommt als Nächstes? 1 – 11 – 21 – 1211 – 111221 – ?"},
    {"Read each number aloud: 1211 is “one 1, one 2, two 1s”.", "Lies jede Zahl laut vor: 1211 ist „eine 1, eine 2, zwei 1“."},
    "312211", 0, 0, {}, nullptr, {}, nullptr, 0};

// ---- Kinder (13–24): kurze Denkfallen, kleine Zahlen ----
void artSeries(Scene& s, float t, float won, int) {
  static const char* N[5] = {"2", "4", "6", "?", "10"};
  const float r = 26, g = 8, x0 = CX - (5 * 2 * r + 4 * g) / 2 + r;
  for (int i = 0; i < 5; i++) {
    const bool q = i == 3;
    const float x = x0 + i * (2 * r + g), y = 172 + (q ? 0 : 3 * std::sin(t * 3 + i));
    s.circle(x, y, r, q ? (won > 0 ? CYAN : YELLOW) : i % 2 ? MINT : PINK);
    s.text(fonts::num, q && won > 0 ? "8" : N[i], x, y - 13, INK, 1, Align::Center);
  }
}

// Zahlenreihen mit Lücke an Stelle 4: {a, b, c, ?, e}, Lösung in [3]
constexpr int SERIES[5][5] = {{2, 4, 6, 8, 10}, {5, 10, 15, 20, 25}, {1, 3, 5, 7, 9}, {10, 8, 6, 4, 2}, {1, 2, 4, 8, 16}};
template <int ID>
void artSeriesN(Scene& s, float t, float won, int) {
  const float r = 26, g = 8, x0 = CX - (5 * 2 * r + 4 * g) / 2 + r;
  for (int i = 0; i < 5; i++) {
    const bool q = i == 3;
    const float x = x0 + i * (2 * r + g), y = 172 + (q ? 0 : 3 * std::sin(t * 3 + i));
    s.circle(x, y, r, q ? (won > 0 ? CYAN : YELLOW) : i % 2 ? MINT : PINK);
    char b[4];
    std::snprintf(b, sizeof b, "%d", SERIES[ID][i]);
    s.text(fonts::num, q && won <= 0 ? "?" : b, x, y - 13, INK, 1, Align::Center);
  }
}

const QuizSpec K_ROOSTER{
    {"A rooster lays an egg right on the tip of a roof. Which way does it roll?", "Ein Hahn legt ein Ei genau auf die Spitze vom Dach. Auf welche Seite rollt es?"},
    {"Who lays eggs – the rooster or the hen?", "Wer legt eigentlich Eier – der Hahn oder die Henne?"},
    nullptr, 3, 2, {{"LEFT", "LINKS"}, {"RIGHT", "RECHTS"}, {"NEITHER", "GAR NICHT"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_COMB{
    {"What has teeth but cannot bite?", "Was hat Zähne, kann aber nicht beißen?"},
    {"You use it on your hair.", "Du brauchst es für deine Haare."},
    nullptr, 3, 0, {{"COMB", "KAMM"}, {"SHARK", "HAI"}, {"DOG", "HUND"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_SPIDER{
    {"How many legs does a spider have?", "Wie viele Beine hat eine Spinne?"},
    {"More than a beetle. Beetles have 6.", "Mehr als ein Käfer. Käfer haben 6."},
    "8", 0, 0, {}, "6", {"That's a beetle!", "Das ist ein Käfer!"}, nullptr, 0};
const QuizSpec K_SERIES5{{"Which number is missing?", "Welche Zahl fehlt?"}, {"Count in steps of 5.", "Zähl in Fünferschritten."}, "20", 0, 0, {}, nullptr, {}, artSeriesN<1>, 0};
const QuizSpec K_APPLES{
    {"There are 5 apples on the table. You take 3 of them. How many apples do YOU have?", "Auf dem Tisch liegen 5 Äpfel. Du nimmst 3 davon. Wie viele Äpfel hast DU?"},
    {"Not how many are left – how many are in your hands?", "Nicht wie viele übrig sind – wie viele hast du in der Hand?"},
    "3", 0, 0, {}, "2", {"You took 3!", "Du hast doch 3 genommen!"}, nullptr, 0};
const QuizSpec K_COCOA{
    {"Which hand is best for stirring your hot chocolate?", "Mit welcher Hand rührst du den Kakao am besten um?"},
    {"With your hand it gets hot and sticky …", "Mit der Hand wird es heiß und klebrig …"},
    nullptr, 3, 2, {{"LEFT", "LINKS"}, {"RIGHT", "RECHTS"}, {"A SPOON", "LÖFFEL"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_BUS{
    {"A bus has 4 wheels. How many wheels do 3 buses have?", "Ein Bus hat 4 Räder. Wie viele Räder haben 3 Busse?"},
    {"4 + 4 + 4", "4 + 4 + 4"},
    "12", 0, 0, {}, "7", {"Not plus – three times!", "Nicht plus – dreimal!"}, nullptr, 0};
const QuizSpec K_SHORTEST{
    {"Lisa is taller than Tom. Tom is taller than Ben. Who is the shortest?", "Lisa ist größer als Tom. Tom ist größer als Ben. Wer ist am kleinsten?"},
    {"Line them up: Lisa, then Tom, then …", "Stell sie in eine Reihe: Lisa, dann Tom, dann …"},
    nullptr, 3, 2, {{"LISA", "LISA"}, {"TOM", "TOM"}, {"BEN", "BEN"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_SERIES_ODD{{"Which number is missing?", "Welche Zahl fehlt?"}, {"Always +2.", "Immer +2."}, "7", 0, 0, {}, nullptr, {}, artSeriesN<2>, 0};
const QuizSpec K_DAYS{
    {"Today is Monday. What day is the day after tomorrow?", "Heute ist Montag. Welcher Tag ist übermorgen?"},
    {"Tomorrow is Tuesday. And the day after that?", "Morgen ist Dienstag. Und der Tag danach?"},
    nullptr, 3, 1, {{"TUESDAY", "DIENSTAG"}, {"WEDNESDAY", "MITTWOCH"}, {"THURSDAY", "DONNERSTAG"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_ODD{
    {"Which one does not belong?", "Was gehört nicht dazu?"},
    {"Three of them grow on trees.", "Drei davon wachsen am Baum."},
    nullptr, 4, 2, {{"APPLE", "APFEL"}, {"PEAR", "BIRNE"}, {"CARROT", "KAROTTE"}, {"BANANA", "BANANE"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_RABBITS{
    {"6 rabbits are in a meadow. 2 hop away, then 3 more come. How many are there now?", "Auf einer Wiese sind 6 Hasen. 2 hoppeln weg, dann kommen 3 dazu. Wie viele sind es jetzt?"},
    {"6 – 2, then + 3.", "6 – 2, dann + 3."},
    "7", 0, 0, {}, nullptr, {}, nullptr, 0};
const QuizSpec K_CUBE{
    {"How many corners does a dice have?", "Wie viele Ecken hat ein Würfel?"},
    {"4 corners on top, and how many at the bottom?", "Oben 4 Ecken, und unten?"},
    "8", 0, 0, {}, "4", {"Look underneath too!", "Schau auch unten nach!"}, nullptr, 0};
const QuizSpec K_SERIES_DOWN{{"Which number is missing?", "Welche Zahl fehlt?"}, {"It goes down: always –2.", "Es wird weniger: immer –2."}, "4", 0, 0, {}, nullptr, {}, artSeriesN<3>, 0};
const QuizSpec K_PENGUIN{
    {"Which bird cannot fly?", "Welcher Vogel kann nicht fliegen?"},
    {"It lives where it is very cold and swims instead.", "Er lebt da, wo es sehr kalt ist, und schwimmt lieber."},
    nullptr, 4, 0, {{"PENGUIN", "PINGUIN"}, {"EAGLE", "ADLER"}, {"SPARROW", "SPATZ"}, {"OWL", "EULE"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_RIDDLE{
    {"I am thinking of a number. Double it and add 2, and you get 10. What is my number?", "Ich denke mir eine Zahl. Verdoppelt und plus 2 ergibt das 10. Welche Zahl ist es?"},
    {"Go backwards: 10 – 2, then halve it.", "Rechne rückwärts: 10 – 2, dann die Hälfte."},
    "4", 0, 0, {}, "8", {"Halve it too!", "Noch halbieren!"}, nullptr, 0};
const QuizSpec K_MUM{
    {"Dad is 35 years old. Mum is 3 years younger. How old is Mum?", "Papa ist 35 Jahre alt. Mama ist 3 Jahre jünger. Wie alt ist Mama?"},
    {"Younger means fewer years.", "Jünger heißt weniger Jahre."},
    "32", 0, 0, {}, "38", {"Younger means less!", "Jünger heißt weniger!"}, nullptr, 0};
const QuizSpec K_SERIES_DOUBLE{{"Which number is missing?", "Welche Zahl fehlt?"}, {"Each number is double the one before.", "Jede Zahl ist doppelt so groß wie die davor."}, "8", 0, 0, {}, "10", {"Not +2 – double!", "Nicht +2 – verdoppeln!"}, artSeriesN<4>, 0};

const QuizSpec K_SHEEP{
    {"A farmer has 5 sheep. All but 3 run away. How many sheep are left?", "Ein Bauer hat 5 Schafe. Alle bis auf 3 laufen weg. Wie viele Schafe bleiben übrig?"},
    {"“All but 3” means: 3 stay.", "„Alle bis auf 3“ heißt: 3 bleiben da."},
    "3", 0, 0, {}, "2", {"Read it again: all BUT 3!", "Lies nochmal: alle BIS AUF 3!"}, nullptr, 0};
const QuizSpec K_FEATHERS{
    {"What is heavier: 1 kg of feathers or 1 kg of stones?", "Was ist schwerer: 1 kg Federn oder 1 kg Steine?"},
    {"Look at the numbers, not at the things.", "Schau auf die Zahlen, nicht auf die Sachen."},
    nullptr, 3, 2, {{"FEATHERS", "FEDERN"}, {"STONES", "STEINE"}, {"SAME", "GLEICH"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_LEGS{
    {"How many legs do 3 chickens and 2 dogs have together?", "Wie viele Beine haben 3 Hühner und 2 Hunde zusammen?"},
    {"A chicken has 2 legs, a dog has 4.", "Ein Huhn hat 2 Beine, ein Hund hat 4."},
    "14", 0, 0, {}, "20", {"Chickens only have 2 legs!", "Hühner haben nur 2 Beine!"}, nullptr, 0};
const QuizSpec K_RACE{
    {"In a race you overtake the runner in second place. Which place are you in now?", "Beim Wettrennen überholst du den Zweiten. Auf welchem Platz bist du jetzt?"},
    {"You take HIS place. Who is still in front of you?", "Du nimmst SEINEN Platz ein. Wer ist noch vor dir?"},
    "2", 0, 0, {}, "1", {"The winner is still ahead!", "Der Erste ist noch vor dir!"}, nullptr, 0};
const QuizSpec K_SERIES{
    {"Which number is missing?", "Welche Zahl fehlt?"},
    {"Look how much it grows each time.", "Schau, um wie viel es jedes Mal größer wird."},
    "8", 0, 0, {}, nullptr, {}, artSeries, 0};
const QuizSpec K_EGGS{
    {"One egg takes 5 minutes to boil. How many minutes do 3 eggs take in the same pot?", "Ein Ei kocht 5 Minuten. Wie viele Minuten brauchen 3 Eier im selben Topf?"},
    {"They are all in the pot at the same time!", "Sie sind alle gleichzeitig im Topf!"},
    "5", 0, 0, {}, "15", {"They all cook at the same time!", "Die kochen doch alle gleichzeitig!"}, nullptr, 0};
const QuizSpec K_TOWEL{
    {"What gets wetter the more it dries?", "Was wird nasser, je mehr es trocknet?"},
    {"What do you use after a bath?", "Was nimmst du nach dem Baden?"},
    nullptr, 3, 0, {{"TOWEL", "HANDTUCH"}, {"SUN", "SONNE"}, {"WIND", "WIND"}}, nullptr, {}, nullptr, 0};
const QuizSpec K_MONTHS{
    {"How many months have 28 days?", "Wie viele Monate haben 28 Tage?"},
    {"Not only February has 28 days …", "Nicht nur der Februar hat 28 Tage …"},
    "12", 0, 0, {}, "1", {"The others have 28 days too – and more!", "Die anderen haben auch 28 Tage – und mehr!"}, nullptr, 0};
const QuizSpec K_TOM{
    {"Tom's mother has three children: Mia, Leo and …?", "Toms Mutter hat drei Kinder: Mia, Leo und …?"},
    {"Read the very first word again.", "Lies das allererste Wort nochmal."},
    nullptr, 3, 1, {{"BEN", "BEN"}, {"TOM", "TOM"}, {"LISA", "LISA"}}, nullptr, {}, nullptr, 0};

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

// ======================= Merk dir die Zahl (Erwachsene: 7 Ziffern, 2 s; Kinder: 4 Ziffern, 3 s) =======================
class Memory7 : public Level {
  Win win;
  NumPad pad;
  Lock lock;
  char num[8] = {};
  float phase0 = 0;
  uint32_t seed = 0x5EED;
  const int len;
  const float SHOW;
  void fresh() {
    num[0] = char('1' + lcg(seed) % 9);
    for (int i = 1; i < len; i++) num[i] = char('0' + lcg(seed) % 10);
    num[len] = 0;
  }
 public:
  explicit Memory7(int digits = 7, float show = 2.0f) : len(digits), SHOW(show) {}
  Txt task() const override {
    if (len < 7) return {"Remember the number! (3 seconds)", "Merk dir die Zahl! (3 Sekunden)"};
    return {"Remember the number! (2 seconds)", "Merk dir die Zahl! (2 Sekunden)"};
  }
  Txt hint() const override {
    if (len < 7) return {"Say the digits quietly to yourself. Restart shows a new number.", "Sag die Ziffern leise vor dich hin. Neustart zeigt eine neue Zahl."};
    return {"Chunk it: 3 + 4 digits, like a phone number. Restart shows a new one.", "In Häppchen merken: 3 + 4 Ziffern, wie eine Telefonnummer. Neustart zeigt eine neue."};
  }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return num; }
  bool hasPad() const override { return true; }
  void reset() override { win.reset(); pad.clear(); lock.reset(); phase0 = 0.4f; seed = rnd(); fresh(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on() || f.t - phase0 < SHOW) return;
    if (pad.tick(f, h, lock.on(f))) {
      if (pad.is(num)) { win.start(f); h.sfx(SFX_WIN); }
      else { lock.fail(f, kids); pad.wrong(f); h.nope(CX, NP_DY - 30); fresh(); phase0 = lock.until; }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    const float tt = f.t - phase0;
    if (tt >= 0 && tt < SHOW) {
      s.rrect(CX - 140, 120, 280, 76, 20, INK);
      s.text(fonts::huge, num, CX, 132, WHITE, 1, Align::Center);
      s.capsule(CX - 110, 212, CX + 110 - 220 * (tt / SHOW), 212, 4, CYAN);
      return;
    }
    s.rrect(CX - 140, 120, 280, 76, 20, win.on() ? CYAN : BTN);
    static const char Q[] = "???????";
    s.text(fonts::huge, win.on() ? num : Q + 7 - len, CX, 132, win.on() ? INK : G3, 1, Align::Center);
    pad.draw(s, f, lock, win.on());
  }
};

// ======================= Stroop-Sprint =======================
class StroopSprint : public Level {
  Win win;
  int word = 0, ink = 1, streak = 0, wrongBtn = -1;
  float startT = -1, wrongT = -10, okT = -10;
  uint32_t seed = 0xC0FFEE;
  char ans[2] = {};
  const int NEED;       // Erwachsene 10 in 12 s, Kinder 6 in 15 s
  const float LIMIT;
  static constexpr float GW = 152, GH = 42, GY0 = 252, GY1 = 300;
  static const Txt& name(int i) {
    static const Txt N[4] = {{"RED", "ROT"}, {"BLUE", "BLAU"}, {"GREEN", "GRÜN"}, {"YELLOW", "GELB"}};
    return N[i];
  }
  static Col color(int i) { static const Col C[4] = {RED, BLUE, GREEN, AMBER}; return C[i]; }
  void next() {
    word = lcg(seed) % 4;
    ink = (word + 1 + lcg(seed) % 3) % 4;   // Farbe und Wort passen nie zusammen
    ans[0] = char('0' + ink);
  }
  void box(int i, float& x, float& y) const { x = i % 2 ? CX + 6 : CX - 6 - GW; y = i / 2 ? GY1 : GY0; }
 public:
  explicit StroopSprint(int need = 10, float limit = 12) : NEED(need), LIMIT(limit) {}
  Txt task() const override {
    if (NEED < 10) return {"Tap the INK COLOR: 6 in a row within 15 seconds!", "Tippe die SCHRIFTFARBE: 6 richtige in Folge in 15 s!"};
    return {"Tap the INK COLOR: 10 in a row within 12 seconds!", "Tippe die SCHRIFTFARBE: 10 richtige in Folge in 12 s!"};
  }
  Txt hint() const override { return {"Ignore what the word says. Only the color of the letters counts.", "Ignorier, was da steht. Nur die Farbe der Buchstaben zählt."}; }
  int tools() const override { return TOOL_FINGER | TOOL_THINK; }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); streak = 0; startT = -1; wrongT = okT = -10; wrongBtn = -1; seed = rnd(); next(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on()) return;
    if (startT >= 0 && f.t - startT > LIMIT) {
      streak = 0; startT = -1; wrongT = f.t; wrongBtn = -1;
      h.say(CX, 120, {"Too slow!", "Zu langsam!"}); h.sfx(SFX_NOPE);
      next();
    }
    if (!f.tap) return;
    for (int i = 0; i < 4; i++) {
      float x, y;
      box(i, x, y);
      if (f.x < x - 3 || f.x > x + GW + 3 || f.y < y - 3 || f.y > y + GH + 3) continue;
      if (i == ink) {
        if (streak == 0) startT = f.t;
        streak++; okT = f.t;
        h.sfx(SFX_POP);
        if (streak >= NEED) { win.start(f); h.sfx(SFX_WIN); }
        else next();
      } else {
        streak = 0; startT = -1; wrongT = f.t; wrongBtn = i;
        h.nope(x + GW / 2, y - 30);
        next();
      }
    }
  }
  void draw(Scene& s, const Frame& f, int lang) override {
    const float pop = f.t - okT < 0.15f ? 1.06f : 1;
    if (!win.on()) s.text(fonts::huge, tr(name(word), lang), CX, 140 - (pop - 1) * 40, color(ink), 1, Align::Center);
    else { char b[8]; std::snprintf(b, sizeof b, "%d/%d", NEED, NEED); s.text(fonts::huge, b, CX, 140, INK, 1, Align::Center); }
    if (startT >= 0 && !win.on()) {
      const float u = clampf((f.t - startT) / LIMIT, 0, 1);
      s.capsule(CX - 120, 206, CX + 120 - 240 * u, 206, 4, u > 0.75f ? HOTPINK : CYAN);
    }
    for (int i = 0; i < NEED; i++) s.circle(CX - (NEED - 1) * 9 + i * 18, 226, 5, i < streak || win.on() ? INK : G2);
    for (int i = 0; i < 4; i++) {
      float x, y;
      box(i, x, y);
      const float sh = (i == wrongBtn && f.t - wrongT < 0.4f) ? std::sin((f.t - wrongT) * 50) * 4 : 0;
      s.rrect(x + sh, y, GW, GH, 12, BTN);
      s.text(fonts::heavy, tr(name(i), lang), x + GW / 2 + sh, y + 5, INK, 1, Align::Center);
    }
  }
};

// ======================= Farbfolge nachtippen =======================
class Simon : public Level {
  Win win;
  Lock lock;
  int seq[7] = {}, pos = 0;
  float phase0 = 0, litT[4] = {-10, -10, -10, -10};
  uint32_t seed = 0xBEEF;
  char ans[8] = {};
  const int N;            // Erwachsene 7 Farben, Kinder 4 (und langsamer)
  const float STEP, ON;
  static constexpr float PW = 100, PH = 80;
  static Col color(int i) { static const Col C[4] = {CYAN, YELLOW, HOTPINK, MINT}; return C[i]; }
  static void pad(int i, float& x, float& y) { x = i % 2 ? CX + 4 : CX - 104; y = i / 2 ? 216 : 130; }
  bool playing(const Frame& f) const { return f.t - phase0 < N * STEP; }
 public:
  explicit Simon(int n = 7) : N(n), STEP(n < 7 ? 0.85f : 0.6f), ON(n < 7 ? 0.6f : 0.4f) {}
  Txt task() const override { return {"Watch, then repeat the color sequence", "Schau zu und tippe die Farbfolge nach"}; }
  Txt hint() const override {
    if (N < 7) return {"Say the colors quietly while watching: “blue, yellow …”.", "Sag die Farben beim Zuschauen leise mit: „blau, gelb …“."};
    return {"7 colors. Name them in your head while watching, e.g. “blue, yellow, pink …”.", "7 Farben. Sprich sie beim Zuschauen im Kopf mit, z. B. „blau, gelb, pink …“."};
  }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return ans; }
  void reset() override {
    win.reset(); lock.reset(); pos = 0; phase0 = 0.8f; seed = rnd();
    for (int i = 0; i < N; i++) { seq[i] = lcg(seed) % 4; ans[i] = char('0' + seq[i]); }
    ans[N] = 0;
    for (float& t : litT) t = -10;
  }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on() || f.t < phase0 || playing(f) || lock.on(f) || !f.pressed) return;
    for (int i = 0; i < 4; i++) {
      float x, y;
      pad(i, x, y);
      if (f.x < x || f.x > x + PW || f.y < y || f.y > y + PH) continue;
      litT[i] = f.t;
      if (i == seq[pos]) {
        h.sfx(SFX_POP);
        if (++pos == N) { win.start(f); h.sfx(SFX_WIN); }
      } else {
        h.nope(CX, 100);
        lock.fail(f, kids);
        pos = 0;
        phase0 = lock.until + 0.4f;
      }
    }
  }
  void draw(Scene& s, const Frame& f, int lang) override {
    const float tt = f.t - phase0;
    const int step = tt >= 0 ? int(tt / STEP) : -1;
    const bool on = step >= 0 && step < N && std::fmod(tt, STEP) < ON;
    for (int i = 0; i < 4; i++) {
      float x, y;
      pad(i, x, y);
      const bool lit = (on && seq[step] == i) || f.t - litT[i] < 0.25f || win.on();
      s.rrect(x, y, PW, PH, 18, color(i), lit ? 1.f : 0.28f);
      if (lit) s.glow(x + PW / 2, y + PH / 2, 70, color(i), 0.35f);
    }
    char b[32];
    if (lock.on(f)) std::snprintf(b, sizeof b, "%d s", int(std::ceil(lock.left(f))));
    else if (win.on()) std::snprintf(b, sizeof b, "%d/%d", N, N);
    else if (f.t < phase0 || playing(f)) std::snprintf(b, sizeof b, "%s", lang ? "Schau zu …" : "Watch …");
    else std::snprintf(b, sizeof b, "%s  %d/%d", lang ? "Du bist dran" : "Your turn", pos, N);
    s.text(fonts::heavy, b, CX, 306, lock.on(f) ? G4 : INK, 1, Align::Center);
  }
};

// ======================= Kugel-Labyrinth (kippen) =======================
class Maze : public Level {
  float bx = 0, by = 0, vx = 0, vy = 0;
  int falls = 0;
  float fallT = -10;
  Win win;
  static constexpr float R = 10, X0 = 20, Y0 = 104, X1 = 348, Y1 = 346;
  static constexpr float SX = 44, SY = 132, GX = 44, GY = 312;
  struct Box { float x0, y0, x1, y1; };
  static constexpr Box WALL{X0, 160, 296, 290};
  static constexpr float HOLES[4][2] = {{240, 148}, {150, 330}, {170, 118}, {322, 226}};   // Kinder: nur die ersten zwei
  static constexpr float HR = 12;
  const int nHoles;
  static bool hitBox(float x, float y, const Box& b) {
    const float cx = clampf(x, b.x0, b.x1), cy = clampf(y, b.y0, b.y1);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) < R * R;
  }
  static bool blocked(float x, float y) { return x < X0 + R || x > X1 - R || y < Y0 + R || y > Y1 - R || hitBox(x, y, WALL); }
 public:
  explicit Maze(int holes = 4) : nHoles(holes) {}
  Txt task() const override { return {"Roll the ball into the goal", "Roll die Kugel ins Ziel"}; }
  Txt hint() const override {
    if (nHoles < 4) return {"Tilt the whole device – very gently – and roll around the holes.", "Kipp das ganze Gerät – ganz sanft – und roll um die Löcher herum."};
    return {"Tilt the whole device – gently. In the last corridor, keep the ball close to the upper wall.", "Kipp das ganze Gerät – ganz sanft. Im letzten Gang die Kugel oben an der Wand halten."};
  }
  int tools() const override { return TOOL_TILT; }
  void reset() override { bx = SX; by = SY; vx = vy = 0; falls = 0; fallT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on()) return;
    const float ax = -f.tiltX * 900, ay = f.tiltY * 900;   // Schwerkraft in der Bildschirmebene
    const int sub = 4;
    const float dt = f.dt / sub;
    for (int i = 0; i < sub; i++) {
      vx = (vx + ax * dt) * (1 - 1.2f * dt);
      vy = (vy + ay * dt) * (1 - 1.2f * dt);
      const float sp = std::sqrt(vx * vx + vy * vy);
      if (sp > 520) { vx *= 520 / sp; vy *= 520 / sp; }
      if (!blocked(bx + vx * dt, by)) bx += vx * dt; else vx *= -0.25f;
      if (!blocked(bx, by + vy * dt)) by += vy * dt; else vy *= -0.25f;
      for (int k = 0; k < nHoles; k++)
        if (const float* hole = HOLES[k]; dist(bx, by, hole[0], hole[1]) < HR) {
          falls++; fallT = f.t;
          h.sfx(SFX_NOPE);
          h.say(hole[0], hole[1] - 30, {"Plop!", "Plumps!"});
          bx = SX; by = SY; vx = vy = 0;
          return;
        }
      if (dist(bx, by, GX, GY) < 14) { bx = GX; by = GY; vx = vy = 0; win.start(f); h.sfx(SFX_WIN); return; }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.rrect(X0 - 6, Y0 - 6, X1 - X0 + 12, Y1 - Y0 + 12, 14, G4);
    s.rrect(X0, Y0, X1 - X0, Y1 - Y0, 10, hex(0xF6F1E8));
    s.rrect(WALL.x0, WALL.y0, WALL.x1 - WALL.x0, WALL.y1 - WALL.y0, 8, G4);
    s.rrect(WALL.x0 + 4, WALL.y0 + 4, WALL.x1 - WALL.x0 - 8, WALL.y1 - WALL.y0 - 8, 6, G3);
    for (int k = 0; k < nHoles; k++) { s.circle(HOLES[k][0], HOLES[k][1], HR + 2, G4); s.circle(HOLES[k][0], HOLES[k][1], HR, G6); }
    s.circle(GX, GY, 17, win.on() ? CYAN : YELLOW);
    s.circle(GX, GY, 12, G6);
    if (!win.on()) {
      s.circle(bx + 2, by + 3, R, G4, 0.5f);
      s.circle(bx, by, R, hex(0xC9CED6));
      s.circle(bx - 3, by - 3, 3.5f, WHITE, 0.9f);
    } else {
      rays(s, GX, GY, 24, 34, 9, 2.5f, YELLOW, 1, f.t);
    }
    (void)falls; (void)fallT;
  }
};

// ======================= Kleine (unter 5): klare Aufgaben, große Ziele =======================
// Ein bekanntes Level mit eigener, ganz direkter Aufgabe (wird vorgelesen)
class Retext : public Level {
  Level* in;
  Txt t, hn;
 public:
  Retext(Level* l, Txt task, Txt hint) : in(l), t(task), hn(hint) {}
  ~Retext() override { delete in; }
  Txt task() const override { return t; }
  Txt hint() const override { return hn; }
  void reset() override { in->reset(); }
  void tick(const Frame& f, Host& h) override { in->tick(f, h); }
  void draw(Scene& s, const Frame& f, int lang) override { in->draw(s, f, lang); }
  bool ownsTask() const override { return in->ownsTask(); }
  int tools() const override { return in->tools(); }
};

const Txt COLOR_NAMES[4] = {{"That's red!", "Das ist rot!"}, {"That's blue!", "Das ist blau!"}, {"That's yellow!", "Das ist gelb!"}, {"That's green!", "Das ist grün!"}};
const Col COLORS4[4] = {RED, BLUE, hex(0xFFD400), GREEN};

// Tippe auf das Bonbon in Farbe c
class TapColor : public Level {
  Win win;
  int want, hit = -1;
  float wrongT = -10;
  int wrongI = -1;
  static constexpr float P[4][2] = {{110, 160}, {258, 160}, {110, 272}, {258, 272}};
  int perm[4] = {1, 2, 0, 3};   // Farbe je Platz, bei jedem Spiel neu gemischt
  char ans[2] = {};
  const int* order() const { return perm; }
 public:
  explicit TapColor(int color) : want(color) {}
  Txt task() const override {
    static const Txt T[4] = {{"Tap the red candy", "Tippe auf das rote Bonbon"}, {"Tap the blue candy", "Tippe auf das blaue Bonbon"},
                             {"Tap the yellow candy", "Tippe auf das gelbe Bonbon"}, {"Tap the green candy", "Tippe auf das grüne Bonbon"}};
    return T[want];
  }
  Txt hint() const override {
    static const Txt T[4] = {{"Red like a strawberry.", "Rot wie eine Erdbeere."}, {"Blue like the sky.", "Blau wie der Himmel."},
                             {"Yellow like the sun.", "Gelb wie die Sonne."}, {"Green like grass.", "Grün wie Gras."}};
    return T[want];
  }
  const char* answer() const override { return ans; }
  const char* voiceKey() const override { static const char* K[4] = {"vcol0", "vcol1", "vcol2", "vcol3"}; return K[want]; }
  const char* hintKey() const override { static const char* K[4] = {"vhcol0", "vhcol1", "vhcol2", "vhcol3"}; return K[want]; }
  void reset() override {
    win.reset(); hit = wrongI = -1; wrongT = -10;
    shuffle(perm, 4);
    for (int i = 0; i < 4; i++) if (perm[i] == want) ans[0] = char('0' + i);
  }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap) return;
    for (int i = 0; i < 4; i++) {
      if (dist(f.x, f.y, P[i][0], P[i][1]) > 56) continue;
      const int c = order()[i];
      if (c == want) { hit = i; win.start(f); h.sfx(SFX_MAGIC); h.say(P[i][0], P[i][1] - 60, {"Yes!", "Ja!"}, HOTPINK); }
      else { wrongI = i; wrongT = f.t; h.sfx(SFX_NOPE); h.say(P[i][0], P[i][1] - 60, COLOR_NAMES[c]); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    for (int i = 0; i < 4; i++) {
      const float sh = i == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
      const float k = i == hit ? 1.9f + 0.15f * std::sin(win.since(f) * 12) : 1.7f;
      drawCandy(s, P[i][0] + sh, P[i][1], k, COLORS4[order()[i]]);
    }
    if (win.on()) for (int i = 0; i < 4; i++) sparkle(s, P[hit][0] - 60 + i * 40, P[hit][1] - 46 + (i % 2) * 90, 9, i % 2 ? CYAN : INK, 1, f.t * 2);
  }
};

// Wie viele sind es? Antwort über große Knöpfe 1–5
class CountTap : public Level {
  Win win;
  int n, kind, wrongI = -1, okI = -1;
  float wrongT = -10;
  static constexpr float BW = 58, BG = 6, BY = 266, BH = 62;
  static float bx(int i) { return CX - (5 * BW + 4 * BG) / 2 + i * (BW + BG); }
 public:
  char ans[2] = {};
  CountTap(int count, int what) : n(count), kind(what) { ans[0] = char('0' + count); }
  const char* answer() const override { return ans; }
  const char* voiceKey() const override { static const char* K[3] = {"vcnt0", "vcnt1", "vcnt2"}; return K[kind]; }
  const char* hintKey() const override { return "vhcnt"; }
  Txt task() const override {
    static const Txt T[3] = {{"How many candies are there?", "Wie viele Bonbons sind das?"}, {"How many mice are there?", "Wie viele Mäuse sind das?"},
                             {"How many stars are there?", "Wie viele Sterne sind das?"}};
    return T[kind];
  }
  Txt hint() const override { return {"Touch each one and count out loud: one, two, three …", "Tipp jedes einzeln an und zähl laut: eins, zwei, drei …"}; }
  void reset() override { win.reset(); wrongI = okI = -1; wrongT = -10; }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap) return;
    for (int i = 0; i < 5; i++) {
      if (f.x < bx(i) - 3 || f.x > bx(i) + BW + 3 || f.y < BY - 3 || f.y > BY + BH + 3) continue;
      if (i + 1 == n) { okI = i; win.start(f); h.sfx(SFX_WIN); }
      else { wrongI = i; wrongT = f.t; h.nope(bx(i) + BW / 2, BY - 30); }
    }
    // Antippen der Dinge selbst: kleines „Plopp“ zum Mitzählen
    if (f.y < BY - 10) h.sfx(SFX_POP);
  }
  void draw(Scene& s, const Frame& f, int) override {
    static const Col CC[5] = {PINK, CYAN, YELLOW, PURPLE, MINT};
    const float gap = n <= 3 ? 104 : n == 4 ? 84 : 66, x0 = CX - gap * (n - 1) / 2;
    for (int i = 0; i < n; i++) {
      const float x = x0 + i * gap, y = 190 + (i % 2) * 14 + (win.on() ? -std::fabs(std::sin(win.since(f) * 8 + i)) * 10 : 0);
      if (kind == 0) drawCandy(s, x, y, n <= 3 ? 1.5f : 1.2f, CC[i]);
      else if (kind == 1) drawMouse(s, x + 10, y + (i % 2 ? -14 : 8), n <= 3 ? 0.75f : 0.5f, i % 2);
      else sparkle(s, x, y + (i % 2 ? -10 : 6), n <= 3 ? 30 : 24, AMBER, 1, 0.1f * i);
      if (win.on()) {
        char b[4];
        std::snprintf(b, sizeof b, "%d", i + 1);
        s.circle(x, y - 46, 14, CYAN);
        s.text(fonts::label, b, x, y - 55, INK, 1, Align::Center);
      }
    }
    for (int i = 0; i < 5; i++) {
      const float sh = i == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 4 : 0;
      char b[4];
      std::snprintf(b, sizeof b, "%d", i + 1);
      s.rrect(bx(i) + sh, BY, BW, BH, 16, i == okI ? INK : BTN);
      s.text(fonts::heavy, b, bx(i) + BW / 2 + sh, BY + 14, i == okI ? WHITE : INK, 1, Align::Center);
    }
  }
};

// Groß oder klein: zwei gleiche Tiere, eins tippen
class BigSmall : public Level {
  Win win;
  bool wantBig;
  int animal;   // 0 Elefant, 1 Maus
  float wrongT = -10;
  static constexpr float LX = 110, RX = 262, GY = 270;
  bool left_ = true;
  char ans[2] = {};
  bool bigLeft() const { return left_; }
 public:
  BigSmall(bool big, int a) : wantBig(big), animal(a) {}
  const char* answer() const override { return ans; }   // 0 links, 1 rechts
  const char* voiceKey() const override { static const char* K[4] = {"vbig00", "vbig01", "vbig10", "vbig11"}; return K[animal * 2 + wantBig]; }
  const char* hintKey() const override { return wantBig ? "vhbig1" : "vhbig0"; }
  Txt task() const override {
    if (animal == 0) return wantBig ? Txt{"Tap the big elephant", "Tippe auf den großen Elefanten"} : Txt{"Tap the small elephant", "Tippe auf den kleinen Elefanten"};
    return wantBig ? Txt{"Tap the big mouse", "Tippe auf die große Maus"} : Txt{"Tap the little mouse", "Tippe auf die kleine Maus"};
  }
  Txt hint() const override { return wantBig ? Txt{"Big means: takes up lots of room.", "Groß heißt: braucht viel Platz."} : Txt{"Little means: tiny, like a baby.", "Klein heißt: winzig, wie ein Baby."}; }
  void reset() override { win.reset(); wrongT = -10; left_ = rndi(2); ans[0] = char('0' + ((left_ == wantBig) ? 0 : 1)); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap || f.y < 120 || f.y > 340) return;
    const bool left = f.x < CX;
    const bool big = left == bigLeft();
    if (big == wantBig) { win.start(f); h.sfx(SFX_MAGIC); h.say(left ? LX : RX, 150, {"Yes!", "Ja!"}, HOTPINK); }
    else { wrongT = f.t; h.sfx(SFX_NOPE); h.say(left ? LX : RX, 150, big ? Txt{"That one is big!", "Der ist groß!"} : Txt{"That one is little!", "Der ist klein!"}); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, GY + 50, 160, 12, G1, G2);
    const float bounce = win.on() ? std::fabs(std::sin(win.since(f) * 9)) * 10 : 0;
    const float bx = bigLeft() ? LX : RX, sx = bigLeft() ? RX : LX;
    const float bb = wantBig ? bounce : 0, sb = wantBig ? 0 : bounce;
    if (animal == 0) { drawElephant(s, bx - 10, GY - bb, 0.95f, !bigLeft()); drawElephant(s, sx, GY + 30 - sb, 0.45f, bigLeft()); }
    else { drawMouse(s, bx, GY - bb, 1.15f); drawMouse(s, sx + 10, GY + 16 - sb, 0.55f, true); }
    (void)wrongT;
  }
};

// Formen erkennen: Kreis, Dreieck, Quadrat
class Shapes : public Level {
  Win win;
  int want, hit = -1, wrongI = -1;
  float wrongT = -10;
  static constexpr float XS[3] = {72, 184, 296}, Y = 220;
  int perm[3] = {1, 2, 0};
  char ans[2] = {};
  int shapeAt(int, int i) const { return perm[i]; }   // bei jedem Spiel neu gemischt
  static void shape(Scene& s, int k, float x, float y, float r, float a = 1) {
    if (k == 0) s.circle(x, y, r, CYAN, a);
    else if (k == 1) tri(s, x - r * 1.1f, y + r * 0.8f, x + r * 1.1f, y + r * 0.8f, x, y - r * 1.05f, HOTPINK, a);
    else s.rrect(x - r * 0.85f, y - r * 0.85f, r * 1.7f, r * 1.7f, 6, YELLOW, a);
  }
 public:
  explicit Shapes(int s) : want(s) {}
  const char* answer() const override { return ans; }
  const char* voiceKey() const override { static const char* K[3] = {"vshp0", "vshp1", "vshp2"}; return K[want]; }
  const char* hintKey() const override { static const char* K[3] = {"vhshp0", "vhshp1", "vhshp2"}; return K[want]; }
  Txt task() const override {
    static const Txt T[3] = {{"Where is the circle?", "Wo ist der Kreis?"}, {"Where is the triangle?", "Wo ist das Dreieck?"}, {"Where is the square?", "Wo ist das Quadrat?"}};
    return T[want];
  }
  Txt hint() const override {
    static const Txt T[3] = {{"A circle is round like a ball.", "Ein Kreis ist rund wie ein Ball."}, {"A triangle has 3 corners.", "Ein Dreieck hat 3 Ecken."},
                             {"A square has 4 corners, all sides the same.", "Ein Quadrat hat 4 Ecken und gleich lange Seiten."}};
    return T[want];
  }
  void reset() override {
    win.reset(); hit = wrongI = -1; wrongT = -10;
    shuffle(perm, 3);
    for (int i = 0; i < 3; i++) if (perm[i] == want) ans[0] = char('0' + i);
  }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap) return;
    static const Txt N[3] = {{"That's a circle!", "Das ist ein Kreis!"}, {"That's a triangle!", "Das ist ein Dreieck!"}, {"That's a square!", "Das ist ein Quadrat!"}};
    for (int i = 0; i < 3; i++) {
      if (std::fabs(f.x - XS[i]) > 56 || std::fabs(f.y - Y) > 70) continue;
      const int k = shapeAt(want, i);
      if (k == want) { hit = i; win.start(f); h.sfx(SFX_MAGIC); h.say(XS[i], Y - 80, {"Yes!", "Ja!"}, HOTPINK); }
      else { wrongI = i; wrongT = f.t; h.sfx(SFX_NOPE); h.say(XS[i], Y - 80, N[k]); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    for (int i = 0; i < 3; i++) {
      const float sh = i == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
      const float r = i == hit ? 46 + 4 * std::sin(win.since(f) * 12) : 42;
      shape(s, shapeAt(want, i), XS[i] + sh, Y, r);
    }
    if (win.on()) rays(s, XS[hit], Y, 62, 74, 10, 2.5f, INK, 1, f.t);
  }
};

// Tiere zeichnen (für Suchen und Zuordnen); Mittelpunkt etwa bei (x, y), Breite gut 100 px bei k = 1
const Txt ANIMAL_IS[7] = {{"That's an owl!", "Das ist eine Eule!"}, {"That's a rabbit!", "Das ist ein Hase!"}, {"That's an elephant!", "Das ist ein Elefant!"},
                          {"That's a bat!", "Das ist eine Fledermaus!"}, {"That's a dog!", "Das ist ein Hund!"}, {"That's a mouse!", "Das ist eine Maus!"},
                          {"That's a dinosaur!", "Das ist ein Dino!"}};
void drawAnimal(Scene& s, int a, float x, float y, float k, float t) {
  switch (a) {
    case 0: drawOwl(s, x, y - 4, 0.52f * k, 1, t); break;
    case 1: drawRabbit(s, x, y + 34 * k, 0.85f * k); break;
    case 2: drawElephant(s, x - 8 * k, y - 4, 0.62f * k); break;
    case 3: drawBat(s, x, y, 0.7f * k, 0, false); break;
    case 4: drawDog(s, x, y + 30 * k, 0.6f * k, 0, t, false); break;
    case 5: drawMouse(s, x + 12 * k, y + 6, 0.9f * k); break;
    default: drawDino(s, x + 4 * k, y + 6, 0.5f * k, 1); break;
  }
}

// Wo ist das Tier? Drei Tiere, eins antippen
class FindAnimal : public Level {
  Win win;
  int want, hit = -1, wrongI = -1;
  float wrongT = -10;
  static constexpr float XS[3] = {72, 184, 296}, Y = 226;
  int slots[3] = {};
  char ans[2] = {};
  int at(int slot) const { return slots[slot]; }
 public:
  explicit FindAnimal(int a) : want(a) {}
  const char* voiceKey() const override { static const char* K[7] = {"vani0", "vani1", "vani2", "vani3", "vani4", "vani5", "vani6"}; return K[want]; }
  const char* hintKey() const override { static const char* K[7] = {"vhani0", "vhani1", "vhani2", "vhani3", "vhani4", "vhani5", "vhani6"}; return K[want]; }
  Txt task() const override {
    static const Txt T[7] = {{"Where is the owl?", "Wo ist die Eule?"}, {"Where is the rabbit?", "Wo ist der Hase?"}, {"Where is the elephant?", "Wo ist der Elefant?"},
                             {"Where is the bat?", "Wo ist die Fledermaus?"}, {"Where is the dog?", "Wo ist der Hund?"}, {"Where is the mouse?", "Wo ist die Maus?"},
                             {"Where is the dinosaur?", "Wo ist der Dino?"}};
    return T[want];
  }
  Txt hint() const override {
    static const Txt T[7] = {{"The owl has big round eyes.", "Die Eule hat große runde Augen."}, {"The rabbit has long ears.", "Der Hase hat lange Ohren."},
                             {"The elephant has a long trunk.", "Der Elefant hat einen langen Rüssel."}, {"The bat has wings.", "Die Fledermaus hat Flügel."},
                             {"The dog wags its tail.", "Der Hund wedelt mit dem Schwanz."}, {"The mouse is small with a thin tail.", "Die Maus ist klein und hat einen dünnen Schwanz."},
                             {"The dinosaur has spikes on its back.", "Der Dino hat Zacken auf dem Rücken."}};
    return T[want];
  }
  const char* answer() const override { return ans; }
  void reset() override {
    win.reset(); hit = wrongI = -1; wrongT = -10;
    int others[7], k = 0;
    shuffle(others, 7);
    const int t = rndi(3);
    for (int i = 0; i < 3; i++) {
      if (i == t) { slots[i] = want; continue; }
      while (others[k] == want) k++;
      slots[i] = others[k++];
    }
    ans[0] = char('0' + t);
  }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap) return;
    for (int i = 0; i < 3; i++) {
      if (std::fabs(f.x - XS[i]) > 56 || std::fabs(f.y - Y) > 80) continue;
      if (at(i) == want) { hit = i; win.start(f); h.sfx(SFX_MAGIC); h.say(XS[i], Y - 90, {"Yes!", "Ja!"}, HOTPINK); }
      else { wrongI = i; wrongT = f.t; h.sfx(SFX_NOPE); h.say(XS[i], Y - 90, ANIMAL_IS[at(i)]); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, Y + 62, 170, 12, G1, G2);
    for (int i = 0; i < 3; i++) {
      const float sh = i == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
      const float jump = i == hit ? std::fabs(std::sin(win.since(f) * 9)) * 14 : 0;
      drawAnimal(s, at(i), XS[i] + sh, Y - jump, 1, f.t);
    }
    if (win.on()) rays(s, XS[hit], Y, 66, 78, 10, 2.5f, YELLOW, 1, f.t);
  }
};

// Ballons platzen lassen: alle, nur die roten, oder der Reihe nach 1–6
class Balloons : public Level {
  Win win;
  int n, mode;   // mode 0 alle, 1 nur rote, 2 der Reihe nach (Zahlen)
  bool popped[6] = {};
  float popT[6] = {}, wrongT = -10;
  int next = 0;
  char ans[8] = {};
  int ORDER[6] = {3, 0, 5, 1, 4, 2};   // Zahl k+1 steht auf Ballon ORDER[k] (jedes Spiel neu)
  bool isRed[6] = {true, false, true, false, false, true};
  static constexpr float P[6][2] = {{72, 186}, {154, 158}, {234, 190}, {306, 162}, {114, 274}, {262, 276}};
  bool red(int i) const { return isRed[i]; }
  Col color(int i) const {
    static const Col C[6] = {CYAN, YELLOW, MINT, PURPLE, BLUE, GREEN};
    static const Col C2[6] = {PINK, CYAN, YELLOW, PURPLE, MINT, ORANGE};
    return mode == 1 ? (isRed[i] ? RED : C[i]) : C2[i];
  }
  int numberOf(int i) const { for (int k = 0; k < n; k++) if (ORDER[k] == i) return k + 1; return 0; }
  bool target(int i) const { return mode != 1 || red(i); }
  bool done() const { for (int i = 0; i < n; i++) if (target(i) && !popped[i]) return false; return true; }
 public:
  Balloons(int count, int m) : n(count), mode(m) {}
  const char* voiceKey() const override { static const char* K[3] = {"vbal0", "vbal1", "vbal2"}; return K[mode]; }
  const char* hintKey() const override { static const char* K[3] = {"vhbal0", "vhbal1", "vhbal2"}; return K[mode]; }
  void shuffleUp() {
    shuffle(ORDER, n);
    int p[6];
    shuffle(p, n);
    const int reds = n <= 4 ? 2 : 3;
    for (int i = 0; i < 6; i++) isRed[i] = false;
    for (int i = 0; i < reds; i++) isRed[p[i]] = true;
    int k = 0;
    if (mode == 2) for (int j = 0; j < n; j++) ans[k++] = char('0' + ORDER[j]);
    else for (int i = 0; i < n; i++) if (target(i)) ans[k++] = char('0' + i);
    ans[k] = 0;
  }
  Txt task() const override {
    if (mode == 1) return {"Pop only the red balloons!", "Lass nur die roten Ballons platzen!"};
    if (mode == 2) return {"Pop the balloons in order: 1, 2, 3 …", "Lass die Ballons der Reihe nach platzen: 1, 2, 3 …"};
    return {"Pop all the balloons!", "Lass alle Ballons platzen!"};
  }
  Txt hint() const override {
    if (mode == 1) return {"Leave the other colors alone.", "Die anderen Farben lässt du in Ruhe."};
    if (mode == 2) return {"Start with 1. A mistake brings all balloons back.", "Fang bei 1 an. Bei einem Fehler kommen alle Ballons zurück."};
    return {"Tap each balloon once.", "Tipp jeden Ballon einmal an."};
  }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); for (bool& p : popped) p = false; next = 0; wrongT = -10; shuffleUp(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.8f);
    if (win.on() || !f.pressed) return;
    for (int i = 0; i < n; i++) {
      if (popped[i] || dist(f.x, f.y, P[i][0], P[i][1] + std::sin(f.t * 2 + i) * 5) > 40) continue;
      if (mode == 1 && !red(i)) { h.nope(P[i][0], P[i][1] - 50); return; }
      if (mode == 2 && numberOf(i) != next + 1) {
        h.nope(P[i][0], P[i][1] - 50);
        for (bool& p : popped) p = false;
        next = 0;
        wrongT = f.t;
        return;
      }
      popped[i] = true; popT[i] = f.t; next++;
      h.sfx(SFX_POP);
      if (done()) { win.start(f); h.sfx(SFX_WIN); }
      return;
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    for (int i = 0; i < n; i++) {
      const float x = P[i][0], y = P[i][1] + std::sin(f.t * 2 + i) * 5;
      if (popped[i]) {
        const float u = (f.t - popT[i]) / 0.35f;
        if (u < 1) for (int k = 0; k < 8; k++) {
          const float a = k * TAU / 8;
          s.capsule(x + std::sin(a) * (20 + u * 20), y + std::cos(a) * (20 + u * 20), x + std::sin(a) * (28 + u * 26), y + std::cos(a) * (28 + u * 26), 2, color(i), 1 - u);
        }
        continue;
      }
      const float px[] = {x, y + 38, x + 6, y + 58, x - 4, y + 78, x + 2, y + 96};
      for (int k = 0; k < 3; k++) s.capsule(px[2 * k], px[2 * k + 1], px[2 * k + 2], px[2 * k + 3], 1, G4);
      tri(s, x - 6, y + 40, x + 6, y + 40, x, y + 32, dk::scale(color(i), 0.8f));
      s.ellipse(x, y, 28, 34, color(i), dk::scale(color(i), 0.8f));
      s.ellipse(x - 9, y - 12, 6, 10, WHITE, 0.45f);
      if (mode == 2) {
        char b[4];
        std::snprintf(b, sizeof b, "%d", numberOf(i));
        s.text(fonts::heavy, b, x, y - 17, INK, 1, Align::Center);
      }
    }
    if (f.t - wrongT < 0.5f) s.text(fonts::hand, "1, 2, 3 …", CX, 330, HOTPINK, 1 - (f.t - wrongT) / 0.5f, Align::Center);
  }
};

// Bring es nach Hause (ziehen): Maus zum Käse, Hund in die Hütte
class DragHome : public Level {
  Drag thing;
  Win win;
  int v;
  static constexpr float HX = 270, HY = 250;
 public:
  explicit DragHome(int variant) : v(variant) {}
  const char* voiceKey() const override { return v ? "vdrag1" : "vdrag0"; }
  const char* hintKey() const override { return "vhdrag"; }
  Txt task() const override { return v == 0 ? Txt{"Bring the mouse to the cheese", "Bring die Maus zum Käse"} : Txt{"Bring the dog into its kennel", "Bring den Hund in seine Hütte"}; }
  Txt hint() const override { return {"Put your finger on it and pull.", "Leg den Finger drauf und zieh."}; }
  void reset() override { thing.set(84, 300); win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on()) return;
    if (thing.grab(f, 60)) {
      if (f.pressed) h.sfx(SFX_POP);
      thing.move(f);
      if (thing.drop(f) && dist(thing.x, thing.y, HX, HY + 30) < 80) {
        thing.set(HX, HY + 30);
        win.start(f);
        h.sfx(v == 0 ? SFX_CHOMP : SFX_WOOF);
      }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, 336, 170, 12, G1, G2);
    if (v == 0) {   // Käse
      tri(s, HX - 60, HY + 30, HX + 50, HY + 30, HX + 50, HY - 20, YELLOW);
      s.rrect(HX - 60, HY + 30, 110, 22, 4, hex(0xF5D800));
      for (auto& c : {std::array<float, 3>{HX - 10, HY + 22, 6}, {HX + 26, HY + 10, 7}, {HX + 10, HY + 40, 5}}) s.circle(c[0], c[1], c[2], hex(0xE0BC00));
      if (win.on()) { const float u = win.since(f); for (int i = 0; i < 2; i++) { const float hy = HY - 40 - u * 40 - i * 20; s.circle(HX - 25 + i * 30 - 5, hy, 6, HOTPINK, 1 - u); s.circle(HX - 25 + i * 30 + 5, hy, 6, HOTPINK, 1 - u); tri(s, HX - 25 + i * 30 - 11, hy + 2, HX - 25 + i * 30 + 11, hy + 2, HX - 25 + i * 30, hy + 14, HOTPINK, 1 - u); } }
      drawMouse(s, thing.x + 10, thing.y, thing.on ? 0.95f : 0.85f, win.on());
    } else {   // Hütte
      s.rrect(HX - 56, HY - 30, 112, 100, 6, G2);
      for (int i = 0; i < 5; i++) s.capsule(HX - 54, HY - 14 + i * 18, HX + 54, HY - 14 + i * 18, 1.2f, G3);
      tri(s, HX - 70, HY - 26, HX + 70, HY - 26, HX, HY - 88, G5);
      s.rrect(HX - 26, HY + 12, 52, 58, 22, G6);
      s.rrect(HX - 26, HY + 42, 52, 28, 0, G6);
      if (win.on()) { drawDog(s, HX + 4, HY + 70, 0.5f, 0, f.t, true); zzz(s, HX + 30, HY + 10, f.t, CYAN); }
      else drawDog(s, thing.x, thing.y + 30, thing.on ? 0.62f : 0.58f, 0, f.t, false);
    }
  }
};

// Kitzeln / streicheln: Finger auf dem Tier hin und her bewegen
class Tickle : public Level {
  Win win;
  int v;
  float rubbed = 0, lastSay = 0, wig = 0;
  static constexpr float TX = CX, TY = 240;
 public:
  explicit Tickle(int variant) : v(variant) {}
  const char* voiceKey() const override { return v ? "vtick1" : "vtick0"; }
  const char* hintKey() const override { return "vhtick"; }
  Txt task() const override { return v == 0 ? Txt{"Tickle the dinosaur!", "Kitzel den Dino!"} : Txt{"Pet the dog!", "Streichel den Hund!"}; }
  Txt hint() const override { return {"Rub your finger back and forth on it.", "Fahr mit dem Finger hin und her über das Tier."}; }
  void reset() override { win.reset(); rubbed = lastSay = wig = 0; }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.2f);
    wig = std::fmax(0.f, wig - f.dt * 2);
    if (win.on() || !f.down || dist(f.x, f.y, TX, TY) > 120) return;
    const float d = (std::fabs(f.vx) + std::fabs(f.vy)) * f.dt;
    if (d < 1) return;
    rubbed += d;
    wig = 1;
    if (rubbed - lastSay > 180) {
      lastSay = rubbed;
      if (v == 0) { h.say(TX + (int(rubbed) % 2 ? 70 : -70), TY - 110, {"Hihi!", "Hihi!"}, HOTPINK); h.sfx(SFX_SQUEAK); }
      else h.say(TX + 60, TY - 90, {"Mmmh!", "Mmmh!"}, HOTPINK);
    }
    if (rubbed > 900) { win.start(f); h.sfx(v == 0 ? SFX_WIN : SFX_WOOF); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, 330, 160, 12, G1, G2);
    const float w = std::sin(f.t * 30) * 4 * wig;
    if (v == 0) drawDino(s, TX + w, TY + 10, 1.0f, rubbed > 300 || win.on() ? 1 : 0);
    else drawDog(s, TX + w, TY + 70, 1.0f, 0, f.t * (1 + 3 * wig), false);
    const float u = clampf(rubbed / 900, 0, 1);   // Herzen zeigen den Fortschritt
    for (int i = 0; i < 5; i++) {
      const bool on = u * 5 > i + 0.5f || win.on();
      const float hx = CX - 72 + i * 36, hy = 128;
      s.circle(hx - 5, hy, 6, on ? HOTPINK : G2);
      s.circle(hx + 5, hy, 6, on ? HOTPINK : G2);
      tri(s, hx - 11, hy + 2, hx + 11, hy + 2, hx, hy + 14, on ? HOTPINK : G2);
    }
  }
};

// Wo sind mehr / weniger? Zwei Gruppen, eine antippen
class MoreLess : public Level {
  Win win;
  int a, b, kind, wrongI = -1;
  bool more;
  float wrongT = -10;
  char ans[2] = {};
  static constexpr float BY = 118, BH = 190;
  int right() const { return (more ? a > b : a < b) ? 0 : 1; }
 public:
  MoreLess(int left, int rgt, bool m, int k) : a(left), b(rgt), kind(k), more(m) { ans[0] = char('0' + right()); }
  const char* voiceKey() const override { static const char* K[4] = {"vml00", "vml01", "vml10", "vml11"}; return K[kind * 2 + more]; }
  const char* hintKey() const override { return more ? "vhml1" : "vhml0"; }
  Txt task() const override {
    if (kind == 0) return more ? Txt{"Where are there more candies?", "Wo sind mehr Bonbons?"} : Txt{"Where are there fewer candies?", "Wo sind weniger Bonbons?"};
    return more ? Txt{"Where are there more mice?", "Wo sind mehr Mäuse?"} : Txt{"Where are there fewer mice?", "Wo sind weniger Mäuse?"};
  }
  Txt hint() const override { return more ? Txt{"Count both sides. More is the bigger number.", "Zähl beide Seiten. Mehr ist die größere Zahl."} : Txt{"Count both sides. Fewer is the smaller number.", "Zähl beide Seiten. Weniger ist die kleinere Zahl."}; }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); wrongI = -1; wrongT = -10; if (rndi(2)) std::swap(a, b); ans[0] = char('0' + right()); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap || f.y < BY || f.y > BY + BH) return;
    const int side = f.x < CX ? 0 : 1;
    if (side == right()) { win.start(f); h.sfx(SFX_WIN); }
    else { wrongI = side; wrongT = f.t; h.say(side ? 268 : 100, BY - 10, {"Count again!", "Zähl nochmal!"}); h.sfx(SFX_NOPE); }
  }
  void draw(Scene& s, const Frame& f, int) override {
    static const Col CC[6] = {PINK, CYAN, YELLOW, PURPLE, MINT, ORANGE};
    for (int side = 0; side < 2; side++) {
      const int n = side ? b : a;
      const float x0 = side ? 192 : 24;
      const float sh = side == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
      const bool ok = win.on() && side == right();
      s.rrect(x0 + sh, BY, 152, BH, 20, ok ? hex(0xE9FDFE) : G1);
      if (ok) s.rrect(x0 + sh, BY, 152, 6, 3, CYAN);
      for (int i = 0; i < n; i++) {
        const float x = x0 + 40 + (i % 2) * 72 + sh, y = BY + 34 + (i / 2) * 58 + (i % 2) * 8;
        if (kind == 0) drawCandy(s, x, y, 0.95f, CC[i % 6]);
        else drawMouse(s, x + 8, y + 6, 0.42f, i % 2);
      }
    }
  }
};

// Hütchenspiel: Unter welchem Becher ist das Bonbon? (Kleine: Becher bleiben stehen; Kinder: werden vertauscht)
class Cups : public Level {
  Win win;
  int swaps, pos[3] = {0, 1, 2}, sw[8][2] = {};   // pos[Becher] = Platz; Becher 0 hat das Bonbon
  int lifted = -1;
  float t0 = 0, liftT = -10;
  bool wrong = false;
  uint32_t seed = 0xC0B5;
  char ans[2] = {};
  static constexpr float CY = 256, SHOW = 1.6f;
  const float D;   // Dauer eines Tauschs: je mehr Tausche, desto schneller
  static float slotX(int p) { return 84 + p * 100; }
  float playEnd() const { return t0 + SHOW + swaps * D; }
  void round(float start) {
    t0 = start;
    pos[0] = lcg(seed) % 3; pos[1] = (pos[0] + 1) % 3; pos[2] = (pos[0] + 2) % 3;
    int p[3] = {pos[0], pos[1], pos[2]};
    for (int k = 0; k < swaps; k++) {
      const int a = lcg(seed) % 3, b = (a + 1 + lcg(seed) % 2) % 3;
      sw[k][0] = a; sw[k][1] = b;
      for (int& q : p) q = q == a ? b : q == b ? a : q;
    }
    ans[0] = char('0' + p[0]);
    lifted = -1; wrong = false;
  }
  // Lage von Becher c zur Zeit t (x, y); während eines Tauschs auf einem Bogen
  void cupAt(int c, float t, float& x, float& y) const {
    int p = pos[c];
    const float u = (t - t0 - SHOW) / D;
    y = CY;
    for (int k = 0; k < swaps; k++) {
      const int a = sw[k][0], b = sw[k][1];
      if (u < k) break;
      const int q = p == a ? b : p == b ? a : p;
      if (u < k + 1 && q != p) {
        const float v = ease(u - k);
        x = lerpf(slotX(p), slotX(q), v);
        y = CY + (p < q ? -1 : 1) * std::sin(v * PIF) * 34;
        return;
      }
      p = q;
    }
    x = slotX(p);
  }
 public:
  explicit Cups(int n) : swaps(n), D(n > 5 ? 0.42f : n > 3 ? 0.6f : 0.7f) {}
  const char* voiceKey() const override { return swaps ? "vcup1" : "vcup0"; }
  const char* hintKey() const override { return swaps ? "vhcup1" : "vhcup0"; }
  Txt task() const override { return swaps ? Txt{"Watch closely: where is the candy?", "Pass gut auf: Wo ist das Bonbon?"} : Txt{"Remember where the candy is!", "Merk dir, wo das Bonbon ist!"}; }
  Txt hint() const override { return swaps ? Txt{"Follow just one cup with your eyes.", "Folge nur einem Becher mit den Augen."} : Txt{"Look where the candy goes before the cups come down.", "Schau, wo das Bonbon liegt, bevor die Becher runterkommen."}; }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); seed = rnd(); round(0.4f); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on()) return;
    if (wrong && f.t - liftT > 1.2f) round(f.t + 0.3f);
    if (wrong || f.t < playEnd() || !f.tap) return;
    for (int c = 0; c < 3; c++) {
      float x, y;
      cupAt(c, f.t, x, y);
      if (std::fabs(f.x - x) > 46 || f.y < CY - 80 || f.y > CY + 60) continue;
      lifted = c; liftT = f.t;
      if (c == 0) { win.start(f); h.sfx(SFX_WIN); h.say(x, CY - 110, {"Found it!", "Gefunden!"}, HOTPINK); }
      else { wrong = true; h.nope(x, CY - 110); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    s.ellipse(CX, CY + 44, 170, 14, G1, G2);
    const float show = f.t - t0;
    for (int c = 0; c < 3; c++) {
      float x, y;
      cupAt(c, f.t, x, y);
      float up = 0;
      if (show < SHOW) up = show < SHOW - 0.5f ? 70 : 70 * (1 - ease((show - (SHOW - 0.5f)) / 0.5f));   // am Anfang angehoben
      if (c == lifted) up = 70 * ease((f.t - liftT) / 0.25f);
      if (c == 0 && up > 5) drawCandy(s, x, CY + 26, 0.9f, PINK);
      const float yy = y - up;
      s.rrect(x - 34, yy - 30, 68, 68, 12, G3);
      s.ellipse(x, yy + 36, 40, 9, G2, G4);
      s.ellipse(x, yy - 30, 30, 6, G4, G3);
      s.ellipse(x - 16, yy, 5, 20, WHITE, 0.5f);
    }
  }
};

// Was passt nicht dazu? Drei gleiche und ein anderes
class OddOne : public Level {
  Win win;
  int v, hit = -1, wrongI = -1;
  float wrongT = -10;
  char ans[2] = {};
  int oddAt = 2;
  static constexpr float P[4][2] = {{110, 170}, {258, 170}, {110, 282}, {258, 282}};
  int odd() const { return oddAt; }
 public:
  explicit OddOne(int variant) : v(variant) {}
  const char* voiceKey() const override { return "vodd"; }
  const char* hintKey() const override { return "vhodd"; }
  Txt task() const override { return {"Which one does not belong?", "Was passt nicht dazu?"}; }
  Txt hint() const override { return {"Three are the same kind, one is different.", "Drei sind gleich, eins ist anders."}; }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); hit = wrongI = -1; wrongT = -10; oddAt = rndi(4); ans[0] = char('0' + oddAt); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.0f);
    if (win.on() || !f.tap) return;
    for (int i = 0; i < 4; i++) {
      if (dist(f.x, f.y, P[i][0], P[i][1]) > 58) continue;
      if (i == odd()) { hit = i; win.start(f); h.sfx(SFX_MAGIC); h.say(P[i][0], P[i][1] - 60, {"Yes!", "Ja!"}, HOTPINK); }
      else { wrongI = i; wrongT = f.t; h.nope(P[i][0], P[i][1] - 60); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    static const Col CC[4] = {PINK, CYAN, YELLOW, MINT};
    for (int i = 0; i < 4; i++) {
      const float sh = i == wrongI && f.t - wrongT < 0.4f ? std::sin((f.t - wrongT) * 50) * 5 : 0;
      const float x = P[i][0] + sh, y = P[i][1] - (i == hit ? std::fabs(std::sin(win.since(f) * 9)) * 12 : 0);
      if (v == 0) { if (i == odd()) drawMouse(s, x + 10, y + 8, 0.75f); else drawCandy(s, x, y, 1.5f, CC[i]); }
      else { if (i == odd()) drawCandy(s, x, y, 1.5f, CC[i]); else drawAnimal(s, i % 3 == 0 ? 0 : i % 3 == 1 ? 1 : 4, x, y, 0.85f, f.t); }
    }
    if (win.on()) rays(s, P[hit][0], P[hit][1], 58, 70, 10, 2.5f, YELLOW, 1, f.t);
  }
};

// ======================= Zufalls-Generatoren (Zufallsmodus) =======================
// Gemeinsame Grundlage: Zahlenantwort über die Tastatur, Text in eigenen Puffern, bei jedem reset() neu gewürfelt
class GenPad : public Level {
 protected:
  Win win;
  NumPad pad;
  Lock lock;
  char qen[128] = {}, qde[128] = {}, hen[96] = {}, hde[96] = {}, ans[10] = {};
  bool adult;
  virtual void roll() = 0;   // neue Aufgabe würfeln: qen/qde, hen/hde, ans
  void setQ(const char* en, const char* de) { std::snprintf(qen, sizeof qen, "%s", en); std::snprintf(qde, sizeof qde, "%s", de); }
  void setH(const char* en, const char* de) { std::snprintf(hen, sizeof hen, "%s", en); std::snprintf(hde, sizeof hde, "%s", de); }
  void setA(int v) { std::snprintf(ans, sizeof ans, "%d", v); }
 public:
  explicit GenPad(bool a) : adult(a) {}
  Txt task() const override { return {qen, qde}; }
  Txt hint() const override { return {hen, hde}; }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return ans; }
  bool hasPad() const override { return true; }
  void reset() override { win.reset(); pad.clear(); lock.reset(); roll(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on()) return;
    if (pad.tick(f, h, lock.on(f))) {
      if (pad.is(ans)) { win.start(f); h.sfx(SFX_WIN); }
      else { lock.fail(f, !adult); pad.wrong(f); h.nope(CX, NP_DY - 30); }
    }
  }
  void draw(Scene& s, const Frame& f, int) override {
    pad.draw(s, f, lock, win.on());
    if (win.on()) for (int i = 0; i < 4; i++) sparkle(s, 50 + i * 90, 210 + (i % 2) * 18, 9, i % 2 ? CYAN : INK, 1, f.t * 2 + i);
  }
};

// Rechnen: Kinder bis 20 und kleines Einmaleins (auch als kleine Geschichten), Erwachsene Kopfrechnen mit Kniff
class GenMath : public GenPad {
  void roll() override {
    char e[128], d[128];
    if (!adult) {
      switch (rndi(8)) {
        case 0: { const int a = 2 + rndi(11), b = 2 + rndi(9); std::snprintf(e, sizeof e, "What is %d + %d?", a, b); std::snprintf(d, sizeof d, "Wie viel ist %d + %d?", a, b); setA(a + b); break; }
        case 1: { const int a = 8 + rndi(13), b = 1 + rndi(a - 1); std::snprintf(e, sizeof e, "What is %d – %d?", a, b); std::snprintf(d, sizeof d, "Wie viel ist %d – %d?", a, b); setA(a - b); break; }
        case 2: { const int a = 2 + rndi(4), b = 2 + rndi(9); std::snprintf(e, sizeof e, "What is %d × %d?", a, b); std::snprintf(d, sizeof d, "Wie viel ist %d × %d?", a, b); setA(a * b); break; }
        case 3: { const int a = 3 + rndi(8), b = 2 + rndi(6);
          std::snprintf(e, sizeof e, "%d children are playing in the garden. %d more come. How many are there now?", a, b);
          std::snprintf(d, sizeof d, "%d Kinder spielen im Garten. %d kommen dazu. Wie viele sind es jetzt?", a, b); setA(a + b); break; }
        case 4: { const int a = 6 + rndi(10), b = 1 + rndi(a - 2);
          std::snprintf(e, sizeof e, "You have %d sweets and eat %d of them. How many are left?", a, b);
          std::snprintf(d, sizeof d, "Du hast %d Bonbons und isst %d davon. Wie viele hast du noch?", a, b); setA(a - b); break; }
        case 5: { const int a = 2 + rndi(5);
          std::snprintf(e, sizeof e, "A dog has 4 legs. How many legs do %d dogs have?", a);
          std::snprintf(d, sizeof d, "Ein Hund hat 4 Beine. Wie viele Beine haben %d Hunde?", a); setA(4 * a); break; }
        case 6: { const int a = 2 * (2 + rndi(9));
          std::snprintf(e, sizeof e, "What is half of %d?", a); std::snprintf(d, sizeof d, "Was ist die Hälfte von %d?", a); setA(a / 2); break; }
        default: { const int a = 5 + rndi(10), b = 2 + rndi(4), c = 1 + rndi(b);
          std::snprintf(e, sizeof e, "There are %d birds on a tree. %d fly away, then %d come back. How many are there?", a, b, c);
          std::snprintf(d, sizeof d, "Auf einem Baum sitzen %d Vögel. %d fliegen weg, dann kommen %d zurück. Wie viele sind es?", a, b, c); setA(a - b + c); break; }
      }
      setQ(e, d);
      setH("Go step by step. You can use your fingers.", "Rechne Schritt für Schritt. Du darfst die Finger nehmen.");
      return;
    }
    switch (rndi(8)) {
      case 0: { const int a = 12 + rndi(18), b = 3 + rndi(7); std::snprintf(e, sizeof e, "What is %d × %d?", a, b); std::snprintf(d, sizeof d, "Wie viel ist %d × %d?", a, b); setA(a * b);
        setH("Split it: tens times, then units times, then add.", "Zerlegen: Zehner mal, Einer mal, dann addieren."); break; }
      case 1: { const int b = 3 + rndi(10), q = 6 + rndi(20); std::snprintf(e, sizeof e, "What is %d : %d?", b * q, b); std::snprintf(d, sizeof d, "Wie viel ist %d : %d?", b * q, b); setA(q);
        setH("Which number times the divisor gives this?", "Welche Zahl mal den Teiler ergibt das?"); break; }
      case 2: { static const int P[4] = {10, 20, 25, 50}; const int p = P[rndi(4)], v = 20 * (2 + rndi(20));
        std::snprintf(e, sizeof e, "What is %d %% of %d?", p, v); std::snprintf(d, sizeof d, "Wie viel sind %d %% von %d?", p, v); setA(v * p / 100);
        setH("10 % is a tenth, 25 % a quarter, 50 % half.", "10 % ist ein Zehntel, 25 % ein Viertel, 50 % die Hälfte."); break; }
      case 3: { const int n = 11 + rndi(15); std::snprintf(e, sizeof e, "What is the square root of %d?", n * n); std::snprintf(d, sizeof d, "Was ist die Wurzel aus %d?", n * n); setA(n);
        setH("Look at the last digit and estimate the tens.", "Schau auf die letzte Ziffer und schätz die Zehner."); break; }
      case 4: { const int x = 5 * (4 + rndi(36)), p = rndi(2) ? 20 : 40;
        std::snprintf(e, sizeof e, "Something costs €%d and is reduced by %d %%. How many euros does it cost now?", x, p);
        std::snprintf(d, sizeof d, "Etwas kostet %d € und wird um %d %% reduziert. Wie viele Euro kostet es jetzt?", x, p); setA(x * (100 - p) / 100);
        setH("Work out the discount first, then subtract it.", "Erst den Rabatt ausrechnen, dann abziehen."); break; }
      case 5: { const int n = 20 + 10 * rndi(8), dd = 1 + rndi(4);
        std::snprintf(e, sizeof e, "What is %d² – %d²?", n + dd, n - dd); std::snprintf(d, sizeof d, "Wie viel ist %d² – %d²?", n + dd, n - dd); setA(4 * n * dd);
        setH("a² – b² = (a + b) × (a – b)", "a² – b² = (a + b) × (a – b)"); break; }
      case 6: { const int h = 2 + rndi(5), m = 15 * (1 + rndi(3));
        std::snprintf(e, sizeof e, "How many minutes are %d hours and %d minutes?", h, m); std::snprintf(d, sizeof d, "Wie viele Minuten sind %d Stunden und %d Minuten?", h, m); setA(60 * h + m);
        setH("One hour has 60 minutes.", "Eine Stunde hat 60 Minuten."); break; }
      default: { const int a = 101 + rndi(800), b = 99 + rndi(100);
        std::snprintf(e, sizeof e, "What is %d + %d?", a, b); std::snprintf(d, sizeof d, "Wie viel ist %d + %d?", a, b); setA(a + b);
        setH("Round the second number to 100 and correct afterwards.", "Runde die zweite Zahl auf 100 und korrigier danach."); break; }
    }
    setQ(e, d);
  }
 public:
  using GenPad::GenPad;
};

// Zahlenreihen: fünf Kreise, eine Zahl fehlt
class GenSeries : public GenPad {
  int v[5] = {}, miss = 3;
  float t0 = 0;
  void roll() override {
    const int type = adult ? rndi(5) : rndi(3);
    static const int KD[4] = {2, 3, 5, 10};
    const int a = adult ? 2 + rndi(9) : 1 + rndi(9), d = adult ? 3 + rndi(9) : KD[rndi(4)];
    switch (type) {
      case 0: for (int i = 0; i < 5; i++) v[i] = a + d * i;   // +d
        setH(adult ? "Same step every time." : "Always the same step.", adult ? "Jedes Mal derselbe Schritt." : "Immer der gleiche Schritt."); break;
      case 1: for (int i = 0; i < 5; i++) v[i] = a + d * (4 - i);   // –d
        setH("It goes down by the same step.", "Es wird um denselben Schritt weniger."); break;
      case 2: { const int b = adult ? 1 + rndi(3) : 1; v[0] = b; for (int i = 1; i < 5; i++) v[i] = v[i - 1] * 2;   // verdoppeln
        setH("Each number is double the one before.", "Jede Zahl ist doppelt so groß wie die davor."); break; }
      case 3: { const int s = 1 + rndi(6); for (int i = 0; i < 5; i++) v[i] = (s + i) * (s + i);   // Quadratzahlen
        setH("Square numbers: 1, 4, 9, 16 …", "Quadratzahlen: 1, 4, 9, 16 …"); break; }
      default: { v[0] = 1 + rndi(5); v[1] = 1 + rndi(5); for (int i = 2; i < 5; i++) v[i] = v[i - 1] + v[i - 2];   // Fibonacci-artig
        setH("Add the two numbers before.", "Addiere die zwei Zahlen davor."); break; }
    }
    miss = 1 + rndi(4);
    setA(v[miss]);
    setQ("Which number is missing?", "Welche Zahl fehlt?");
  }
 public:
  using GenPad::GenPad;
  void draw(Scene& s, const Frame& f, int lang) override {
    const float r = 28, g = 6, x0 = CX - (5 * 2 * r + 4 * g) / 2 + r;
    for (int i = 0; i < 5; i++) {
      const bool q = i == miss;
      const float x = x0 + i * (2 * r + g), y = 168 + (q ? 0 : 3 * std::sin(f.t * 3 + i));
      s.circle(x, y, r, q ? (win.on() ? CYAN : YELLOW) : i % 2 ? MINT : PINK);
      char b[8];
      std::snprintf(b, sizeof b, "%d", v[i]);
      const char* t = q && !win.on() ? "?" : b;
      const auto& fnt = Scene::textW(fonts::num, t) <= 2 * r - 6 ? fonts::num : fonts::label;
      s.text(fnt, t, x, y - (&fnt == &fonts::num ? 13 : 9), INK, 1, Align::Center);
    }
    GenPad::draw(s, f, lang);
    (void)t0;
  }
};

// Kettenrechnung: Zahlen und Rechenzeichen erscheinen nacheinander, am Ende das Ergebnis eintippen
class GenChain : public GenPad {
  int n = 4, num[6] = {}, opr[6] = {};   // opr: 0 +, 1 –, 2 ×
  float phase0 = 0;
  static constexpr float STEP = 1.3f;
  float show() const { return n * STEP; }
  void roll() override {
    n = adult ? 5 : 3;
    int acc = adult ? 3 + rndi(9) : 2 + rndi(8);
    num[0] = acc; opr[0] = 0;
    for (int i = 1; i < n; i++) {
      int o = rndi(adult ? 3 : 2);
      if (o == 2 && acc > 30) o = 1;
      int x;
      if (o == 0) x = adult ? 3 + rndi(20) : 1 + rndi(9);
      else if (o == 1) { x = 1 + rndi(std::max(1, std::min(acc - 1, adult ? 25 : 9))); if (acc - x < 0) { o = 0; } }
      else x = 2 + rndi(4);
      if (o == 0) acc += x; else if (o == 1) acc -= x; else acc *= x;
      num[i] = x; opr[i] = o;
    }
    setA(acc);
    setQ("Calculate along in your head!", "Rechne im Kopf mit!");
    setH("Keep only the running total in your head. Restart shows a new chain.", "Behalte nur das Zwischenergebnis im Kopf. Neustart zeigt eine neue Kette.");
    phase0 = 0.5f;
  }
 public:
  using GenPad::GenPad;
  void reset() override { GenPad::reset(); }
  void tick(const Frame& f, Host& h) override {
    if (f.t - phase0 < show()) return;
    GenPad::tick(f, h);
  }
  void draw(Scene& s, const Frame& f, int lang) override {
    const float t = f.t - phase0, cy = 160;
    if (t < 0) return;
    const int i = int(t / STEP);
    if (i < n) {
      static const char* OP[3] = {"+", "–", "×"};
      const float u = std::fmod(t, STEP) / STEP;
      s.circle(CX, cy, 56 + 4 * std::sin(u * PIF), i % 2 ? YELLOW : MINT);
      char b[8];
      std::snprintf(b, sizeof b, "%d", num[i]);
      const float nw = Scene::textW(fonts::huge, b), ow = i ? Scene::textW(fonts::heavy, OP[opr[i]]) + 8 : 0;
      const float x0 = CX - (nw + ow) / 2;
      if (i) s.text(fonts::heavy, OP[opr[i]], x0, cy - 16, INK);
      s.text(fonts::huge, b, x0 + ow, cy - 28, INK);
      for (int k = 0; k < n; k++) s.circle(CX - (n - 1) * 8 + k * 16, 212, 4, k <= i ? INK : G2);
      return;
    }
    s.circle(CX, cy, 56, win.on() ? CYAN : G1);
    s.text(fonts::huge, win.on() ? ans : "?", CX, cy - 28, win.on() ? INK : G3, 1, Align::Center);
    GenPad::draw(s, f, lang);
  }
};

}  // namespace

// Zufallsmodus: eine frisch gewürfelte Aufgabe (ohne die festen Level, die wählt das Spiel selbst dazu)
Level* makeRandomLevel(int mode) {
  Level* l;
  if (mode == 2) {   // Klein: nur Spiele, deren Aufgabe vorgelesen werden kann
    switch (rndi(11)) {
      case 0: l = new TapColor(rndi(4)); break;
      case 1: l = new CountTap(1 + rndi(5), rndi(3)); break;
      case 2: l = new BigSmall(rndi(2), rndi(2)); break;
      case 3: l = new Shapes(rndi(3)); break;
      case 4: l = new FindAnimal(rndi(7)); break;
      case 5: { const int a = 1 + rndi(5); int b = 1 + rndi(5); if (b == a) b = a == 5 ? 3 : a + 1; l = new MoreLess(a, b, rndi(2), rndi(2)); break; }
      case 6: l = new Balloons(3 + rndi(4), rndi(2)); break;
      case 7: l = new Cups(0); break;
      case 8: l = new OddOne(rndi(2)); break;
      case 9: l = new DragHome(rndi(2)); break;
      default: l = new Tickle(rndi(2)); break;
    }
    l->kids = true;
    return l;
  }
  if (mode == 1) {   // Kind
    switch (rndi(10)) {
      case 0: case 1: case 2: l = new GenMath(false); break;
      case 3: l = new GenSeries(false); break;
      case 4: l = new GenChain(false); break;
      case 5: l = new Memory7(4 + rndi(2), 3.0f); break;
      case 6: l = new Simon(4 + rndi(3)); break;
      case 7: l = new Cups(3 + rndi(3)); break;
      case 8: l = new Balloons(4 + rndi(3), 2); break;
      default: l = rndi(2) ? (Level*)new StroopSprint(6, 15) : (Level*)new Maze(2 + rndi(2)); break;
    }
    l->kids = true;
    return l;
  }
  switch (rndi(9)) {   // Erwachsene
    case 0: case 1: case 2: return new GenMath(true);
    case 3: case 4: return new GenSeries(true);
    case 5: return new GenChain(true);
    case 6: return rndi(2) ? (Level*)new Memory7(7, 2.0f) : (Level*)new Simon(7);
    case 7: return new Cups(8);
    default: return rndi(2) ? (Level*)new StroopSprint() : (Level*)new Maze(4);
  }
}

// Sätze der Zufalls-Varianten (Klein), für die Sprachausgabe: je Schlüssel einmal Aufgabe bzw. Tipp
void randomVoices(void (*out)(const char* key, const Txt& t)) {
  auto both = [&](Level* l) {
    out(l->voiceKey(), l->task());
    out(l->hintKey(), l->hint());
    delete l;
  };
  for (int c = 0; c < 4; c++) both(new TapColor(c));
  for (int k = 0; k < 3; k++) both(new CountTap(3, k));
  for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) both(new BigSmall(b, a));
  for (int k = 0; k < 3; k++) both(new Shapes(k));
  for (int a = 0; a < 7; a++) both(new FindAnimal(a));
  for (int k = 0; k < 2; k++) for (int m = 0; m < 2; m++) both(new MoreLess(2, 4, m, k));
  for (int m = 0; m < 3; m++) both(new Balloons(5, m));
  for (int c = 0; c < 2; c++) both(new Cups(c * 3));
  both(new OddOne(0));
  for (int v = 0; v < 2; v++) both(new DragHome(v));
  for (int v = 0; v < 2; v++) both(new Tickle(v));
}

// Kleine (unter 5 Jahre): alles wird vorgelesen, keine Zahlentastatur, keine Sperren
Level* makeSmallLevel(int i) {
  Level* l;
  switch (i) {
    case 0: l = new Retext(new FeedDino(false), {"Give the candy to the dinosaur", "Gib dem Dino das Bonbon"}, {"Pull the candy into its mouth.", "Zieh das Bonbon in sein Maul."}); break;
    case 1: l = new TapColor(2); break;
    case 2: l = new Retext(new Owl, {"Shake the device to wake the owl!", "Schüttel das Gerät, dann wacht die Eule auf!"}, {"Hold it with both hands and shake.", "Halt es mit beiden Händen fest und schüttel."}); break;
    case 3: l = new CountTap(3, 0); break;
    case 4: l = new BigSmall(true, 0); break;
    case 5: l = new Retext(new ColdDino, {"Push the sun to the dinosaur", "Schieb die Sonne zum Dino"}, {"The yellow ball is the sun. Pull it to the dinosaur.", "Der gelbe Ball ist die Sonne. Zieh sie zum Dino."}); break;
    case 6: l = new Retext(new Candles, {"Blow out the candles!", "Puste die Kerzen aus!"}, {"Blow hard at the screen. Or swipe over the flames.", "Puste kräftig auf das Bild. Oder wisch über die Flammen."}); break;
    case 7: l = new Shapes(0); break;
    case 8: l = new Retext(new Hatch(3.5f), {"Put your finger on the egg and wait", "Leg deinen Finger auf das Ei und warte"}, {"Keep your finger still on the egg.", "Lass den Finger ganz ruhig auf dem Ei liegen."}); break;
    case 9: l = new CountTap(4, 1); break;
    case 10: l = new Retext(new CallDog, {"Call the dog – loudly!", "Ruf den Hund – ganz laut!"}, {"Shout “Doggy!” – or knock on the table.", "Ruf laut „Hund!“ – oder klopf auf den Tisch."}); break;
    case 11: l = new TapColor(0); break;
    case 12: l = new Retext(new Portal, {"Draw a circle around the ring", "Fahr mit dem Finger rund um den Ring"}, {"Go all the way around without lifting your finger.", "Einmal ganz herum, ohne den Finger hochzuheben."}); break;
    case 13: l = new BigSmall(false, 1); break;
    case 14: l = new Shapes(1); break;
    case 15: l = new Retext(new Bat, {"Hang the bat under the branch", "Häng die Fledermaus unten an den Ast"}, {"Pull the bat below the branch.", "Zieh die Fledermaus unter den Ast."}); break;
    case 16: l = new FindAnimal(0); break;
    case 17: l = new TapColor(1); break;
    case 18: l = new Balloons(5, 0); break;
    case 19: l = new CountTap(2, 0); break;
    case 20: l = new BigSmall(false, 0); break;
    case 21: l = new DragHome(0); break;
    case 22: l = new Shapes(2); break;
    case 23: l = new Tickle(0); break;
    case 24: l = new MoreLess(2, 4, true, 0); break;
    case 25: l = new FindAnimal(1); break;
    case 26: l = new Cups(0); break;
    case 27: l = new TapColor(3); break;
    case 28: l = new CountTap(5, 2); break;
    case 29: l = new DragHome(1); break;
    case 30: l = new BigSmall(true, 1); break;
    case 31: l = new Balloons(6, 1); break;
    case 32: l = new Tickle(1); break;
    case 33: l = new MoreLess(5, 3, false, 1); break;
    case 34: l = new FindAnimal(3); break;
    default: l = new OddOne(0); break;
  }
  l->kids = true;
  return l;
}

Level* makeLevel(int i) {
  switch (i) {
    case 0: return new FeedDino;
    case 1: return new Biggest;
    case 2: return new CountCandy;
    case 3: return new Fridge;
    case 4: return new Hatch;
    case 5: return new ColdDino;
    case 6: return new Portal;
    case 7: return new Owl;
    case 8: return new HiddenMouse;
    case 9: return new Candles;
    case 10: return new CallDog;
    case 11: return new Bat;
    case 12: return new Quiz(Q_BAT);
    case 13: return new Quiz(Q_MACHINES);
    case 14: return new Quiz(Q_LILY);
    case 15: return new StroopSprint;
    case 16: return new Quiz(Q_SOCKS);
    case 17: return new Memory7;
    case 18: return new Quiz(Q_SPEED);
    case 19: return new Quiz(Q_BOXES);
    case 20: return new Quiz(Q_AGE);
    case 21: return new Quiz(Q_CHAIN);
    case 22: return new Maze;
    case 23: return new Quiz(Q_KNIGHTS);
    case 24: return new Quiz(Q_CUCUMBER);
    case 25: return new Quiz(Q_WEEKDAY);
    case 26: return new Simon;
    case 27: return new Quiz(Q_SNAIL);
    case 28: return new Quiz(Q_MAGIC);
    case 29: return new Quiz(Q_ONES);
    case 30: return new Quiz(Q_MONTY);
    case 31: return new Quiz(Q_CHESS);
    case 32: return new Quiz(Q_ROPE);
    case 33: return new Quiz(Q_CLOCK);
    case 34: return new Quiz(Q_LETTERS);
    default: return new Quiz(Q_LOOKSAY);
  }
}

// Kinder: die zwölf Trick-Level, dann leichte Denkfallen und Spiele; falsche Antworten sperren nur kurz
Level* makeKidLevel(int i) {
  Level* l;
  switch (i) {
    case 12: l = new Quiz(K_SHEEP); break;
    case 13: l = new Quiz(K_FEATHERS); break;
    case 14: l = new Memory7(4, 3.0f); break;
    case 15: l = new Quiz(K_LEGS); break;
    case 16: l = new Quiz(K_RACE); break;
    case 17: l = new Quiz(K_SERIES); break;
    case 18: l = new Simon(4); break;
    case 19: l = new Quiz(K_EGGS); break;
    case 20: l = new Quiz(K_TOWEL); break;
    case 21: l = new Maze(2); break;
    case 22: l = new Quiz(K_MONTHS); break;
    case 23: l = new Quiz(K_TOM); break;
    case 24: l = new Quiz(K_ROOSTER); break;
    case 25: l = new Cups(4); break;
    case 26: l = new Quiz(K_COMB); break;
    case 27: l = new Quiz(K_SPIDER); break;
    case 28: l = new Quiz(K_SERIES5); break;
    case 29: l = new Quiz(K_APPLES); break;
    case 30: l = new Memory7(5, 3.0f); break;
    case 31: l = new Quiz(K_COCOA); break;
    case 32: l = new Quiz(K_BUS); break;
    case 33: l = new Quiz(K_SHORTEST); break;
    case 34: l = new Balloons(6, 2); break;
    case 35: l = new Quiz(K_SERIES_ODD); break;
    case 36: l = new Quiz(K_DAYS); break;
    case 37: l = new Simon(5); break;
    case 38: l = new Quiz(K_ODD); break;
    case 39: l = new Quiz(K_RABBITS); break;
    case 40: l = new StroopSprint(6, 15); break;
    case 41: l = new Quiz(K_CUBE); break;
    case 42: l = new Quiz(K_SERIES_DOWN); break;
    case 43: l = new Quiz(K_PENGUIN); break;
    case 44: l = new Maze(3); break;
    case 45: l = new Quiz(K_RIDDLE); break;
    case 46: l = new Quiz(K_MUM); break;
    case 47: l = new Quiz(K_SERIES_DOUBLE); break;
    default: l = makeLevel(i); break;
  }
  l->kids = true;
  return l;
}

}  // namespace bc
