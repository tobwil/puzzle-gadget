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
  void startLevel(int i);   // für Tests und den seriellen Befehl „level N“
  void resetProgress();
  const char* debug();
  uint32_t doneMask() const { return done_; }
  int hints() const { return hints_; }
  void setLang(int l) { lang_ = l & 1; }
  dk::Motion motion;

 private:
  enum Screen { TITLE, PICK, PLAY, END };
  enum Overlay { NONE, HINT, SKIP, SOLVED };

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
  bool isDone(int i) const { return (done_ >> i) & 1; }
  bool unlocked(int i) const { return i == 0 || isDone(i) || isDone(i - 1); }
  int nextLevel() const;
  void save();
  void setSound(bool on);

  dk::Synth& synth_;
  dk::Store& store_;
  Level* levels_[LEVELS] = {};
  Screen screen_ = TITLE;
  Overlay overlay_ = NONE;
  float overlayT_ = 0;
  int cur_ = 0;
  uint32_t done_ = 0, hintUsed_ = 0;
  int hints_ = 15, lang_ = 0;
  bool sound_ = true;
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
