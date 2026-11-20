// Die Level. Jedes Level ist ein kleines Trickrätsel: Die naheliegende Antwort ist selten die richtige.
// Mechaniken: ziehen, tippen, halten, Kreis malen, schütteln, klopfen, pusten (Mikrofon), umdrehen (IMU),
// und Teile der Oberfläche selbst (Aufgabentext, „Deko“) sind Teil des Rätsels.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui.h"

namespace bc {

namespace {

constexpr float CX = W / 2.f;

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
  void fail(const Frame& f) { fails++; until = f.t + std::fmin(30.f, 4.f * float(1 << std::min(fails - 1, 4))); }
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
      s.rrect(x, y, NP_KW, NP_KH, 10, k == 11 ? INK : down ? BTN_DOWN : BTN, a);
      if (k == 11) s.text(fonts::label, L[k], npKeyX(k), y + 9, WHITE, a, Align::Center);
      else s.text(fonts::num, L[k], npKeyX(k), y + 5, INK, a, Align::Center);
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
  float unwrapT = -10;
  float eatT = 0;
  Win win;
  static constexpr float HX = 252, HY = 206, K = 0.92f;
  float mouthX() const { return HX - 30 * K; }
  float mouthY() const { return HY + 30 * K; }

 public:
  Txt task() const override { return {"Feed the dinosaur", "Füttere den Dino"}; }
  Txt hint() const override { return {"Nobody eats candy with the wrapper. Swipe quickly across it to unwrap it.", "Niemand isst Bonbons mit Papier. Wisch schnell darüber, um es auszupacken."}; }
  void reset() override { candy.set(84, 318); eaten = false; wrapped = true; unwrapT = -10; win.reset(); }
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
      else { lock.fail(f); pad.wrong(f); h.nope(CX, NP_DY - 30); }
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
  static constexpr float EX = CX, EY = 236;
 public:
  Txt task() const override { return {"Hatch the egg", "Lass das Küken schlüpfen"}; }
  Txt hint() const override { return {"Eggs need warmth. Keep your finger on it for a while.", "Eier brauchen Wärme. Lass deinen Finger eine Weile drauf."}; }
  void reset() override { heat = 0; wobT = -10; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 1.1f);
    if (win.on()) return;
    const bool onEgg = f.down && dist(f.x, f.y, EX, EY) < 70;
    const float before = heat;
    if (onEgg && f.hold > 0.35f) heat += f.dt / 7.0f;
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
    if (f.voice > 0.3f) {
      blowT += f.dt;
      if (blowT > 0.3f) { for (int i = 0; i < 3; i++) if (lit[i]) { out(i); break; } blowT = 0; }
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
    lock.fail(f);
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

uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

// ======================= Merk dir die Zahl (7 Ziffern, 2 s) =======================
class Memory7 : public Level {
  Win win;
  NumPad pad;
  Lock lock;
  char num[8] = {};
  float phase0 = 0;
  uint32_t seed = 0x5EED;
  static constexpr float SHOW = 2.0f;
  void fresh() {
    num[0] = char('1' + lcg(seed) % 9);
    for (int i = 1; i < 7; i++) num[i] = char('0' + lcg(seed) % 10);
    num[7] = 0;
  }
 public:
  Txt task() const override { return {"Remember the number! (2 seconds)", "Merk dir die Zahl! (2 Sekunden)"}; }
  Txt hint() const override { return {"Chunk it: 3 + 4 digits, like a phone number. Restart shows a new one.", "In Häppchen merken: 3 + 4 Ziffern, wie eine Telefonnummer. Neustart zeigt eine neue."}; }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return num; }
  void reset() override { win.reset(); pad.clear(); lock.reset(); phase0 = 0.4f; fresh(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (win.on() || f.t - phase0 < SHOW) return;
    if (pad.tick(f, h, lock.on(f))) {
      if (pad.is(num)) { win.start(f); h.sfx(SFX_WIN); }
      else { lock.fail(f); pad.wrong(f); h.nope(CX, NP_DY - 30); fresh(); phase0 = lock.until; }
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
    s.text(fonts::huge, win.on() ? num : "???????", CX, 132, win.on() ? INK : G3, 1, Align::Center);
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
  static constexpr int NEED = 10;
  static constexpr float LIMIT = 12, GW = 152, GH = 42, GY0 = 252, GY1 = 300;
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
  Txt task() const override { return {"Tap the INK COLOR: 10 in a row within 12 seconds!", "Tippe die SCHRIFTFARBE: 10 richtige in Folge in 12 s!"}; }
  Txt hint() const override { return {"Ignore what the word says. Only the color of the letters counts.", "Ignorier, was da steht. Nur die Farbe der Buchstaben zählt."}; }
  int tools() const override { return TOOL_FINGER | TOOL_THINK; }
  const char* answer() const override { return ans; }
  void reset() override { win.reset(); streak = 0; startT = -1; wrongT = okT = -10; wrongBtn = -1; next(); }
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
    else s.text(fonts::huge, "10/10", CX, 140, INK, 1, Align::Center);
    if (startT >= 0 && !win.on()) {
      const float u = clampf((f.t - startT) / LIMIT, 0, 1);
      s.capsule(CX - 120, 206, CX + 120 - 240 * u, 206, 4, u > 0.75f ? HOTPINK : CYAN);
    }
    for (int i = 0; i < NEED; i++) s.circle(CX - 81 + i * 18, 226, 5, i < streak || win.on() ? INK : G2);
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
  static constexpr int N = 7;
  static constexpr float STEP = 0.6f, ON = 0.4f, PW = 100, PH = 80;
  static Col color(int i) { static const Col C[4] = {CYAN, YELLOW, HOTPINK, MINT}; return C[i]; }
  static void pad(int i, float& x, float& y) { x = i % 2 ? CX + 4 : CX - 104; y = i / 2 ? 216 : 130; }
  bool playing(const Frame& f) const { return f.t - phase0 < N * STEP; }
 public:
  Txt task() const override { return {"Watch, then repeat the color sequence", "Schau zu und tippe die Farbfolge nach"}; }
  Txt hint() const override { return {"7 colors. Name them in your head while watching, e.g. “blue, yellow, pink …”.", "7 Farben. Sprich sie beim Zuschauen im Kopf mit, z. B. „blau, gelb, pink …“."}; }
  int tools() const override { return TOOL_THINK; }
  const char* answer() const override { return ans; }
  void reset() override {
    win.reset(); lock.reset(); pos = 0; phase0 = 0.8f;
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
        lock.fail(f);
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
    else if (win.on()) std::snprintf(b, sizeof b, "7/7");
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
  static constexpr float HOLES[4][2] = {{170, 118}, {240, 148}, {322, 226}, {150, 330}};
  static constexpr float HR = 12;
  static bool hitBox(float x, float y, const Box& b) {
    const float cx = clampf(x, b.x0, b.x1), cy = clampf(y, b.y0, b.y1);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) < R * R;
  }
  static bool blocked(float x, float y) { return x < X0 + R || x > X1 - R || y < Y0 + R || y > Y1 - R || hitBox(x, y, WALL); }
 public:
  Txt task() const override { return {"Roll the ball into the goal", "Roll die Kugel ins Ziel"}; }
  Txt hint() const override { return {"Tilt the whole device – gently. In the last corridor, keep the ball close to the upper wall.", "Kipp das ganze Gerät – ganz sanft. Im letzten Gang die Kugel oben an der Wand halten."}; }
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
      for (auto& hole : HOLES)
        if (dist(bx, by, hole[0], hole[1]) < HR) {
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
    for (auto& hole : HOLES) { s.circle(hole[0], hole[1], HR + 2, G4); s.circle(hole[0], hole[1], HR, G6); }
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

}  // namespace

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

}  // namespace bc
