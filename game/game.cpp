#include "game.h"

#include <cstdio>
#include <cstring>

namespace bc {

namespace {
// Knöpfe der Oberfläche
enum Btn { B_BACK, B_SKIP, B_RESTART, B_HINT, B_PLAY = 10, B_GRID, B_LANG, B_SOUND, B_YES = 30, B_NO, B_TILE = 40 };

constexpr uint32_t DIM_MS = 90000, OFF_MS = 240000;
constexpr int BRIGHT = 170, BRIGHT_DIM = 40;

// Kacheln der Levelauswahl
constexpr float TW = 92, TH = 62, TGX = 14, TGY = 12, TX0 = (W - 3 * TW - 2 * TGX) / 2, TY0 = 86;

const Txt T_RESTART{"Restart", "Neustart"}, T_HINTS{"Hints", "Tipps"};
const Txt T_PLAY{"PLAY", "SPIELEN"}, T_SUB{"logic puzzles", "Logikrätsel"}, T_LEVELS{"LEVELS", "LEVEL"};
const Txt T_HINT{"HINT", "TIPP"}, T_NOHINT{"No hints left. Every solved level gives you one.", "Keine Tipps mehr. Für jedes gelöste Level gibt es einen."};
const Txt T_SKIPQ{"Skip this level?", "Level überspringen?"}, T_COST{"costs 3 hints", "kostet 3 Tipps"}, T_NOSKIP{"You need 3 hints to skip.", "Zum Überspringen brauchst du 3 Tipps."};
const Txt T_YES{"YES", "JA"}, T_NO{"NO", "NEIN"}, T_OK{"OK", "OK"};
const Txt T_SOLVED{"Level solved!", "Level geschafft!"}, T_PLUS{"+1 hint", "+1 Tipp"}, T_NEXT{"NEXT", "WEITER"};
const Txt T_GENIUS{"GENIUS!", "GENIAL!"}, T_ALL{"All 12 puzzles solved.", "Alle 12 Rätsel gelöst."}, T_BACKTAP{"Tap to go back", "Tippen zum Start"};
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
  sound_ = store_.get("bc_snd", 1) != 0;
  cur_ = store_.get("bc_cur", 0) % LEVELS;
  setSound(sound_);
  motion.threshold = 0.12f;
}

Game::~Game() { for (auto* l : levels_) delete l; }

void Game::save() {
  store_.set("bc_done", int(done_));
  store_.set("bc_used", int(hintUsed_));
  store_.set("bc_hints", hints_);
  store_.set("bc_cur", cur_);
}

void Game::setSound(bool on) {
  sound_ = on;
  synth_.setVolume(on ? 0.55f : 0);
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
  store_.set("bc_cur", cur_);
}

// ---------------- Host ----------------
void Game::solve() {
  Level* l = levels_[cur_];
  if (l->solved) return;
  l->solved = true;
  solvedAt_ = levelT_;
  praise_ = PRAISE[lang_][praiseN_++ % 5];
  if (!isDone(cur_)) { done_ |= 1u << cur_; hints_++; save(); }
}

void Game::nope(float x, float y) {
  say(x, y, NOPES[(int(now_ * 7)) % 4], INK);
  sfx(SFX_NOPE);
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
static int btnAtPlay(float x, float y) {
  if (y < 72 && x < 76) return B_BACK;
  if (y < 72 && x > W - 76) return B_SKIP;
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
    else { pressedBtn_ = btnAtPlay(px, py); uiTouch_ = pressedBtn_ >= 0; }
  }
  if (released && uiTouch_) {
    if (isTap) tap(x_, y_);
    uiTouch_ = false;
    pressedBtn_ = -1;
  }

  if (screen_ == PLAY) {
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
  }
  frame_ = f;
  frame_.ms = in.ms;
  // alte Zurufe entfernen
  int k = 0;
  for (int i = 0; i < nRemarks_; i++) if (now_ - remarks_[i].t0 < 1.6f) remarks_[k++] = remarks_[i];
  nRemarks_ = k;
}

void Game::tap(float x, float y) {
  if (overlay_ == HINT) { overlay_ = NONE; sfx(SFX_CLICK); return; }
  if (overlay_ == SKIP) {
    const bool can = hints_ >= 3;
    if (can && inRect(x, y, 40, OB_Y, 136, OB_H)) {   // Ja
      hints_ -= 3;
      done_ |= 1u << cur_;
      save();
      sfx(SFX_WHOOSH);
      if (cur_ + 1 < LEVELS) startLevel(cur_ + 1);
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
    else startLevel(cur_ + 1 < LEVELS && !isDone(cur_ + 1) ? cur_ + 1 : n);
    return;
  }
  switch (screen_) {
    case TITLE:
      if (inRect(x, y, 30, 350, 220, 64)) { sfx(SFX_POP); startLevel(nextLevel()); }
      else if (inRect(x, y, 262, 350, 80, 64)) { sfx(SFX_CLICK); screen_ = PICK; }
      else if (inRect(x, y, 280, 8, 80, 56)) { lang_ ^= 1; store_.set("bc_lang", lang_); sfx(SFX_CLICK); }
      else if (inRect(x, y, 214, 8, 62, 56)) { setSound(!sound_); store_.set("bc_snd", sound_); sfx(SFX_CLICK); }
      break;
    case PICK:
      if (y < 72 && x < 80) { screen_ = TITLE; sfx(SFX_CLICK); break; }
      for (int i = 0; i < LEVELS; i++) {
        const float tx = TX0 + (i % 3) * (TW + TGX), ty = TY0 + (i / 3) * (TH + TGY);
        if (inRect(x, y, tx - 4, ty - 4, TW + 8, TH + 8)) {
          if (unlocked(i)) { sfx(SFX_POP); startLevel(i); }
          else sfx(SFX_NOPE);
        }
      }
      break;
    case END:
      screen_ = TITLE;
      break;
    case PLAY:
      switch (btnAtPlay(x, y)) {
        case B_BACK: screen_ = PICK; sfx(SFX_CLICK); break;
        case B_SKIP: overlay_ = SKIP; overlayT_ = 0; sfx(SFX_CLICK); break;
        case B_RESTART: startLevel(cur_); sfx(SFX_CLICK); break;
        case B_HINT:
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
    iconSkip(s, W - 36, HEAD_Y, INK);
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
  sparkle(s, 52, 236, 12, INK, 1, 0.2f);
  sparkle(s, 96, 300, 8, INK, 1, 0.6f);
  sparkle(s, 344, 250, 9, INK, 1, 0.1f);
  // oben rechts: Ton und Sprache
  iconSpeaker(s, 240, 34, sound_, INK);
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
  for (int i = 0; i < LEVELS; i++) {
    const float x = TX0 + (i % 3) * (TW + TGX), y = TY0 + (i / 3) * (TH + TGY);
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
    if (hints_ >= 3) {
      s.text(fonts::hand, tr(T_COST, lang_), W / 2, 232, INK, a, Align::Center);
      s.rrect(40, OB_Y, 136, OB_H, 26, INK, a);
      s.text(fonts::heavy, tr(T_YES, lang_), 108, OB_Y + 9, WHITE, a, Align::Center);
      s.rrect(W - 176, OB_Y, 136, OB_H, 26, BTN, a);
      s.text(fonts::heavy, tr(T_NO, lang_), W - 108, OB_Y + 9, INK, a, Align::Center);
    } else {
      textWrap(s, fonts::hand, tr(T_NOSKIP, lang_), W / 2, 232, 300, INK, a);
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
    const float pw = Scene::textW(fonts::heavy, tr(T_PLUS, lang_)), px = W / 2 - (pw + 34) / 2;
    iconBulb(s, px + 12, 290, 0.9f, INK);
    s.text(fonts::heavy, tr(T_PLUS, lang_), px + 34, 272, INK, a);
    static const float SP[4][2] = {{52, 70}, {322, 62}, {40, 286}, {330, 236}};
    for (int i = 0; i < 4; i++) sparkle(s, SP[i][0], SP[i][1], 9 + (i % 2) * 3, i % 2 ? INK : CYAN, a, now_ + i);
    s.rrect(W / 2 - 92, OB_Y + 14, 184, 52, 26, INK, a);
    s.text(fonts::heavy, tr(T_NEXT, lang_), W / 2, OB_Y + 23, WHITE, a, Align::Center);
  }
}

void Game::draw(Scene& s) {
  s.reset();
  s.bg = WHITE;
  if (off_) { s.bg = {0, 0, 0}; return; }
  switch (screen_) {
    case TITLE: drawTitle(s); break;
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

const char* Game::debug() {
  std::snprintf(dbg_, sizeof dbg_, "screen %d level %d overlay %d solved 0x%03x hints %d lang %s tilt %.2f %.2f %.2f",
                int(screen_), cur_ + 1, int(overlay_), unsigned(done_), hints_, lang_ ? "de" : "en", motion.tiltX, motion.tiltY, motion.tiltZ);
  return dbg_;
}

}  // namespace bc
