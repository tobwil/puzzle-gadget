// Brain Candy: gemeinsame Bausteine für Spiel und Level (Farben, Eingabe pro Frame, Level-Schnittstelle, Zeichenhelfer).
// Look wie die iPhone-App: weißer Grund, Objekte in Graustufen wie ausgeschnittene Fotos, knallige Akzente
// (Cyan, Gelb, Pink), fette schwarze Montserrat-Schrift, Handschrift für Zurufe.
#pragma once
#include <cmath>
#include <cstdint>

#include "fonts.h"
#include "render.h"

namespace bc {
using dk::Col;
using dk::hex;
using dk::Scene;
using dk::W;
using dk::H;
using dk::PIF;
using dk::TAU;
using dk::Align;

// ---------------- Farben ----------------
constexpr Col WHITE = hex(0xFFFFFF), INK = hex(0x121212), BTN = hex(0xE6E6E6), BTN_DOWN = hex(0xCFCFCF);
constexpr Col CYAN = hex(0x14E6F0), YELLOW = hex(0xFFEE00), PINK = hex(0xFF7ADB), HOTPINK = hex(0xFF2E9E);
constexpr Col ORANGE = hex(0xF4581F), AMBER = hex(0xFFB000), PURPLE = hex(0x6B2BFF), MINT = hex(0x7BF5C8);
// Graustufen für die „Foto“-Objekte
constexpr Col G1 = hex(0xF2F2F2), G2 = hex(0xD9D9D9), G3 = hex(0xB5B5B5), G4 = hex(0x8C8C8C), G5 = hex(0x5E5E5E), G6 = hex(0x333333);

// ---------------- Sprache ----------------
struct Txt { const char* en; const char* de; };
inline const char* tr(const Txt& t, int lang) { return lang ? t.de : t.en; }

// ---------------- Eingabe eines Frames ----------------
struct Frame {
  uint32_t ms = 0;
  float dt = 0;       // s seit dem letzten Frame
  float t = 0;        // s seit Levelstart (oder Neustart)
  bool down = false;      // Finger liegt auf (und gehört dem Level)
  bool pressed = false;   // Finger ist in diesem Frame aufgesetzt
  bool released = false;  // Finger ist in diesem Frame losgelassen worden
  bool tap = false;       // kurzer Tipp ohne Bewegung (beim Loslassen)
  float x = 0, y = 0;     // Fingerposition (beim Loslassen: letzte)
  float sx = 0, sy = 0;   // wo der Finger aufgesetzt hat
  float vx = 0, vy = 0;   // Fingergeschwindigkeit px/s
  float hold = 0;         // s, die der Finger schon liegt
  float tiltX = 0, tiltY = 1, tiltZ = 0;   // Beschleunigung in g (x rechts, y oben, z aus dem Bildschirm)
  bool shaking = false;
  float shake = 0;
  int knocks = 0;         // Schläge auf den Tisch in diesem Frame
  float voice = 0;        // Mikrofonpegel 0..1
};

// ---------------- Level ----------------
enum Sfx { SFX_CLICK, SFX_POP, SFX_NOPE, SFX_WIN, SFX_HINT, SFX_WHOOSH, SFX_CRACK, SFX_CHOMP, SFX_SQUEAK, SFX_MAGIC, SFX_WOOF, SFX_HOOT };

struct Host {
  virtual void solve() = 0;                                // Level gelöst (Spiel zeigt danach „NICE!“)
  virtual void nope(float x, float y) = 0;                 // falscher Versuch: freundlicher Zuruf
  virtual void say(float x, float y, const Txt& t, Col c = INK) = 0;   // eigener Zuruf in Handschrift
  virtual void sfx(int id) = 0;
  virtual ~Host() = default;
};

class Level {
 public:
  virtual ~Level() = default;
  virtual Txt task() const = 0;
  virtual Txt hint() const = 0;
  virtual void reset() = 0;
  virtual void tick(const Frame& f, Host& h) = 0;
  virtual void draw(Scene& s, const Frame& f, int lang) = 0;
  // true: das Level zeichnet (und bewegt) den Aufgabentext selbst
  virtual bool ownsTask() const { return false; }
  bool solved = false;
};

constexpr int LEVELS = 12;
Level* makeLevel(int i);   // 0 .. LEVELS-1

// ---------------- Layout ----------------
constexpr float HEAD_Y = 40;       // Mitte der Kopfzeile („<  LVL.3  >>“)
constexpr float TASK_Y = 70;       // Oberkante des Aufgabentexts
constexpr float BTN_Y = 356, BTN_S = 50;   // Knöpfe unten (Restart, Hints)
constexpr float BTN_LX = 30, BTN_RX = W - 30 - BTN_S;

// ---------------- Zeichenhelfer (art.cpp) ----------------
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float ease(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
inline float dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

// Text mit Zeilenumbruch, zentriert um cx; gibt die Höhe zurück
float textWrap(Scene& s, const fonts::Font& f, const char* str, float cx, float y, float maxW, Col c, float a = 1);
// Pinselstrich durch Punkte (Cyan-Schnörkel wie in der App), Dicke schwankt leicht
void scribble(Scene& s, const float* pts, int n, float thick, Col c, float a = 1);
// Sternchen-Funkeln (wie die schwarzen Sternchen im App-Symbol)
void sparkle(Scene& s, float x, float y, float r, Col c, float a = 1, float rot = 0);
// Strahlen rund um einen Punkt (gelbe Striche wie beim Herz)
void rays(Scene& s, float x, float y, float r0, float r1, int n, float thick, Col c, float a = 1, float rot = 0);
// Dreieck aus Kapseln (für Ohren, Zähne, Zacken)
void tri(Scene& s, float ax, float ay, float bx, float by, float cx, float cy, Col c, float a = 1);

// Symbole der Bedienung
void iconBack(Scene& s, float x, float y, Col c);
void iconSkip(Scene& s, float x, float y, Col c);
void iconRestart(Scene& s, float x, float y, Col c);
void iconBulb(Scene& s, float x, float y, float k, Col c);
void iconSpeaker(Scene& s, float x, float y, bool on, Col c);
void iconGrid(Scene& s, float x, float y, Col c);

// Figuren (Graustufen wie Fotos). k = Größe (1 = normal)
void drawCandy(Scene& s, float x, float y, float k, Col c = PINK, float a = 1);
void drawDinoHead(Scene& s, float x, float y, float k, float open, float blink = 0);   // schaut nach links, Maul bei (x - 60k, y + 20k)
void drawDino(Scene& s, float x, float y, float k, float mood, bool flip = false);     // ganzer T-Rex, mood: 0 friert, 1 froh
void drawMouse(Scene& s, float x, float y, float k, bool flip = false);
void drawRabbit(Scene& s, float x, float y, float k);
void drawElephant(Scene& s, float x, float y, float k, bool flip = false);
void drawOwl(Scene& s, float x, float y, float k, float awake, float t);
void drawBat(Scene& s, float x, float y, float k, float sleep, bool hanging);
void drawDog(Scene& s, float x, float y, float k, float run, float t, bool asleep);
void drawEgg(Scene& s, float x, float y, float k, float crack, float wob);

}  // namespace bc
