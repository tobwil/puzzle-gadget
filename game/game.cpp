#include "game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace bc {

namespace {
// Knöpfe der Oberfläche
enum Btn { B_BACK, B_SKIP, B_RESTART, B_HINT, B_PLAY = 10, B_GRID, B_LANG, B_SOUND, B_YES = 30, B_NO, B_TILE = 40 };
constexpr float VOLS[4] = {0, 0.25f, 0.55f, 1.0f};

constexpr uint32_t DIM_MS = 90000, OFF_MS = 240000;
constexpr int BRIGHT = 170, BRIGHT_DIM = 40;

// Kacheln der Levelauswahl
constexpr float TW = 92, TH = 62, TGX = 14, TGY = 12, TX0 = (W - 3 * TW - 2 * TGX) / 2, TY0 = 86;

const Txt T_RESTART{"Restart", "Neustart"}, T_HINTS{"Hints", "Tipps"};
const Txt T_PLAY{"PLAY", "SPIELEN"}, T_SUB{"logic puzzles", "Logikrätsel"}, T_LEVELS{"LEVELS", "LEVEL"};
const Txt T_HINT{"HINT", "TIPP"}, T_NOHINT{"No hints left. Every solved level gives you one.", "Keine Tipps mehr. Für jedes gelöste Level gibt es einen."};
const Txt T_SKIPQ{"Skip this level?", "Level überspringen?"}, T_COST{"costs 3 hints", "kostet 3 Tipps"}, T_NOSKIP{"You need 3 hints to skip.", "Zum Überspringen brauchst du 3 Tipps."};
const Txt T_COST1{"costs 1 hint", "kostet 1 Tipp"}, T_NOSKIP1{"You need 1 hint to skip.", "Zum Überspringen brauchst du 1 Tipp."};
const Txt T_DIFF[3] = {{"EASY", "LEICHT"}, {"NORMAL", "NORMAL"}, {"HARD", "SCHWER"}};
const Txt T_GO{"GO!", "LOS!"}, T_MAYBE{"This might help:", "Das könnte helfen:"}, T_CLOCK{"The clock starts with GO.", "Die Zeit läuft ab LOS."};
const Txt T_BEST{"Best", "Bestzeit"}, T_RECORD{"NEW RECORD!", "NEUER REKORD!"}, T_TIME{"Time", "Zeit"};
const Txt T_TOOLS[7] = {{"finger", "Finger"}, {"shake", "schütteln"}, {"tilt", "kippen"}, {"voice", "pusten/rufen"},
                        {"knock", "klopfen"}, {"think", "nachdenken"}, {"patience", "Geduld"}};
const Txt T_SKIPINTRO{"Skip", "Überspringen"}, T_NEXTP{"NEXT", "WEITER"};
struct IntroPage { Txt title, body; };
const IntroPage INTRO_PAGES[3] = {
    {{"Think outside the box", "Um die Ecke denken"},
     {"Every level is a little trick. The obvious answer is rarely the right one – read each task carefully!",
      "Jedes Level ist ein kleiner Trick. Die naheliegende Antwort ist selten die richtige – lies jede Aufgabe genau!"}},
    {{"Use the whole device", "Nutz das ganze Gerät"},
     {"Drag, tap and hold – but also shake, tilt, turn it over, blow at it or knock on the table.",
      "Ziehen, tippen, halten – aber auch schütteln, kippen, umdrehen, anpusten oder auf den Tisch klopfen."}},
    {{"Stuck?", "Hängst du fest?"},
     {"The bulb shows a hint, >> skips a level. Choose the difficulty on the title screen.",
      "Die Glühbirne zeigt einen Tipp, mit >> überspringst du ein Level. Die Schwierigkeit wählst du auf dem Titelbild."}},
};
const Txt T_YES{"YES", "JA"}, T_NO{"NO", "NEIN"}, T_OK{"OK", "OK"};
const Txt T_SOLVED{"Level solved!", "Level geschafft!"}, T_PLUS{"+1 hint", "+1 Tipp"}, T_NEXT{"NEXT", "WEITER"};
const Txt T_GENIUS{"GENIUS!", "GENIAL!"}, T_ALL{"All 24 puzzles solved.", "Alle 24 Rätsel gelöst."}, T_BACKTAP{"Tap to go back", "Tippen zum Start"};
const char* const PRAISE[2][5] = {{"NICE!", "BRAVO!", "WOW!", "SMART!", "GENIUS!"}, {"STARK!", "BRAVO!", "WOW!", "SUPER!", "GENIAL!"}};
const Txt NOPES[] = {{"Nope!", "Nö!"}, {"Hmm… no.", "Hmm… nein."}, {"Not quite!", "Nicht ganz!"}, {"Try again!", "Nochmal!"}};

bool inRect(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

// Knöpfe der Overlays (Ja/Nein bzw. OK/Weiter)
constexpr float OB_Y = 318, OB_H = 52;
}  // namespace

Game::Game(dk::Synth& synth, dk::Store& store) : synth_(synth), store_(store) {
  for (int i = 0; i < LEVELS; i++) levels_[i] = makeLevel(i);
  done_ = uint32_t(store_.get("bc_done", 0));
  hintUsed_ = uint32_t(store_.get("bc_used", 0));
  hints_ = store_.get("bc_hints", 15);
  lang_ = store_.get("bc_lang", 0);
  vol_ = store_.get("bc_vol", store_.get("bc_snd", 1) ? 2 : 0) & 3;
  diff_ = store_.get("bc_diff", 1) % 3;
  cur_ = store_.get("bc_cur", 0) % LEVELS;
  setVolume(vol_);
  if (!store_.get("bc_intro", 0)) screen_ = INTRO;
  motion.threshold = 0.12f;
}

Game::~Game() { for (auto* l : levels_) delete l; }

void Game::save() {
  store_.set("bc_done", int(done_));
  store_.set("bc_used", int(hintUsed_));
  store_.set("bc_hints", hints_);
  store_.set("bc_cur", cur_);
}

void Game::setVolume(int v) {
  vol_ = v & 3;
  sound_ = vol_ > 0;
  synth_.setVolume(VOLS[vol_]);
}

void Game::speak(const char* what) {
  if (!sound_ || !speaker) return;
  char k[16];
  std::snprintf(k, sizeof k, "%s_%s", lang_ ? "de" : "en", what);
  speaker->speak(k);
}

void Game::enterLevel(int i) {
  startLevel(i);
  overlay_ = TASK;
  overlayT_ = 0;
  char k[8];
  std::snprintf(k, sizeof k, "t%02d", cur_ + 1);
  speak(k);
}

void Game::resetProgress() {
  done_ = hintUsed_ = 0;
  hints_ = 15;
  cur_ = 0;
  save();
  screen_ = TITLE;
  overlay_ = NONE;
}

int Game::nextLevel() const {
  for (int i = 0; i < LEVELS; i++) if (!isDone(i)) return i;
  return cur_;
}

void Game::startLevel(int i) {
  cur_ = (i % LEVELS + LEVELS) % LEVELS;
  levels_[cur_]->reset();
  levels_[cur_]->solved = false;
  levelT_ = 0;
  solvedAt_ = -1;
  overlay_ = NONE;
  screen_ = PLAY;
  nRemarks_ = 0;
  autoHinted_ = newBest_ = false;
  char k[8];
  std::snprintf(k, sizeof k, "bc_t%d", cur_);
  bestT_ = store_.get(k, 0);
  store_.set("bc_cur", cur_);
}

// ---------------- Host ----------------
void Game::solve() {
  Level* l = levels_[cur_];
  if (l->solved) return;
  l->solved = true;
  solvedAt_ = levelT_;
  const int pi = praiseN_++ % 5;
  praise_ = PRAISE[lang_][pi];
  char k[8];
  std::snprintf(k, sizeof k, "p%d", pi);
  speak(k);
  if (!isDone(cur_)) { done_ |= 1u << cur_; hints_++; save(); }
  if (diff_ == 2) {
    const int t = int(solvedAt_ * 10 + 0.5f);
    if (bestT_ == 0 || t < bestT_) {
      newBest_ = bestT_ != 0;
      bestT_ = t;
      std::snprintf(k, sizeof k, "bc_t%d", cur_);
      store_.set(k, t);
    }
  }
}

void Game::nope(float x, float y) {
  const int n = (int(now_ * 7)) % 4;
  say(x, y, NOPES[n], INK);
  sfx(SFX_NOPE);
  if (now_ - lastNopeVoice_ > 2.5f) {
    lastNopeVoice_ = now_;
    char k[8];
    std::snprintf(k, sizeof k, "n%d", n);
    speak(k);
  }
}

void Game::say(float x, float y, const Txt& t, Col c) {
  if (nRemarks_ == 5) { std::memmove(remarks_, remarks_ + 1, sizeof(Remark) * 4); nRemarks_ = 4; }
  remarks_[nRemarks_++] = {clampf(x, 40, W - 40), clampf(y, 100, 330), now_, t, c};
}

void Game::sfx(int id) {
  if (!sound_) return;
  using V = dk::Synth::Voice;
  auto play = [&](V v) { synth_.play(v); };
  V v;
  switch (id) {
    case SFX_CLICK: v.f0 = 1300; v.f1 = 800; v.dur = 0.05f; v.vol = 0.22f; play(v); break;
    case SFX_POP: v.f0 = 420; v.f1 = 1200; v.dur = 0.09f; v.vol = 0.28f; play(v); break;
    case SFX_NOPE:
      v.wave = dk::Synth::SQUARE; v.filt = dk::Synth::LP; v.cut0 = v.cut1 = 900; v.f0 = 230; v.f1 = 150; v.dur = 0.2f; v.vol = 0.12f; play(v); break;
    case SFX_WIN: {
      const float N[] = {660, 880, 1320};
      for (int i = 0; i < 3; i++) { V n; n.wave = dk::Synth::TRI; n.f0 = n.f1 = N[i]; n.delay = i * 0.09f; n.dur = 0.3f; n.vol = 0.25f; play(n); }
      break;
    }
    case SFX_HINT: v.f0 = v.f1 = 1500; v.dur = 0.35f; v.vol = 0.18f; play(v); v.f0 = v.f1 = 2250; v.delay = 0.06f; play(v); break;
    case SFX_WHOOSH:
      v.wave = dk::Synth::NOISE; v.filt = dk::Synth::BP; v.cut0 = 700; v.cut1 = 2600; v.q = 1.2f; v.attack = 0.05f; v.dur = 0.35f; v.vol = 0.3f; play(v); break;
    case SFX_CRACK: v.wave = dk::Synth::NOISE; v.filt = dk::Synth::HP; v.cut0 = v.cut1 = 2500; v.dur = 0.05f; v.vol = 0.35f; play(v); break;
    case SFX_CHOMP:
      for (int i = 0; i < 2; i++) { V n; n.wave = dk::Synth::NOISE; n.filt = dk::Synth::LP; n.cut0 = n.cut1 = 700; n.dur = 0.08f; n.vol = 0.4f; n.delay = i * 0.14f; play(n); }
      break;
    case SFX_SQUEAK: v.f0 = 2200; v.f1 = 3100; v.dur = 0.12f; v.vol = 0.18f; v.lfoF = 30; v.lfoD = 120; play(v); v.delay = 0.15f; play(v); break;
    case SFX_MAGIC: {
      const float N[] = {880, 1175, 1480, 1760, 2350};
      for (int i = 0; i < 5; i++) { V n; n.f0 = n.f1 = N[i]; n.delay = i * 0.06f; n.dur = 0.45f; n.vol = 0.15f; play(n); }
      break;
    }
    case SFX_WOOF:
      for (int i = 0; i < 2; i++) { V n; n.wave = dk::Synth::SAW; n.filt = dk::Synth::LP; n.cut0 = 1200; n.cut1 = 500; n.f0 = 280; n.f1 = 150; n.dur = 0.16f; n.vol = 0.3f; n.delay = i * 0.22f; play(n); }
      break;
    case SFX_HOOT: v.f0 = 440; v.f1 = 390; v.dur = 0.3f; v.vol = 0.3f; v.attack = 0.03f; play(v); v.f0 = 420; v.f1 = 370; v.delay = 0.4f; v.dur = 0.45f; play(v); break;
  }
}

// ---------------- Eingabe ----------------
static int btnAtPlay(float x, float y, int diff) {
  if (y < 72 && x < 76) return B_BACK;
  if (y < 72 && x > W - 76) return diff == 2 ? -1 : B_SKIP;
  if (inRect(x, y, BTN_LX - 6, BTN_Y - 6, BTN_S + 12, BTN_S + 30)) return B_RESTART;
  if (inRect(x, y, BTN_RX - 6, BTN_Y - 6, BTN_S + 12, BTN_S + 30)) return B_HINT;
  return -1;
}

void Game::tick(const dk::Input& in) {
  const float dt = now_ == 0 ? 0.033f : clampf((in.ms - frame_.ms) * 0.001f, 0.001f, 0.1f);
  now_ = in.ms * 0.001f;
  if (overlay_ != NONE) overlayT_ += dt;

  Frame f;
  f.ms = in.ms;
  f.dt = dt;
  bool pressed = false, released = false;
  float px = 0, py = 0;
  for (int i = 0; i < in.nTouch; i++) {
    const auto& t = in.touch[i];
    lastActive_ = in.ms;
    if (t.down && !down_) {
      down_ = true;
      if (off_) { wake_ = true; off_ = false; }   // aus dem Schwarzen: erster Tipp weckt nur
      pressed = true; px = sx_ = x_ = lx_ = t.x; py = sy_ = y_ = ly_ = t.y; downMs_ = t.ms;
      vx_ = vy_ = 0;
    } else if (t.down) {
      x_ = t.x; y_ = t.y;
    } else if (!t.down && down_) {
      down_ = false;
      released = true;
    }
  }
  if (down_ || released) {
    const float nvx = (x_ - lx_) / dt, nvy = (y_ - ly_) / dt;
    vx_ += (nvx - vx_) * 0.6f; vy_ += (nvy - vy_) * 0.6f;
    lx_ = x_; ly_ = y_;
  }
  const bool isTap = released && dist(x_, y_, sx_, sy_) < 22 && in.ms - downMs_ < 600;

  // Energie: nach 90 s dunkler, nach 4 min aus
  if (in.ms - lastActive_ > OFF_MS) off_ = true;
  bright_ = off_ ? 0 : in.ms - lastActive_ > DIM_MS ? BRIGHT_DIM : BRIGHT;

  motion.feed(in.acc, in.nAcc, down_ || released);
  if (motion.shaking || in.voice > 0.4f) lastActive_ = in.ms;

  if (wake_) {   // die weckende Berührung zählt nicht
    if (released) wake_ = false;
    pressed = released = false;
  }

  // Wem gehört der Finger?
  if (pressed) {
    if (overlay_ != NONE || screen_ != PLAY) { uiTouch_ = true; pressedBtn_ = 0; }
    else { pressedBtn_ = btnAtPlay(px, py, diff_); uiTouch_ = pressedBtn_ >= 0; }
  }
  if (released && uiTouch_) {
    const float dx = x_ - sx_;
    const bool swipe = !isTap && std::fabs(dx) > 60 && std::fabs(dx) > std::fabs(y_ - sy_);
    if (swipe && screen_ == INTRO) { introPage_ = clampf(introPage_ + (dx < 0 ? 1 : -1), 0, 2); sfx(SFX_CLICK); }
    else if (swipe && screen_ == PICK) { pickPage_ = dx < 0 ? 1 : 0; sfx(SFX_CLICK); }
    else if (isTap) tap(x_, y_);
    uiTouch_ = false;
    pressedBtn_ = -1;
  }

  if (screen_ == PLAY && overlay_ != TASK) {
    Level* l = levels_[cur_];
    levelT_ += dt;
    f.t = levelT_;
    const bool mine = !uiTouch_ && !wake_ && overlay_ == NONE && !l->solved;
    if (mine) {
      f.down = down_;
      f.pressed = pressed;
      f.released = released;
      f.tap = isTap;
      f.x = x_; f.y = y_; f.sx = sx_; f.sy = sy_;
      f.vx = vx_; f.vy = vy_;
      f.hold = down_ ? (in.ms - downMs_) * 0.001f : 0;
    }
    if (overlay_ == NONE && !l->solved) {
      f.tiltX = motion.tiltX; f.tiltY = motion.tiltY; f.tiltZ = motion.tiltZ;
      f.shaking = motion.shaking; f.shake = motion.shake;
      f.knocks = motion.nKnocks;
      f.voice = in.voice;
    }
    l->tick(f, *this);
    if (l->solved && overlay_ == NONE && levelT_ - solvedAt_ > 1.1f) { overlay_ = SOLVED; overlayT_ = 0; sfx(SFX_WIN); }
    // Leicht: nach 30 s ohne Lösung kommt der Tipp von selbst (gratis)
    if (diff_ == 0 && !l->solved && overlay_ == NONE && !autoHinted_ && levelT_ > 30) {
      autoHinted_ = true;
      hintUsed_ |= 1u << cur_;
      save();
      overlay_ = HINT; overlayT_ = 0; sfx(SFX_HINT);
    }
  }
  frame_ = f;
  frame_.ms = in.ms;
  // alte Zurufe entfernen
  int k = 0;
  for (int i = 0; i < nRemarks_; i++) if (now_ - remarks_[i].t0 < 1.6f) remarks_[k++] = remarks_[i];
  nRemarks_ = k;
}

void Game::tap(float x, float y) {
  if (overlay_ == TASK) { if (overlayT_ > 0.3f) { overlay_ = NONE; sfx(SFX_POP); } return; }
  if (overlay_ == HINT) { overlay_ = NONE; sfx(SFX_CLICK); return; }
  if (overlay_ == SKIP) {
    const bool can = hints_ >= skipCost();
    if (can && inRect(x, y, 40, OB_Y, 136, OB_H)) {   // Ja
      hints_ -= skipCost();
      done_ |= 1u << cur_;
      save();
      sfx(SFX_WHOOSH);
      if (cur_ + 1 < LEVELS) enterLevel(cur_ + 1);
      else { screen_ = END; overlay_ = NONE; }
    } else if (!can || inRect(x, y, W - 176, OB_Y, 136, OB_H)) {
      overlay_ = NONE;
      sfx(SFX_CLICK);
    }
    return;
  }
  if (overlay_ == SOLVED) {
    if (overlayT_ < 0.4f) return;
    sfx(SFX_CLICK);
    const int n = nextLevel();
    if (done_ == (1u << LEVELS) - 1 && cur_ == LEVELS - 1) { screen_ = END; overlay_ = NONE; }
    else enterLevel(cur_ + 1 < LEVELS && !isDone(cur_ + 1) ? cur_ + 1 : n);
    return;
  }
  switch (screen_) {
    case INTRO:
      if (inRect(x, y, W - 150, 0, 150, 60) || (introPage_ == 2 && inRect(x, y, W / 2 - 100, 376, 200, 64))) {
        store_.set("bc_intro", 1);
        screen_ = TITLE;
        introPage_ = 0;
        sfx(SFX_POP);
      } else if (inRect(x, y, W / 2 - 100, 376, 200, 64)) { introPage_++; sfx(SFX_CLICK); }
      break;
    case TITLE:
      if (inRect(x, y, 30, 350, 220, 64)) { sfx(SFX_POP); enterLevel(nextLevel()); }
      else if (inRect(x, y, 262, 350, 80, 64)) { sfx(SFX_CLICK); screen_ = PICK; pickPage_ = nextLevel() / 12; }
      else if (inRect(x, y, 280, 8, 80, 56)) { lang_ ^= 1; store_.set("bc_lang", lang_); sfx(SFX_CLICK); }
      else if (inRect(x, y, 214, 8, 62, 56)) { setVolume((vol_ + 1) & 3); store_.set("bc_vol", vol_); sfx(SFX_CLICK); }
      else if (inRect(x, y, 156, 8, 56, 56)) { screen_ = INTRO; introPage_ = 0; sfx(SFX_CLICK); }
      else if (inRect(x, y, 22, 190, 150, 46)) { setDifficulty(diff_ + 1); sfx(SFX_CLICK); }
      break;
    case PICK:
      if (y < 72 && x < 80) { screen_ = TITLE; sfx(SFX_CLICK); break; }
      if (y > 382) { pickPage_ = x < W / 2 ? 0 : 1; sfx(SFX_CLICK); break; }
      for (int k = 0; k < 12; k++) {
        const int i = pickPage_ * 12 + k;
        const float tx = TX0 + (k % 3) * (TW + TGX), ty = TY0 + (k / 3) * (TH + TGY);
        if (i < LEVELS && inRect(x, y, tx - 4, ty - 4, TW + 8, TH + 8)) {
          if (unlocked(i)) { sfx(SFX_POP); enterLevel(i); }
          else sfx(SFX_NOPE);
        }
      }
      break;
    case END:
      screen_ = TITLE;
      break;
    case PLAY:
      switch (btnAtPlay(x, y, diff_)) {
        case B_BACK: screen_ = PICK; sfx(SFX_CLICK); break;
        case B_SKIP: overlay_ = SKIP; overlayT_ = 0; sfx(SFX_CLICK); break;
        case B_RESTART: startLevel(cur_); sfx(SFX_CLICK); break;
        case B_HINT:
          if (diff_ == 2) { sfx(SFX_NOPE); break; }   // Schwer: keine Tipps
          if (diff_ == 0 && !((hintUsed_ >> cur_) & 1)) { hintUsed_ |= 1u << cur_; save(); }   // Leicht: gratis
          if (!((hintUsed_ >> cur_) & 1) && hints_ > 0) { hints_--; hintUsed_ |= 1u << cur_; save(); }
          overlay_ = HINT; overlayT_ = 0; sfx(SFX_HINT);
          break;
      }
      break;
  }
}

// ---------------- Zeichnen ----------------
void Game::drawHeader(Scene& s, int level) {
  const bool backDown = down_ && uiTouch_ && pressedBtn_ == B_BACK, skipDown = down_ && uiTouch_ && pressedBtn_ == B_SKIP;
  if (backDown) s.circle(36, HEAD_Y, 22, BTN);
  if (skipDown) s.circle(W - 36, HEAD_Y, 22, BTN);
  iconBack(s, 36, HEAD_Y, INK);
  if (level >= 0) {
    if (diff_ != 2) iconSkip(s, W - 36, HEAD_Y, INK);
    char b[16];
    std::snprintf(b, sizeof b, "LVL.%d", level + 1);
    const float tw = Scene::textW(fonts::heavy, b);
    s.capsule(W / 2 - tw / 2 - 3, HEAD_Y + 5, W / 2 + tw / 2 + 3, HEAD_Y + 3, 8, CYAN);
    s.text(fonts::heavy, b, W / 2, HEAD_Y - 18, INK, 1, Align::Center);
  }
}

void Game::drawButtons(Scene& s) {
  const bool rDown = down_ && uiTouch_ && pressedBtn_ == B_RESTART, hDown = down_ && uiTouch_ && pressedBtn_ == B_HINT;
  s.rrect(BTN_LX, BTN_Y, BTN_S, BTN_S, 15, rDown ? BTN_DOWN : BTN);
  iconRestart(s, BTN_LX + BTN_S / 2, BTN_Y + BTN_S / 2, INK);
  s.text(fonts::label, tr(T_RESTART, lang_), BTN_LX + BTN_S / 2, BTN_Y + BTN_S + 3, INK, 1, Align::Center);
  if (diff_ == 2) {   // Schwer: Stoppuhr statt Tipps
    const int t = int(playTime());
    char b[12];
    std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    s.rrect(BTN_RX - 34, BTN_Y + 6, BTN_S + 34, 38, 19, INK);
    s.text(fonts::num, b, BTN_RX + (BTN_S - 34) / 2, BTN_Y + 13, WHITE, 1, Align::Center);
    s.text(fonts::label, tr(T_TIME, lang_), BTN_RX + (BTN_S - 34) / 2, BTN_Y + BTN_S + 3, INK, 1, Align::Center);
    return;
  }
  s.rrect(BTN_RX, BTN_Y, BTN_S, BTN_S, 15, hDown ? BTN_DOWN : BTN);
  iconBulb(s, BTN_RX + BTN_S / 2, BTN_Y + BTN_S / 2 - 1, 1, INK);
  s.text(fonts::label, tr(T_HINTS, lang_), BTN_RX + BTN_S / 2, BTN_Y + BTN_S + 3, INK, 1, Align::Center);
  // Zähler wie in der App: Cyan-Kreis mit kleinen Strahlen
  const float bx = BTN_RX + BTN_S - 4, by = BTN_Y + 6;
  s.capsule(bx + 10, by - 16, bx + 13, by - 21, 1.3f, INK);
  s.capsule(bx + 16, by - 9, bx + 22, by - 11, 1.3f, INK);
  s.circle(bx, by, 12.5f, INK);
  s.circle(bx, by, 11, CYAN);
  char b[8];
  std::snprintf(b, sizeof b, "%d", hints_);
  s.text(fonts::label, b, bx, by - 9, INK, 1, Align::Center);
}

void Game::drawTitle(Scene& s) {
  const float t = now_;
  // gelber Kreis, Schnörkel hinter dem Dino wie beim App-Symbol
  s.circle(312, 168, 30, YELLOW);
  const float sc[] = {128, 340, 132, 250, 176, 196, 250, 182, 318, 216, 352, 300};
  scribble(s, sc, 6, 11, CYAN, 0.95f);
  s.text(fonts::huge, "BRAIN", 26, 52, INK);
  s.text(fonts::huge, "CANDY", 26, 98, INK);
  s.text(fonts::hand, tr(T_SUB, lang_), 30, 150, INK);
  // Dino mit Bonbon (App-Symbol)
  const float dk = 0.95f, dx = 246, dy = 262;
  drawDinoHead(s, dx, dy, dk, 0.5f + 0.05f * std::sin(t * 2), std::fmod(t, 5.f) < 0.12f ? 1 : 0);
  drawCandy(s, dx - 34 * dk, dy + 28 * dk, 1.35f, PINK);
  sparkle(s, 58, 262, 12, INK, 1, 0.2f);
  sparkle(s, 96, 300, 8, INK, 1, 0.6f);
  sparkle(s, 344, 250, 9, INK, 1, 0.1f);
  // Schwierigkeit: drei Balken und Name
  static const Col DC[3] = {MINT, CYAN, HOTPINK};
  s.rrect(30, 196, 126, 34, 17, BTN);
  for (int i = 0; i < 3; i++) s.rrect(44 + i * 7, 220 - 6 - i * 4, 5, 8 + i * 4, 1.5f, i <= diff_ ? DC[diff_] : G3);
  s.text(fonts::label, tr(T_DIFF[diff_], lang_), 104, 204, INK, 1, Align::Center);
  // oben rechts: Anleitung, Ton und Sprache
  s.circle(184, 35, 17, BTN);
  s.text(fonts::heavy, "?", 184, 18, INK, 1, Align::Center);
  iconSpeaker(s, 240, 34, vol_, INK);
  s.rrect(290, 20, 50, 30, 15, BTN);
  s.text(fonts::label, lang_ ? "DE" : "EN", 315, 26, INK, 1, Align::Center);
  // Spielen und Levelauswahl
  const bool pDown = down_ && uiTouch_ && inRect(sx_, sy_, 30, 350, 220, 64);
  s.rrect(36, 356, 212, 56, 28, pDown ? G6 : INK);
  s.text(fonts::heavy, tr(T_PLAY, lang_), 142, 362, WHITE, 1, Align::Center);
  char b[24];
  std::snprintf(b, sizeof b, "LVL.%d", nextLevel() + 1);
  s.text(fonts::label, b, 142, 391, CYAN, 1, Align::Center);
  s.rrect(268, 356, 56, 56, 16, BTN);
  iconGrid(s, 296, 384, INK);
}

void Game::drawLevels(Scene& s) {
  drawHeader(s, -1);
  const float tw = Scene::textW(fonts::heavy, tr(T_LEVELS, lang_));
  s.capsule(W / 2 - tw / 2 - 3, HEAD_Y + 5, W / 2 + tw / 2 + 3, HEAD_Y + 3, 8, YELLOW);
  s.text(fonts::heavy, tr(T_LEVELS, lang_), W / 2, HEAD_Y - 18, INK, 1, Align::Center);
  const int next = nextLevel();
  for (int p = 0; p < 2; p++) {   // Seitenwahl unten
    const bool on = p == pickPage_;
    const float bx = W / 2 - 104 + p * 112;
    s.rrect(bx, 390, 96, 36, 18, on ? INK : BTN);
    s.text(fonts::label, p ? "13 – 24" : "1 – 12", bx + 48, 399, on ? WHITE : INK, 1, Align::Center);
  }
  for (int k = 0; k < 12; k++) {
    const int i = pickPage_ * 12 + k;
    if (i >= LEVELS) break;
    const float x = TX0 + (k % 3) * (TW + TGX), y = TY0 + (k / 3) * (TH + TGY);
    const bool done = isDone(i), open = unlocked(i);
    s.rrect(x, y, TW, TH, 16, done ? INK : open ? WHITE : G1);
    if (!done && open) { s.rrect(x, y, TW, TH, 16, INK); s.rrect(x + 3, y + 3, TW - 6, TH - 6, 13, i == next ? hex(0xE9FDFE) : WHITE); }
    char b[4];
    std::snprintf(b, sizeof b, "%d", i + 1);
    s.text(fonts::num, b, x + TW / 2, y + TH / 2 - 14, done ? WHITE : open ? INK : G3, 1, Align::Center);
    if (done) { s.capsule(x + TW - 22, y + 14, x + TW - 17, y + 19, 2.2f, CYAN); s.capsule(x + TW - 17, y + 19, x + TW - 9, y + 9, 2.2f, CYAN); }
  }
}

void Game::drawEnd(Scene& s) {
  s.circle(96, 132, 52, YELLOW);
  s.text(fonts::huge, tr(T_GENIUS, lang_), W / 2, 96, INK, 1, Align::Center);
  s.text(fonts::hand, tr(T_ALL, lang_), W / 2, 152, INK, 1, Align::Center);
  drawDino(s, W / 2, 284, 1.1f, 1);
  for (int i = 0; i < 6; i++) sparkle(s, 44 + i * 56, 210 + (i % 2) * 40, 8 + i % 3 * 3, i % 2 ? CYAN : INK, 1, now_ + i);
  s.text(fonts::label, tr(T_BACKTAP, lang_), W / 2, 392, G4, 1, Align::Center);
}

void Game::drawOverlay(Scene& s) {
  const float a = ease(overlayT_ / 0.2f);
  s.rrect(0, 0, W, H, 0, WHITE, 0.94f * a);
  if (overlay_ == HINT) {
    const bool has = (hintUsed_ >> cur_) & 1;
    s.circle(W / 2, 130, 42, YELLOW, a);
    rays(s, W / 2, 130, 52, 64, 9, 2.6f, INK, a, 0.2f);
    iconBulb(s, W / 2, 132, 1.6f, INK);
    s.text(fonts::heavy, tr(T_HINT, lang_), W / 2, 190, INK, a, Align::Center);
    textWrap(s, fonts::hand, has ? tr(levels_[cur_]->hint(), lang_) : tr(T_NOHINT, lang_), W / 2, 232, 300, INK, a);
    s.rrect(W / 2 - 60, OB_Y + 14, 120, 48, 24, INK, a);
    s.text(fonts::heavy, tr(T_OK, lang_), W / 2, OB_Y + 21, WHITE, a, Align::Center);
  } else if (overlay_ == SKIP) {
    iconSkip(s, W / 2, 130, INK);
    s.circle(W / 2, 130, 40, CYAN, 0.25f * a);
    s.text(fonts::heavy, tr(T_SKIPQ, lang_), W / 2, 186, INK, a, Align::Center);
    if (hints_ >= skipCost()) {
      s.text(fonts::hand, tr(skipCost() == 1 ? T_COST1 : T_COST, lang_), W / 2, 232, INK, a, Align::Center);
      s.rrect(40, OB_Y, 136, OB_H, 26, INK, a);
      s.text(fonts::heavy, tr(T_YES, lang_), 108, OB_Y + 9, WHITE, a, Align::Center);
      s.rrect(W - 176, OB_Y, 136, OB_H, 26, BTN, a);
      s.text(fonts::heavy, tr(T_NO, lang_), W - 108, OB_Y + 9, INK, a, Align::Center);
    } else {
      textWrap(s, fonts::hand, tr(skipCost() == 1 ? T_NOSKIP1 : T_NOSKIP, lang_), W / 2, 232, 300, INK, a);
      s.rrect(W / 2 - 60, OB_Y, 120, OB_H, 26, INK, a);
      s.text(fonts::heavy, tr(T_OK, lang_), W / 2, OB_Y + 9, WHITE, a, Align::Center);
    }
  } else if (overlay_ == SOLVED) {
    const float u = ease(overlayT_ / 0.35f);
    s.circle(112, 150, 56 * u, YELLOW);
    const float sc[] = {60, 208, 140, 196, 220, 210, 300, 194};
    scribble(s, sc, 4, 9, CYAN, u);
    s.text(fonts::huge, praise_, W / 2, 112, INK, a, Align::Center);
    s.text(fonts::hand, tr(T_SOLVED, lang_), W / 2, 226, INK, a, Align::Center);
    if (diff_ == 2) {   // Schwer: Zeit und Bestzeit
      const int t = int(solvedAt_ * 10 + 0.5f);
      char b[48];
      std::snprintf(b, sizeof b, "%d:%02d.%d", t / 600, (t / 10) % 60, t % 10);
      s.text(fonts::heavy, b, W / 2, 262, INK, a, Align::Center);
      if (newBest_) s.text(fonts::label, tr(T_RECORD, lang_), W / 2, 296, HOTPINK, a, Align::Center);
      else {
        std::snprintf(b, sizeof b, "%s %d:%02d.%d", tr(T_BEST, lang_), bestT_ / 600, (bestT_ / 10) % 60, bestT_ % 10);
        s.text(fonts::label, b, W / 2, 296, G4, a, Align::Center);
      }
    } else {
      const float pw = Scene::textW(fonts::heavy, tr(T_PLUS, lang_)), px = W / 2 - (pw + 34) / 2;
      iconBulb(s, px + 12, 290, 0.9f, INK);
      s.text(fonts::heavy, tr(T_PLUS, lang_), px + 34, 272, INK, a);
    }
    static const float SP[4][2] = {{52, 70}, {322, 62}, {40, 286}, {330, 236}};
    for (int i = 0; i < 4; i++) sparkle(s, SP[i][0], SP[i][1], 9 + (i % 2) * 3, i % 2 ? INK : CYAN, a, now_ + i);
    s.rrect(W / 2 - 92, OB_Y + 14, 184, 52, 26, INK, a);
    s.text(fonts::heavy, tr(T_NEXT, lang_), W / 2, OB_Y + 23, WHITE, a, Align::Center);
  } else if (overlay_ == TASK) {
    drawTaskCard(s, a);
  }
}

void Game::drawTaskCard(Scene& s, float a) {
  const float u = ease(overlayT_ / 0.35f);
  char b[16];
  std::snprintf(b, sizeof b, "LVL.%d", cur_ + 1);
  const float tw = Scene::textW(fonts::huge, b);
  s.circle(W / 2 + tw / 2 + 30, 56, 22 * u, YELLOW, a);
  s.capsule(W / 2 - tw / 2, 108, W / 2 - tw / 2 + (tw + 6) * u, 104, 10, CYAN, a);
  s.text(fonts::huge, b, W / 2, 62, INK, a, Align::Center);
  const char* t = tr(levels_[cur_]->task(), lang_);
  const bool longT = std::strlen(t) > 34;
  const float th = textWrap(s, longT ? fonts::body : fonts::heavy, t, W / 2, 140, 300, INK, a);
  float y = 140 + th + 22;
  if (diff_ == 0) {   // Leicht: was könnte helfen?
    s.text(fonts::label, tr(T_MAYBE, lang_), W / 2, y, G4, a, Align::Center);
    y += 26;
    const int tools = levels_[cur_]->tools();
    const char* names[7];
    int n = 0;
    float total = 0;
    for (int i = 0; i < 7; i++)
      if (tools & (1 << i)) { names[n] = tr(T_TOOLS[i], lang_); total += Scene::textW(fonts::label, names[n]) + 28 + 8; n++; }
    float x = W / 2 - (total - 8) / 2;
    for (int i = 0; i < n; i++) {
      const float w = Scene::textW(fonts::label, names[i]) + 28;
      s.rrect(x, y, w, 30, 15, hex(0xE9FDFE), a);
      s.text(fonts::label, names[i], x + w / 2, y + 6, INK, a, Align::Center);
      x += w + 8;
    }
  } else if (diff_ == 2) {   // Schwer: Uhr und Bestzeit
    s.text(fonts::label, tr(T_CLOCK, lang_), W / 2, y, G4, a, Align::Center);
    if (bestT_) {
      char bb[40];
      std::snprintf(bb, sizeof bb, "%s %d:%02d.%d", tr(T_BEST, lang_), bestT_ / 600, (bestT_ / 10) % 60, bestT_ % 10);
      s.text(fonts::label, bb, W / 2, y + 24, HOTPINK, a, Align::Center);
    }
  }
  sparkle(s, 52, 92, 10, INK, a, now_);
  sparkle(s, 326, 150, 8, CYAN, a, now_ + 1);
  s.rrect(W / 2 - 92, OB_Y + 14, 184, 52, 26, INK, a);
  s.text(fonts::heavy, tr(T_GO, lang_), W / 2, OB_Y + 23, WHITE, a, Align::Center);
}

void Game::drawIntro(Scene& s) {
  const IntroPage& p = INTRO_PAGES[introPage_];
  s.text(fonts::label, tr(T_SKIPINTRO, lang_), W - 30, 30, G4, 1, Align::Right);
  const float t = now_;
  const float cx = W / 2, cy = 124;
  if (introPage_ == 0) {
    s.circle(cx, cy, 58, YELLOW);
    rays(s, cx, cy, 66, 78, 11, 3, INK, 1, t * 0.3f);
    iconBulb(s, cx, cy + 2, 2.2f, INK);
  } else if (introPage_ == 1) {
    const float sh = std::sin(t * 18) * 5;
    s.rrect(cx - 44 + sh, cy - 54, 88, 108, 20, INK);
    s.rrect(cx - 36 + sh, cy - 46, 72, 92, 14, WHITE);
    drawCandy(s, cx + sh, cy, 0.9f);
    for (int sd = -1; sd <= 1; sd += 2)
      for (int i = 0; i < 3; i++) s.capsule(cx + sd * (60 + i * 6), cy - 24 + i * 22, cx + sd * (74 + i * 6), cy - 30 + i * 22, 2.5f, CYAN);
  } else {
    s.rrect(cx - 92, cy - 30, 64, 64, 18, BTN);
    iconBulb(s, cx - 60, cy + 1, 1.2f, INK);
    s.rrect(cx + 28, cy - 30, 64, 64, 18, BTN);
    iconSkip(s, cx + 60, cy + 2, INK);
    sparkle(s, cx, cy - 50, 10, INK, 1, t);
  }
  s.text(fonts::heavy, tr(p.title, lang_), W / 2, 214, INK, 1, Align::Center);
  textWrap(s, fonts::body, tr(p.body, lang_), W / 2, 252, 310, INK);
  for (int i = 0; i < 3; i++) s.circle(W / 2 - 18 + i * 18, 366, i == introPage_ ? 5 : 3.5f, i == introPage_ ? INK : G3);
  s.rrect(W / 2 - 92, 382, 184, 52, 26, INK);
  s.text(fonts::heavy, tr(introPage_ == 2 ? T_GO : T_NEXTP, lang_), W / 2, 391, WHITE, 1, Align::Center);
}

void Game::draw(Scene& s) {
  s.reset();
  s.bg = WHITE;
  if (off_) { s.bg = {0, 0, 0}; return; }
  switch (screen_) {
    case TITLE: drawTitle(s); break;
    case INTRO: drawIntro(s); break;
    case PICK: drawLevels(s); break;
    case END: drawEnd(s); break;
    case PLAY: {
      Level* l = levels_[cur_];
      l->draw(s, frame_, lang_);
      if (!l->ownsTask()) textWrap(s, fonts::body, tr(l->task(), lang_), W / 2, TASK_Y, 300, INK);
      drawHeader(s, cur_);
      drawButtons(s);
      for (int i = 0; i < nRemarks_; i++) {
        const Remark& r = remarks_[i];
        const float u = (now_ - r.t0) / 1.6f;
        const float al = u < 0.1f ? u * 10 : u > 0.7f ? (1 - u) / 0.3f : 1;
        const char* str = tr(r.txt, lang_);
        const float tw = Scene::textW(fonts::hand, str);
        const float x = clampf(r.x, 24 + tw / 2, W - 24 - tw / 2);
        s.text(fonts::hand, str, x, r.y - u * 30, r.c, al, Align::Center);
      }
      break;
    }
  }
  if (overlay_ != NONE) drawOverlay(s);
}

void Game::voiceLines(void (*out)(const char* key, const char* text)) {
  char k[16];
  for (int lang = 0; lang < 2; lang++) {
    const char* L = lang ? "de" : "en";
    for (int i = 0; i < LEVELS; i++) {
      Level* l = makeLevel(i);
      std::snprintf(k, sizeof k, "%s_t%02d", L, i + 1);
      out(k, tr(l->task(), lang));
      delete l;
    }
    for (int i = 0; i < 5; i++) { std::snprintf(k, sizeof k, "%s_p%d", L, i); out(k, PRAISE[lang][i]); }
    for (int i = 0; i < 4; i++) { std::snprintf(k, sizeof k, "%s_n%d", L, i); out(k, tr(NOPES[i], lang)); }
  }
}

const char* Game::debug() {
  std::snprintf(dbg_, sizeof dbg_, "screen %d level %d overlay %d solved 0x%06x hints %d lang %s diff %d vol %d tilt %.2f %.2f %.2f",
                int(screen_), cur_ + 1, int(overlay_), unsigned(done_), hints_, lang_ ? "de" : "en", diff_, vol_, motion.tiltX, motion.tiltY, motion.tiltZ);
  return dbg_;
}

}  // namespace bc
