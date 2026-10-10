// -----------------------------------------------------------------------------
// display.cpp - UI for the 64x32 HUB75 panel (MatrixPanel_I2S_DMA).
//
// One fixed view, no rotation: an old-fashioned LED-basketball scoreboard.
//   * Dad's team (TAK) LEFT, opponent RIGHT.
//   * Team names (white, Org_01, half the score size) on TOP of the scores.
//   * Each score is a fixed 3-cell seven-segment display: larger cells always
//     show their dim outline, the value is right-aligned inside them. Values
//     naturally reach the hundreds.
//   * One 1px red frame around the full board edge; no extra bezel lines.
//   * A dotted green->red win-probability bar under the scores: the green
//     share is ESPN's estimated chance that dad's team wins.
//
// All data is read from the plain structs in types.h (fantasy.cpp owns them);
// nothing here talks to the network.
// -----------------------------------------------------------------------------
#include "display.h"
#include "config.h"
#include "types.h"
#include "fantasy.h"

#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/Org_01.h>

static MatrixPanel_I2S_DMA* panel = nullptr;
static bool ready = false;
static unsigned long lastDraw = 0;

// ------------------------------- colors --------------------------------------
#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
static uint16_t cScoreOrange() { return panel->color565(0xFF, 0xA8, 0x00); }  // yellow-amber
static uint16_t cScoreDim()    { return panel->color565(0x2E, 0x1A, 0x00); }  // unlit segments
static uint16_t cFrameRed()    { return panel->color565(0xC8, 0x18, 0x18); }
static uint16_t cGreen()       { return panel->color565(0x00, 0xE0, 0x00); }
static uint16_t cWinRed()      { return panel->color565(0xF0, 0x20, 0x20); }
static uint16_t cName()        { return panel->color565(0xC8, 0xC8, 0xC8); }  // dimmed white, less flicker
static uint16_t cCyan()        { return panel->color565(0x23, 0xD8, 0xD8); }
static uint16_t cBootRed()     { return panel->color565(0xE0, 0x30, 0x30); }

// ------------------------------ board framing ---------------------------------
// A single 1px red frame right on the panel edge. Nothing else (no bezel, no
// inner rectangle): one colored row per side, no blank rows top/bottom.
static void drawFrame() {
  uint16_t red = cFrameRed();
  panel->drawFastHLine(0, 0, 64, red);
  panel->drawFastHLine(0, 31, 64, red);
  panel->drawFastVLine(0, 0, 32, red);
  panel->drawFastVLine(63, 0, 32, red);
}

// --------------------------- seven-segment digits -----------------------------
// Each score owns 3 fixed cells (scores reach the hundreds). Unused cells show
// only their dim outline, like a real arena board. Cell 9 x 17, 1px bars, 1px
// blank column between cells, standard symmetric a-g layout.
#define SEG_W 9
#define SEG_H 17
#define SEG_T 1
#define SEG_GAP 1
#define SEG_BARW (SEG_W - 2 * SEG_T)  // 7
#define SEG_MID  (SEG_H / 2)          // 8
#define SEG_UP   (SEG_MID - SEG_T)    // 7 -> upper/lower vertical height
#define SEG_LOW  (SEG_H - SEG_T - SEG_MID - SEG_T)  // 7

static const uint8_t SEVEN_SEG[10] = {
  0x3F,  // 0 -> a b c d e f
  0x06,  // 1 -> b c
  0x5B,  // 2 -> a b g e d
  0x4F,  // 3 -> a b g c d
  0x66,  // 4 -> f g b c
  0x6D,  // 5 -> a f g c d
  0x7D,  // 6 -> a f g e d c
  0x07,  // 7 -> a b c
  0x7F,  // 8 -> all
  0x6F,  // 9 -> a b c d f g
};
#define SEG_MINUS 0x40  // center bar only (negative live scores exist)

static void drawSegment(int x, int y, int w, int h, bool lit) {
  panel->fillRect(x, y, w, h, lit ? cScoreOrange() : cScoreDim());
}

// Draw one 7-seg cell at (x,y). mask == 0 renders the pure dim outline.
static void drawCell(int x, int y, uint8_t mask) {
  drawSegment(x + SEG_T, y,             SEG_BARW, SEG_T,     mask & 0x01);  // a top
  drawSegment(x,          y + SEG_T,    SEG_T,    SEG_UP,    mask & 0x20);  // f upper-left
  drawSegment(x + SEG_W - SEG_T, y + SEG_T, SEG_T, SEG_UP,   mask & 0x02);  // b upper-right
  drawSegment(x + SEG_T, y + SEG_MID,   SEG_BARW, SEG_T,     mask & 0x40);  // g middle
  drawSegment(x,          y + SEG_MID + SEG_T, SEG_T, SEG_LOW, mask & 0x10); // e lower-left
  drawSegment(x + SEG_W - SEG_T, y + SEG_MID + SEG_T, SEG_T, SEG_LOW, mask & 0x04); // c
  drawSegment(x + SEG_T, y + SEG_H - SEG_T, SEG_BARW, SEG_T, mask & 0x08);  // d bottom
}

