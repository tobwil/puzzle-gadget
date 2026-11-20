// Die Level. Jedes Level ist ein kleines Trickrätsel: Die naheliegende Antwort ist selten die richtige.
// Mechaniken: ziehen, tippen, halten, Kreis malen, schütteln, klopfen, pusten (Mikrofon), umdrehen (IMU),
// und Teile der Oberfläche selbst (Aufgabentext, „Deko“) sind Teil des Rätsels.
#include <cstdio>

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
  bool eaten = false;
  float eatT = 0;
  Win win;
  static constexpr float HX = 252, HY = 206, K = 0.92f;
  float mouthX() const { return HX - 30 * K; }
  float mouthY() const { return HY + 30 * K; }

 public:
  Txt task() const override { return {"Feed the dinosaur", "Füttere den Dino"}; }
  Txt hint() const override { return {"Drag the candy right into its open mouth.", "Zieh das Bonbon direkt in sein offenes Maul."}; }
  void reset() override { candy.set(84, 318); eaten = false; win.reset(); }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.9f);
    if (eaten) return;
    if (candy.grab(f, 44)) {
      if (f.pressed) h.sfx(SFX_POP);
      candy.move(f);
      if (candy.drop(f)) {
        if (dist(candy.x, candy.y, mouthX(), mouthY()) < 58) { eaten = true; eatT = f.t; h.sfx(SFX_CHOMP); win.start(f); }
        else h.sfx(SFX_CLICK);
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
      drawCandy(s, candy.x, candy.y, candy.on ? 1.15f : 1.f);
    } else {
      const float u = f.t - eatT;
      if (u < 0.25f) drawCandy(s, mouthX(), mouthY(), 1 - u * 4);
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

// ======================= 3: Wie viele Bonbons? (Becher anheben) =======================
class CountCandy : public Level {
  Drag cup;
  Win win;
  int wrong = -1;
  float wrongT = 0;
  static constexpr int NUM[4] = {3, 4, 5, 6};
  float btnX(int i) const { return CX - 72 + i * 48; }
  static constexpr float BY = 318;
 public:
  Txt task() const override { return {"How many candies are there?", "Wie viele Bonbons gibt es?"}; }
  Txt hint() const override { return {"Something is hiding under the cup. Move it!", "Unter dem Becher versteckt sich etwas. Schieb ihn weg!"}; }
  void reset() override { cup.set(276, 236); win.reset(); wrong = -1; }
  void tick(const Frame& f, Host& h) override {
    win.tick(f, h, 0.7f);
    if (win.on()) return;
    if (cup.grab(f, 48)) {
      cup.move(f, 40, 150, W - 40, 268);
      cup.drop(f);
      return;
    }
    if (!f.tap) return;
    for (int i = 0; i < 4; i++)
      if (std::fabs(f.x - btnX(i)) < 24 && std::fabs(f.y - BY) < 24) {
        if (NUM[i] == 5) { win.start(f); h.sfx(SFX_WIN); wrong = i; }
        else { h.nope(btnX(i), BY - 40); wrong = i; wrongT = f.t; }
      }
  }
  void draw(Scene& s, const Frame& f, int) override {
    static const float C[5][3] = {{88, 170, 0.1f}, {176, 150, -0.2f}, {150, 230, 0.3f}, {72, 252, 0}, {276, 248, 0}};
    static const Col CC[5] = {PINK, CYAN, HOTPINK, YELLOW, PURPLE};
    for (int i = 0; i < 5; i++) drawCandy(s, C[i][0], C[i][1], 0.9f, CC[i]);
    // Becher (umgedreht): Körper, Rand unten, Henkel
    const float x = cup.x, y = cup.y;
    s.arc(x + 34, y - 4, 16, 6, 0.3f, 2.8f, G4);
    s.rrect(x - 34, y - 42, 68, 80, 12, G3);
    s.ellipse(x, y - 4, 32, 38, G2, G4, 0.6f);
    s.ellipse(x, y + 36, 40, 9, G2, G4);
    s.ellipse(x, y - 42, 30, 6, G4, G3);
    s.ellipse(x - 16, y - 10, 5, 22, WHITE, 0.5f);
    for (int i = 0; i < 4; i++) {
      const bool ok = win.on() && wrong == i;
      const float sh = (i == wrong && !ok && f.t - wrongT < 0.4f) ? std::sin((f.t - wrongT) * 50) * 4 : 0;
      s.rrect(btnX(i) - 21 + sh, BY - 21, 42, 42, 10, ok ? INK : BTN);
      char b[4];
      std::snprintf(b, sizeof b, "%d", NUM[i]);
      s.text(fonts::num, b, btnX(i) + sh, BY - 14, ok ? WHITE : INK, 1, Align::Center);
    }
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
    if (onEgg && f.hold > 0.35f) heat += f.dt / 3.2f;
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
      if (f.down && dist(f.x, f.y, EX, EY) < 70) s.glow(f.x, f.y, 60 + heat * 30, ORANGE, 0.25f + heat * 0.35f);
      drawEgg(s, EX, EY, 1.2f, heat, wob);
      // Wärmebalken als Bogen unter dem Ei
      if (heat > 0.02f) s.arc(EX, EY + 10, 92, 5, -0.9f, -0.9f + 1.8f * heat, AMBER, 0.9f);
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
    // Maus hinter dem Text; der Schwanz schaut rechts heraus (der Hinweis)
    const float pk = win.on() ? std::fabs(std::sin(win.since(f) * 6)) * 3 : 0;
    drawMouse(s, MX, MY - pk, 0.42f, true);
    const float tp[] = {MX + 26, MY + 8, MX + 60, MY + 12, MX + 96, MY + 6, MX + 112, MY - 4, MX + 116, MY - 14};
    for (int i = 0; i < 4; i++) s.capsule(tp[2 * i], tp[2 * i + 1], tp[2 * i + 2], tp[2 * i + 3], 1.3f, G4);
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
    default: return new Bat;
  }
}

}  // namespace bc
