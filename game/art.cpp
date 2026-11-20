// Zeichenhelfer: Schnörkel, Symbole und die Figuren der Level, alles aus den Formen des Renderers.
#include <cstdio>
#include <cstring>
#include <utility>

#include "ui.h"

namespace bc {

float textWrap(Scene& s, const fonts::Font& f, const char* str, float cx, float y, float maxW, Col c, float a) {
  char line[96], word[64];
  line[0] = 0;
  float yy = y;
  const char* p = str;
  while (true) {
    // nächstes Wort (Leerzeichen trennen; UTF-8 bleibt beisammen)
    while (*p == ' ') p++;
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\n' && n < 63) n++;
    std::memcpy(word, p, n);
    word[n] = 0;
    const bool nl = p[n] == '\n';
    p += n;
    if (n) {
      char test[160];
      std::snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
      if (line[0] && Scene::textW(f, test) > maxW) {
        s.text(f, line, cx, yy, c, a, Align::Center);
        yy += f.lineH;
        std::snprintf(line, sizeof line, "%s", word);
      } else {
        std::snprintf(line, sizeof line, "%s", test);
      }
    }
    if (nl && line[0]) { s.text(f, line, cx, yy, c, a, Align::Center); yy += f.lineH; line[0] = 0; }
    if (nl) p++;
    if (!*p) break;
  }
  if (line[0]) { s.text(f, line, cx, yy, c, a, Align::Center); yy += f.lineH; }
  return yy - y;
}

void scribble(Scene& s, const float* pts, int n, float thick, Col c, float a) {
  // Catmull-Rom durch die Punkte, Dicke schwankt wie bei einem Pinselstrich
  auto P = [&](int i, int k) { i = i < 0 ? 0 : i >= n ? n - 1 : i; return pts[2 * i + k]; };
  float px = pts[0], py = pts[1];
  int step = 0;
  for (int i = 0; i + 1 < n; i++)
    for (int j = 1; j <= 8; j++, step++) {
      const float t = j / 8.f, t2 = t * t, t3 = t2 * t;
      float q[2];
      for (int k = 0; k < 2; k++)
        q[k] = 0.5f * (2 * P(i, k) + (-P(i - 1, k) + P(i + 1, k)) * t + (2 * P(i - 1, k) - 5 * P(i, k) + 4 * P(i + 1, k) - P(i + 2, k)) * t2 +
                       (-P(i - 1, k) + 3 * P(i, k) - 3 * P(i + 1, k) + P(i + 2, k)) * t3);
      const float w = thick * 0.5f * (0.7f + 0.3f * std::sin(step * 0.45f)) * (step < 4 ? 0.5f + step * 0.12f : 1);
      s.capsule(px, py, q[0], q[1], w, c, a);
      px = q[0]; py = q[1];
    }
}

void sparkle(Scene& s, float x, float y, float r, Col c, float a, float rot) {
  for (int i = 0; i < 4; i++) {
    const float an = rot + i * PIF / 4, len = i % 2 ? r * 0.7f : r;
    const float dx = std::sin(an) * len, dy = -std::cos(an) * len;
    s.capsule(x - dx, y - dy, x + dx, y + dy, std::fmax(1.2f, r * 0.11f), c, a);
  }
}

void rays(Scene& s, float x, float y, float r0, float r1, int n, float thick, Col c, float a, float rot) {
  for (int i = 0; i < n; i++) {
    const float an = rot + i * TAU / n, sx = std::sin(an), cy = -std::cos(an);
    s.capsule(x + sx * r0, y + cy * r0, x + sx * r1, y + cy * r1, thick, c, a);
  }
}

void tri(Scene& s, float ax, float ay, float bx, float by, float cx, float cy, Col c, float a) {
  // waagrechte Streifen (je 2 px), Aufwand wächst nur mit der Fläche
  if (ay > by) { std::swap(ax, bx); std::swap(ay, by); }
  if (by > cy) { std::swap(bx, cx); std::swap(by, cy); }
  if (ay > by) { std::swap(ax, bx); std::swap(ay, by); }
  const float h = cy - ay;
  if (h < 0.5f) { s.capsule(std::fmin(ax, std::fmin(bx, cx)), ay, std::fmax(ax, std::fmax(bx, cx)), ay, 0.8f, c, a); return; }
  const float step = 2.f, r = 1.15f;
  for (float y = ay + 1; y <= cy - 0.5f; y += step) {
    const float xl = ax + (cx - ax) * (y - ay) / h;   // lange Kante A–C
    const float xs = y < by ? (by - ay < 0.01f ? bx : ax + (bx - ax) * (y - ay) / (by - ay))
                            : (cy - by < 0.01f ? bx : bx + (cx - bx) * (y - by) / (cy - by));
    const float x0 = std::fmin(xl, xs), x1 = std::fmax(xl, xs);
    if (x1 - x0 < 2 * r) s.circle((x0 + x1) / 2, y, std::fmax(0.6f, (x1 - x0) / 2 + 0.3f), c, a);
    else s.capsule(x0 + r * 0.6f, y, x1 - r * 0.6f, y, r, c, a);
  }
}

// ---------------- Symbole ----------------
void iconBack(Scene& s, float x, float y, Col c) {
  s.capsule(x + 6, y - 11, x - 5, y, 3.4f, c);
  s.capsule(x - 5, y, x + 6, y + 11, 3.4f, c);
}

void iconSkip(Scene& s, float x, float y, Col c) {
  for (float dx : {-7.f, 7.f}) {
    s.capsule(x + dx - 6, y - 10, x + dx + 4, y, 3.4f, c);
    s.capsule(x + dx + 4, y, x + dx - 6, y + 10, 3.4f, c);
  }
}

void iconRestart(Scene& s, float x, float y, Col c) {
  s.arc(x, y, 11, 5, 5.2f, 5.2f + 4.9f, c);
  // Pfeilspitze am Anfang des Bogens (oben links)
  const float an = 5.2f, px = x + std::sin(an) * 11, py = y - std::cos(an) * 11;
  tri(s, px - 9, py - 1, px + 3, py - 7, px + 3, py + 6, c);
}

void iconBulb(Scene& s, float x, float y, float k, Col c) {
  s.circle(x, y - 4 * k, 12 * k, c);
  s.rrect(x - 6.5f * k, y + 3 * k, 13 * k, 9 * k, 2 * k, c);
  s.rrect(x - 5.5f * k, y + 13 * k, 11 * k, 3 * k, 1.5f * k, c);
  s.rrect(x - 3.5f * k, y + 17 * k, 7 * k, 2.5f * k, 1.2f * k, c);
}

void iconSpeaker(Scene& s, float x, float y, bool on, Col c) {
  s.rrect(x - 12, y - 5, 8, 10, 2, c);
  tri(s, x - 6, y, x + 2, y - 11, x + 2, y + 11, c);
  if (on) {
    s.arc(x + 4, y, 8, 3, 0.7f, 2.45f, c);
    s.arc(x + 4, y, 14, 3, 0.75f, 2.4f, c);
  } else {
    s.capsule(x + 7, y - 6, x + 17, y + 6, 1.8f, c);
    s.capsule(x + 17, y - 6, x + 7, y + 6, 1.8f, c);
  }
}

void iconGrid(Scene& s, float x, float y, Col c) {
  for (int i = 0; i < 4; i++) s.rrect(x - 11 + (i % 2) * 12, y - 11 + (i / 2) * 12, 10, 10, 2.5f, c);
}

// ---------------- Figuren ----------------
void drawCandy(Scene& s, float x, float y, float k, Col c, float a) {
  const Col dark = dk::scale(c, 0.78f), light = dk::mix(c, WHITE, 0.45f);
  tri(s, x - 15 * k, y, x - 33 * k, y - 13 * k, x - 31 * k, y + 14 * k, dark, a);
  tri(s, x + 15 * k, y, x + 33 * k, y - 14 * k, x + 31 * k, y + 13 * k, dark, a);
  s.ellipse(x, y, 21 * k, 15 * k, light, dark, a);
  s.capsule(x - 17 * k, y - 6 * k, x - 17 * k, y + 6 * k, 1.6f * k, dk::scale(c, 0.6f), a * 0.6f);
  s.capsule(x + 17 * k, y - 6 * k, x + 17 * k, y + 6 * k, 1.6f * k, dk::scale(c, 0.6f), a * 0.6f);
  s.ellipse(x - 6 * k, y - 6 * k, 7 * k, 3.5f * k, WHITE, a * 0.7f);
}

void drawDinoHead(Scene& s, float x, float y, float k, float open, float blink) {
  auto X = [&](float d) { return x + d * k; };
  auto Y = [&](float d) { return y + d * k; };
  const Col TOOTH = hex(0xF6F2E8);
  // Hals
  s.ellipse(X(48), Y(95), 52 * k, 92 * k, G3, G4);
  for (int i = 0; i < 5; i++) s.capsule(X(20 + i * 12), Y(70 + i * 9), X(46 + i * 10), Y(66 + i * 9), 2 * k, G4, 0.35f);
  // Unterkiefer
  const float jy = 28 + open * 30;
  s.ellipse(X(-30), Y(jy), 62 * k, 16 * k, G3, G4);
  // Maul innen
  if (open > 0.05f) {
    s.ellipse(X(-28), Y(16 + open * 12), 56 * k, (4 + open * 18) * k, hex(0x5A1A26), hex(0x8E2C3E));
    s.ellipse(X(-20), Y(jy - 6), 34 * k, 8 * k, hex(0xD9667E), hex(0xB8455C));
  }
  // Zähne
  for (int i = 0; i < 6; i++) {
    const float tx = -82 + i * 13;
    tri(s, X(tx - 4), Y(jy - 13), X(tx + 4), Y(jy - 13), X(tx), Y(jy - 13 - 9 * (0.6f + 0.4f * open)), TOOTH);
    tri(s, X(tx - 3), Y(17), X(tx + 5), Y(17), X(tx + 1), Y(17 + 11 * (0.6f + 0.4f * open)), TOOTH);
  }
  // Schädel und Schnauze
  s.ellipse(X(8), Y(-8), 62 * k, 44 * k, G2, G4);
  s.ellipse(X(-44), Y(2), 54 * k, 22 * k, G2, G4);
  // Hautstruktur
  static const float SP[][3] = {{-60, -4, 4}, {-30, -10, 3}, {10, -30, 5}, {30, 6, 4}, {-8, 10, 3}, {44, -14, 3}, {-74, 8, 2.5f}, {24, -40, 3}};
  for (auto& p : SP) s.circle(X(p[0]), Y(p[1]), p[2] * k, G5, 0.22f);
  s.capsule(X(-80), Y(14), X(-10), Y(18), 1.6f * k, G5, 0.5f);
  // Augenwulst, Auge, Nasenloch
  s.capsule(X(-8), Y(-26), X(28), Y(-34), 7 * k, G4);
  if (blink > 0.5f) {
    s.capsule(X(6), Y(-17), X(20), Y(-17), 2.2f * k, INK);
  } else {
    s.circle(X(13), Y(-17), 7.5f * k, hex(0x2A2016));
    s.circle(X(11), Y(-19), 2.4f * k, WHITE);
  }
  s.ellipse(X(-86), Y(-6), 5 * k, 3 * k, G6);
}

void drawDino(Scene& s, float x, float y, float k, float mood, bool flip) {
  const float f = flip ? -1.f : 1.f;
  auto X = [&](float d) { return x + f * d * k; };
  auto Y = [&](float d) { return y + d * k; };
  // Schwanz
  s.capsule(X(-30), Y(-2), X(-70), Y(14), 17 * k, G3);
  s.capsule(X(-70), Y(14), X(-100), Y(26), 10 * k, G3);
  s.capsule(X(-100), Y(26), X(-122), Y(32), 5 * k, G4);
  // hinteres Bein
  s.ellipse(X(-12), Y(20), 22 * k, 26 * k, G3, G4);
  s.capsule(X(-12), Y(36), X(-16), Y(64), 8 * k, G4);
  s.capsule(X(-26), Y(66), X(-2), Y(66), 5 * k, G5);
  // Körper
  s.ellipse(X(0), Y(0), 46 * k, 32 * k, G2, G4);
  s.ellipse(X(8), Y(12), 28 * k, 14 * k, G1, G3, 0.6f);
  // vorderes Bein
  s.ellipse(X(14), Y(22), 18 * k, 22 * k, G2, G4);
  s.capsule(X(14), Y(36), X(16), Y(64), 8 * k, G4);
  s.capsule(X(4), Y(66), X(28), Y(66), 5 * k, G5);
  // Hals, Kopf
  s.capsule(X(28), Y(-12), X(42), Y(-34), 16 * k, G3);
  s.ellipse(X(50), Y(-44), 30 * k, 19 * k, G2, G4);
  s.ellipse(X(56), Y(-31), 24 * k, 9 * k, G3, G4);
  s.circle(X(56), Y(-50), 4.2f * k, INK);
  s.circle(X(55), Y(-51.5f), 1.4f * k, WHITE);
  s.ellipse(X(76), Y(-46), 3 * k, 2 * k, G6);
  if (mood > 0.5f) s.arc(X(62), Y(-38), 9 * k, 2.2f * k, 1.9f, 3.6f, G6, ease((mood - 0.5f) * 2));
  else s.capsule(X(58), Y(-34), X(72), Y(-35), 1.6f * k, G6);
  // Ärmchen
  s.capsule(X(32), Y(2), X(44), Y(10), 3.2f * k, G4);
  // Rückenzacken
  for (int i = 0; i < 4; i++) s.circle(X(-28 + i * 14), Y(-30 + i * 1.5f), 4 * k, G4);
}

void drawMouse(Scene& s, float x, float y, float k, bool flip) {
  const float f = flip ? -1.f : 1.f;
  auto X = [&](float d) { return x + f * d * k; };
  auto Y = [&](float d) { return y + d * k; };
  // Schwanz
  const float tp[] = {X(-32), Y(10), X(-50), Y(8), X(-64), Y(-2), X(-70), Y(-18), X(-66), Y(-30)};
  for (int i = 0; i < 4; i++) s.capsule(tp[2 * i], tp[2 * i + 1], tp[2 * i + 2], tp[2 * i + 3], (2.8f - i * 0.4f) * k, G4);
  // Körper, Füße
  s.ellipse(X(-14), Y(22), 9 * k, 5 * k, G4);
  s.ellipse(X(18), Y(23), 9 * k, 5 * k, G4);
  s.ellipse(X(0), Y(0), 36 * k, 25 * k, G2, G4);
  // hinteres Ohr
  s.circle(X(16), Y(-26), 12 * k, G3);
  // Kopf
  s.ellipse(X(30), Y(-6), 23 * k, 18 * k, G2, G4);
  s.ellipse(X(48), Y(-1), 12 * k, 9 * k, G2, G3);
  s.circle(X(58), Y(-2), 4.2f * k, hex(0xF08AA8));
  // vorderes Ohr
  s.circle(X(26), Y(-28), 14 * k, G3);
  s.circle(X(26), Y(-28), 9 * k, hex(0xF2A9C0), 0.8f);
  s.circle(X(40), Y(-10), 3.4f * k, INK);
  s.circle(X(39), Y(-11), 1.1f * k, WHITE);
  for (int i = -1; i <= 1; i++) s.capsule(X(52), Y(2), X(70), Y(-2 + i * 7), 0.7f * k, G5, 0.7f);
}

void drawRabbit(Scene& s, float x, float y, float k) {
  auto X = [&](float d) { return x + d * k; };
  auto Y = [&](float d) { return y + d * k; };
  s.ellipse(X(-9), Y(-70), 7.5f * k, 25 * k, G2, G3);
  s.ellipse(X(11), Y(-72), 7.5f * k, 25 * k, G2, G3);
  s.ellipse(X(-9), Y(-68), 3.5f * k, 18 * k, hex(0xF2A9C0), 0.7f);
  s.ellipse(X(11), Y(-70), 3.5f * k, 18 * k, hex(0xF2A9C0), 0.7f);
  s.ellipse(X(0), Y(10), 32 * k, 33 * k, G2, G4);
  s.ellipse(X(0), Y(20), 18 * k, 20 * k, G1, G2, 0.8f);
  s.circle(X(0), Y(-30), 21 * k, G2);
  s.circle(X(-8), Y(-34), 3 * k, INK);
  s.circle(X(8), Y(-34), 3 * k, INK);
  s.circle(X(0), Y(-24), 3 * k, hex(0xF08AA8));
  s.ellipse(X(-12), Y(40), 11 * k, 6 * k, G3);
  s.ellipse(X(12), Y(40), 11 * k, 6 * k, G3);
}

void drawElephant(Scene& s, float x, float y, float k, bool flip) {
  const float f = flip ? -1.f : 1.f;
  auto X = [&](float d) { return x + f * d * k; };
  auto Y = [&](float d) { return y + d * k; };
  s.capsule(X(56), Y(-6), X(64), Y(16), 2.2f * k, G5);
  for (float lx : {-22.f, 2.f, 26.f, 42.f}) s.capsule(X(lx), Y(18), X(lx), Y(48), 10 * k, lx == 2.f || lx == 42.f ? G4 : G3);
  s.ellipse(X(10), Y(0), 50 * k, 35 * k, G2, G4);
  s.circle(X(-38), Y(-12), 27 * k, G3);
  s.capsule(X(-58), Y(-4), X(-68), Y(20), 8.5f * k, G3);
  s.capsule(X(-68), Y(20), X(-64), Y(38), 6.5f * k, G3);
  s.capsule(X(-52), Y(8), X(-62), Y(17), 2.6f * k, hex(0xF6F2E8));
  s.ellipse(X(-22), Y(-10), 19 * k, 25 * k, G2, G4);
  s.circle(X(-48), Y(-20), 3.2f * k, INK);
  for (int i = 0; i < 3; i++) s.capsule(X(-66), Y(4 + i * 9), X(-58), Y(4 + i * 9), 1 * k, G5, 0.5f);
}

void drawOwl(Scene& s, float x, float y, float k, float awake, float t) {
  auto X = [&](float d) { return x + d * k; };
  auto Y = [&](float d) { return y + d * k; };
  const float puff = 1 + 0.06f * awake;
  // Ohrbüschel
  tri(s, X(-44), Y(-52), X(-24), Y(-62), X(-50), Y(-92 - 10 * awake), G4);
  tri(s, X(44), Y(-52), X(24), Y(-62), X(50), Y(-92 - 10 * awake), G4);
  // Körper, Flügel, Bauch
  s.ellipse(X(0), Y(0), 62 * k * puff, 78 * k * puff, G3, G5);
  s.ellipse(X(-52), Y(14), 18 * k, 50 * k, G4, G5);
  s.ellipse(X(52), Y(14), 18 * k, 50 * k, G4, G5);
  s.ellipse(X(0), Y(22), 40 * k, 50 * k, G1, G3);
  for (int r = 0; r < 3; r++)
    for (int i = -1; i <= 1; i++) {
      const float vx = i * 16 + (r % 2) * 8 - 4, vy = 6 + r * 16;
      s.capsule(X(vx - 5), Y(vy), X(vx), Y(vy + 5), 1.4f * k, G4);
      s.capsule(X(vx), Y(vy + 5), X(vx + 5), Y(vy), 1.4f * k, G4);
    }
  // Gesicht
  for (float ex : {-23.f, 23.f}) {
    s.circle(X(ex), Y(-32), 26 * k, G1);
    s.ring(X(ex), Y(-32), 26 * k, 3 * k, G3);
    if (awake > 0.5f) {
      const float g = ease((awake - 0.5f) * 2);
      s.circle(X(ex), Y(-32), 15 * k * g, YELLOW);
      s.circle(X(ex), Y(-32), 8 * k * g, INK);
      s.circle(X(ex - 3), Y(-35), 2.5f * k * g, WHITE);
    } else {
      s.arc(X(ex), Y(-40), 13 * k, 3.4f * k, 2.0f, 4.28f, G6);   // geschlossenes Auge: Bogen nach unten
    }
  }
  tri(s, X(-7), Y(-24), X(7), Y(-24), X(0), Y(-8), AMBER);
  for (float fx : {-16.f, 16.f})
    for (int i = -1; i <= 1; i++) s.capsule(X(fx), Y(74), X(fx + i * 6), Y(84), 2.6f * k, AMBER);
  (void)t;
}

void drawBat(Scene& s, float x, float y, float k, float sleep, bool hanging) {
  const float v = hanging ? -1.f : 1.f;   // hängend: Kopf unten
  auto X = [&](float d) { return x + d * k; };
  auto Y = [&](float d) { return y + v * d * k; };
  for (float f : {-1.f, 1.f}) {
    // Flügel: zwei Dreiecke und Knochen
    const float fold = sleep > 0.5f ? 0.45f : 1.f;
    tri(s, X(f * 14), Y(-10), X(f * (14 + 70 * fold)), Y(-34), X(f * (14 + 62 * fold)), Y(18), G6);
    tri(s, X(f * 14), Y(-6), X(f * (14 + 62 * fold)), Y(18), X(f * (14 + 30 * fold)), Y(30), G6);
    s.capsule(X(f * 14), Y(-10), X(f * (14 + 70 * fold)), Y(-34), 2 * k, G5);
    s.capsule(X(f * (14 + 40 * fold)), Y(-22), X(f * (14 + 62 * fold)), Y(18), 1.6f * k, G5);
    // Ohren
    tri(s, X(f * 6), Y(-46), X(f * 16), Y(-40), X(f * 16), Y(-66), G5);
    // Füße
    s.capsule(X(f * 6), Y(26), X(f * 8), Y(40), 2.2f * k, G6);
  }
  s.ellipse(X(0), Y(4), 20 * k, 30 * k, G4, G6);
  s.circle(X(0), Y(-34), 18 * k, G5);
  if (sleep > 0.5f) {
    for (float f : {-1.f, 1.f}) s.capsule(X(f * 4), Y(-36), X(f * 12), Y(-36), 1.8f * k, G1);
  } else {
    for (float f : {-1.f, 1.f}) {
      s.circle(X(f * 8), Y(-37), 6.5f * k, WHITE);
      s.circle(X(f * 8), Y(-36), 3 * k, INK);
      s.arc(X(f * 8), Y(-37), 8.5f * k, 1.6f * k, 2.2f, 4.1f, hex(0x7A6AA0), 0.7f);   // müde Ränder
    }
  }
  tri(s, X(-4), Y(-25), X(-1), Y(-25), X(-2.5f), Y(-20), WHITE);
  tri(s, X(1), Y(-25), X(4), Y(-25), X(2.5f), Y(-20), WHITE);
}

void drawDog(Scene& s, float x, float y, float k, float run, float t, bool asleep) {
  auto X = [&](float d) { return x + d * k; };
  auto Y = [&](float d) { return y + d * k; };
  const float low = asleep ? 10 : 0;
  // Schwanz
  const float wag = asleep ? 0.3f : std::sin(t * 16) * 0.6f;
  s.capsule(X(38), Y(-6 + low), X(38 + std::cos(-0.8f + wag) * 26), Y(-6 + low + std::sin(-0.8f + wag) * 26), 4 * k, G4);
  // Beine
  for (int i = 0; i < 4; i++) {
    const float lx = i < 2 ? -26 + i * 10 : 22 + (i - 2) * 10;
    const float sw = run * std::sin(t * 18 + i * 1.6f) * 9;
    if (asleep) s.capsule(X(lx - 10), Y(22), X(lx + 6), Y(22), 6 * k, i % 2 ? G4 : G3);
    else s.capsule(X(lx), Y(10), X(lx + sw), Y(36), 6 * k, i % 2 ? G4 : G3);
  }
  s.ellipse(X(0), Y(low), 44 * k, 23 * k, G2, G4);
  s.ellipse(X(14), Y(-4 + low), 16 * k, 12 * k, G5, 0.5f);   // Fleck
  // Kopf
  const float hy = asleep ? 2 : -22;
  s.circle(X(-44), Y(hy), 20 * k, G2);
  s.ellipse(X(-62), Y(hy + 6), 15 * k, 10 * k, G2, G3);
  s.circle(X(-75), Y(hy + 3), 5 * k, INK);
  s.ellipse(X(-36), Y(hy + 6), 9 * k, 18 * k, G5, G6);
  if (asleep) s.arc(X(-50), Y(hy - 2), 5 * k, 1.6f * k, 2.1f, 4.2f, INK);
  else {
    s.circle(X(-52), Y(hy - 5), 3.4f * k, INK);
    s.circle(X(-53), Y(hy - 6), 1.1f * k, WHITE);
    s.ellipse(X(-62), Y(hy + 18), 5 * k, 7 * k, hex(0xF27A9A));
  }
}

void drawEgg(Scene& s, float x, float y, float k, float crack, float wob) {
  const float sx = x + wob * 6 * k;
  auto X = [&](float d) { return sx + d * k; };
  auto Y = [&](float d) { return y + d * k; };
  s.ellipse(X(0), Y(6), 46 * k, 52 * k, hex(0xFBF8F0), hex(0xD8D0C0));
  s.ellipse(X(0), Y(-22), 38 * k, 40 * k, hex(0xFFFDF8), hex(0xEAE3D6));
  static const float SP[][3] = {{-20, -30, 4}, {12, -40, 3}, {24, -6, 5}, {-28, 14, 3.5f}, {4, 30, 4}, {-6, -6, 2.5f}, {30, 26, 3}};
  for (auto& p : SP) s.circle(X(p[0]), Y(p[1]), p[2] * k, G3, 0.5f);
  s.ellipse(X(-16), Y(-30), 8 * k, 14 * k, WHITE, 0.8f);
  if (crack > 0) {   // Zickzack-Riss quer über die Mitte
    static const float CR[] = {-44, 0, -30, -8, -18, 6, -4, -10, 8, 4, 20, -8, 32, 6, 44, -2};
    const int segs = 7;
    const float done = crack * segs;
    for (int i = 0; i < segs && i < done; i++) {
      const float t = std::fmin(1.f, done - i);
      s.capsule(X(CR[2 * i]), Y(CR[2 * i + 1]), X(lerpf(CR[2 * i], CR[2 * i + 2], t)), Y(lerpf(CR[2 * i + 1], CR[2 * i + 3], t)), 1.8f * k, G6);
    }
    if (crack > 0.6f) s.capsule(X(-4), Y(-10), X(-10), Y(-26 * (crack - 0.6f) / 0.4f - 10), 1.5f * k, G6);
  }
}

}  // namespace bc
