// Brain Candy: Spielablauf (Titel, Levelauswahl, Level mit Kopfzeile und Knöpfen, Tipps, Überspringen, Erfolg, Ende).
// Plattformunabhängig: Gerät und Simulator geben pro Frame Eingaben hinein und rastern die Scene.
#pragma once
#include "input.h"
#include "motion.h"
#include "synth.h"
#include "ui.h"

namespace bc {

class Game : Host {
 public:
  Game(dk::Synth& synth, dk::Store& store);
  ~Game() override;
  void tick(const dk::Input& in);
  void draw(Scene& s);

  int brightness() const { return bright_; }   // 0..255
  bool screenOff() const { return off_; }
  void startLevel(int i);   // für Tests und den seriellen Befehl „level N“ (ohne Aufgabenkarte)
  void enterLevel(int i);   // mit Aufgabenkarte (und vorgelesener Aufgabe)
  void resetProgress();
  const char* debug();
  // Alle Sätze der Sprachausgabe (Schlüssel wie bei Speaker::speak, Text), für tools/mkvoice.py
  static void voiceLines(void (*out)(const char* key, const char* text));
  uint64_t doneMask() const { return done_; }
  const char* levelAnswer() const { return levels_[cur_]->answer(); }
  int hints() const { return hints_; }
  void setLang(int l) { lang_ = l & 1; }
  void skipIntro() { if (screen_ == INTRO) screen_ = TITLE; }
  int difficulty() const { return diff_; }
  void setDifficulty(int d) { diff_ = (d % 3 + 3) % 3; store_.set("bc_diff", diff_); }
  dk::Motion motion;
  dk::Speaker* speaker = nullptr;

 private:
  enum Screen { TITLE, PICK, PLAY, END, INTRO };
  enum Overlay { NONE, HINT, SKIP, SOLVED, TASK };

  // Host
  void solve() override;
  void nope(float x, float y) override;
  void say(float x, float y, const Txt& t, Col c) override;
  void sfx(int id) override;

  void tap(float x, float y);            // Tipp auf die Oberfläche (außerhalb des Levels)
  void drawHeader(Scene& s, int level);
  void drawButtons(Scene& s);
  void drawTitle(Scene& s);
  void drawLevels(Scene& s);
  void drawEnd(Scene& s);
  void drawOverlay(Scene& s);
  void drawIntro(Scene& s);
  void drawTaskCard(Scene& s, float a);
  void speak(const char* what);
  int skipCost() const { return diff_ == 0 ? 1 : 3; }
  float playTime() const { return solvedAt_ >= 0 ? solvedAt_ : levelT_; }
  bool isDone(int i) const { return (done_ >> i) & 1; }
  bool hintUsed(int i) const { return (hintUsed_ >> i) & 1; }
  bool unlocked(int i) const { return i == 0 || isDone(i) || isDone(i - 1); }
  int nextLevel() const;
  void save();
  void setVolume(int v);

  dk::Synth& synth_;
  dk::Store& store_;
  Level* levels_[LEVELS] = {};
  Screen screen_ = TITLE;
  Overlay overlay_ = NONE;
  float overlayT_ = 0;
  int cur_ = 0;
  uint64_t done_ = 0, hintUsed_ = 0;   // je Level ein Bit (gespeichert als zwei 32-Bit-Werte)
  int hints_ = 15, lang_ = 0;
  bool sound_ = true;
  int vol_ = 2, diff_ = 1, introPage_ = 0, pickPage_ = 0;
  bool autoHinted_ = false, newBest_ = false;
  int bestT_ = 0;   // Bestzeit des aktuellen Levels in Zehntelsekunden (0 = keine)
  float lastNopeVoice_ = -10;
  float levelT_ = 0, solvedAt_ = -1, now_ = 0;
  const char* praise_ = "NICE!";
  int praiseN_ = 0;

  // Finger
  bool down_ = false, uiTouch_ = false, wake_ = false;
  float sx_ = 0, sy_ = 0, x_ = 0, y_ = 0, lx_ = 0, ly_ = 0, vx_ = 0, vy_ = 0;
  uint32_t downMs_ = 0;
  int pressedBtn_ = -1;
  Frame frame_;

  // Zurufe in Handschrift
  struct Remark { float x, y, t0; Txt txt; Col c; };
  Remark remarks_[5];
  int nRemarks_ = 0;
  float nopeX_ = 0, nopeY_ = 0, nopeT_ = -10;

  // Energie
  uint32_t lastActive_ = 0;
  int bright_ = 170;
  bool off_ = false;
  char dbg_[160];
};

}  // namespace bc