// Fixed 3-cell score centered on xCenter, value right-aligned, top yTop.
static void drawScore(int xCenter, int yTop, long value) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%ld", value);
  int n = strlen(buf);
  int total = 3 * SEG_W + 2 * SEG_GAP;
  int x0 = xCenter - total / 2;
  for (int i = 0; i < 3; i++) {
    int idx = n - 3 + i;            // right-aligned digit for this cell
    uint8_t mask = 0;
    if (idx >= 0 && idx < n) {
      char c = buf[idx];
      mask = (c == '-') ? SEG_MINUS : SEVEN_SEG[c - '0'];
    }
    drawCell(x0 + i * (SEG_W + SEG_GAP), yTop, mask);
  }
}

// ------------------------------- text helpers ---------------------------------
// Org_01 letters all advance 6px; `extra` adds per-letter spacing (names).
static void drawCentered(int xCenter, int yBase, uint16_t color, const char* s, int extra = 0) {
  panel->setFont(&Org_01);
  panel->setTextColor(color);
  int n = strlen(s);
  int total = 6 * n + extra * (n - 1);
  int x = xCenter - total / 2;
  for (int i = 0; i < n; i++) {
    panel->setCursor(x, yBase);
    panel->print(s[i]);
    x += 6 + extra;
  }
}

// ------------------------------- win-probability -------------------------------
// 30 dots (every 2px). The left share is green = dad's estimated win %, the
// rest red.
static void drawWinBar(float dadWinPct) {
  static const int DOTS = 30;
  int x = 2;
  int green = (int)(dadWinPct * DOTS + 0.5f);
  if (green < 0) green = 0;
  if (green > DOTS) green = DOTS;
  for (int i = 0; i < DOTS; i++, x += 2) {
    panel->drawPixel(x, 29, i < green ? cGreen() : cWinRed());
  }
}

// ------------------------------ boot card -------------------------------------
static void drawBoot(bool blink) {
  drawCentered(32, 8, C_WHITE, "SCOREBOARD");
  if (WiFi.status() == WL_CONNECTED) {
    if (blink) drawCentered(32, 19, cCyan(), "CONNECTING...");
  } else {
    if (blink) drawCentered(32, 19, cBootRed(), "NO WIFI");
  }
}

// ---------------------------- fantasy scoreboard -------------------------------
// Layout (inside the 1px red frame at x=0/x=63, y=0/y=31):
//   rows  2..7   team names        (Org_01, baseline 6)
//   rows 10..26  three-cell scores (centers x=16 / x=47)
//   row  29      green->red win bar
static void drawScoreboard() {
  const Matchup* m = fantasyMatchupForTeam(TEAM_ID);
  if (!m) {
    drawCentered(32, 8, C_WHITE, "TAK BYE");
    return;
  }

  bool dadHome = (m->homeId == TEAM_ID);
  const Team* opp = fantasyTeamById(dadHome ? m->awayId : m->homeId);
  const char* oppName = opp ? opp->abbrev : "?";

  drawCentered(16, 6, cName(), "TAK", 1);
  drawCentered(47, 6, cName(), oppName, 1);

  drawScore(16, 10, (long)(dadHome ? m->homeScore : m->awayScore));  // dad, LEFT
  drawScore(47, 10, (long)(dadHome ? m->awayScore : m->homeScore));  // opponent, RIGHT

  if (m->homeWinPct + m->awayWinPct > 0.0f) {
    float dadWp = dadHome ? m->homeWinPct : m->awayWinPct;
    if (dadWp < 0.0f) dadWp = 0.0f;
    if (dadWp > 1.0f) dadWp = 1.0f;
    drawWinBar(dadWp);
  }
}

// ------------------------------- lifecycle ------------------------------------
bool displayReady() { return ready; }

void displayInit() {
  // Explicitly mirror the working library example config (64x32, 1 panel).
  HUB75_I2S_CFG cfg(64, 32, 1);
  panel = new MatrixPanel_I2S_DMA(cfg);
  if (!panel) return;

  bool ok = panel->begin();
  Serial.printf("  panel begin()=%s\n", ok ? "true" : "FALSE");
  if (!ok) { Serial.println("  PANEL INIT FAILED - 5V/pins/EN? see library logs above"); }

  panel->setBrightness8(DISPLAY_BRIGHTNESS);
  panel->setTextWrap(false);
  panel->clearScreen();

  panel->fillScreen(panel->color565(255, 255, 255));
  delay(1000);
  panel->fillScreen(panel->color565(255, 96, 0));
  delay(300);
  panel->fillScreen(C_BLACK);
  delay(150);

  ready = true;
  lastDraw = millis();
}

void displayRender() {
  if (!panel) return;
  unsigned long now = millis();
  if (now - lastDraw < DISPLAY_BLINK_MS) return;
  lastDraw = now;
  bool blink = (now / DISPLAY_BLINK_MS) & 1;

  panel->fillScreen(C_BLACK);
  drawFrame();

  if (g_matchupCount > 0) drawScoreboard();
  else drawBoot(blink);
}