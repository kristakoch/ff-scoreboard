// -----------------------------------------------------------------------------
// display.cpp - UI for the 64x32 HUB75 panel (MatrixPanel_I2S_DMA).
//
// One fixed view, no rotation: an old-fashioned LED-basketball scoreboard.
//   * Whole board is a solid dark-green "felt" background (no border).
//   * Dad's team (TAK) LEFT, opponent RIGHT; white names (Org_01) on TOP.
//   * The numbers sit on BLACK LED panels - red three-cell seven-segment
//     displays, right-aligned, only the lit bars draw (no dim outlines).
//   * A dotted GOLD->purple win-probability bar under the scores: the gold
//     share is ESPN's estimated chance that dad's team wins.
//   * While the first data poll is pending, a big football tumbles instead.
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
#include <math.h>

static MatrixPanel_I2S_DMA* panel = nullptr;
static bool ready = false;

// ------------------------------- colors --------------------------------------
#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
static uint16_t cScoreRed()     { return panel->color565(0xE6, 0x16, 0x16); }  // ~10% dimmed red, less ghost
static uint16_t cBoard()        { return panel->color565(0x03, 0x1E, 0x07); }  // very dark green felt
static uint16_t cGold()         { return panel->color565(0xFF, 0xC8, 0x00); }  // gold win dots
static uint16_t cLoss()         { return panel->color565(0x7A, 0x36, 0xF2); }  // purple/blue loss dots
static uint16_t cName()        { return panel->color565(0xB4, 0xB4, 0xB4); }  // ~10% dimmed white, less flicker
static uint16_t cBall()        { return panel->color565(0x96, 0x32, 0x12); }  // leather brown

// --------------------------- seven-segment digits -----------------------------
// Each score owns 3 fixed cells (scores reach the hundreds). Unused cells are
// simply empty. Thin 1px bars (SEG_T), 8px cells, 2 blank columns between
// digits so the numbers read as separate.
#define SEG_W 8
#define SEG_H 17
#define SEG_T 1
#define SEG_GAP 2
#define SEG_BARW (SEG_W - 2 * SEG_T)  // 6
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
  if (!lit) return;  // unlit segments are not drawn: clean bare digits
  panel->fillRect(x, y, w, h, cScoreRed());
}

// Draw one 7-seg cell at (x,y). mask == 0 draws nothing (empty spare cell).
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
// Dotted bar on row 30 (one green row below it). Spans x=3..62 - the exact outer
// edges of the number boxes (both shifted one LED right).
static void drawWinBar(float dadWinPct) {
  static const int DOTS = 60;    // every column 3..62
  static const int X0  = 3;
  int green = (int)(dadWinPct * DOTS + 0.5f);
  if (green < 0) green = 0;
  if (green > DOTS) green = DOTS;
  for (int i = 0; i < DOTS; i++) {
    panel->drawPixel(X0 + i, 30, i < green ? cGold() : cLoss());
  }
}

// ------------------------------ loading screen -------------------------------
// While no matchup data is loaded yet a big leather-brown football "spirals" on
// the board: the outline stays put (horizontal super-ellipse, exponent <2 so the
// tips taper into points) and the white lace ladder rotates around the long axis
// like a thrown spiral. It rides on the near surface for exactly half a turn
// (sweeping top -> bottom), then hides on the far side for the other half, then
// re-appears at the top.
static void drawFootball(unsigned long frame) {
  static const int CX = 32, CY = 16;     // board centre (rows 1..31 fit the ball)
  static const float MAJ = 11.0f, MIN = 6.0f;  // long / short radius
  const float N = 1.7f;                   // super-ellipse exponent (<2 = pointed tips)
  const int fr = (int)(frame & 7);        // 8 frames; one revolution = 2 s at 4 fps
  const float ang = fr * (PI / 4.0f);     // rotation about the long axis
  const bool front = (fr & 4) == 0;       // near (visible) half of the spin
  const float lc = -4.0f * cosf(ang);     // lace band centre: top -> bottom on the front
  const uint16_t B = cBall();
  const uint16_t W = C_WHITE;
  for (int py = CY - 24; py <= CY + 24; py++) {
    for (int px = CX - 24; px <= CX + 24; px++) {
      float dx = (float)(px - CX), dy = (float)(py - CY);
      float v = powf(fabsf(dx / MAJ), N) + powf(fabsf(dy / MIN), N);
      if (v > 1.0f) continue;             // outside the ball
      bool lace = false;
      if (front) {
        bool spine   = fabsf(dy - lc) <= 0.5f && fabsf(dx) <= 6.0f;
        bool stitch  = fabsf(dy - lc) <= 2.0f && fabsf(dx) <= 4.0f && (((int)lrintf(dx) & 1) == 0);
        lace = spine || stitch;
      }
      panel->drawPixel(px, py, lace ? W : B);
    }
  }
}

// ---------------------------- fantasy scoreboard -------------------------------
// Layout (on the dark-green board, no border):
//   rows  2..7   team names       (Org_01, baseline 6, on the green felt)
//   rows  9..27  black LED panel  (1px pad) with the 3-cell RED scores
//   row  29      green->purple win bar
static void drawScoreboard() {
  const Matchup* m = fantasyMatchupForTeam(TEAM_ID);
  if (!m) {
    drawCentered(32, 8, C_WHITE, "TAK BYE");
    return;
  }

  bool dadHome = (m->homeId == TEAM_ID);
  const Team* opp = fantasyTeamById(dadHome ? m->awayId : m->homeId);
  const char* oppName = opp ? opp->abbrev : "?";

  // Black panels (29 wide) shifted one LED right: left x=3..31, right x=34..62.
  panel->fillRect(3, 9, 29, 19, C_BLACK);    // dad's score, LEFT
  panel->fillRect(34, 9, 29, 19, C_BLACK);   // opponent's score, RIGHT

  drawCentered(17, 6, cName(), "TAK", 1);
  drawCentered(48, 6, cName(), oppName, 1);

  drawScore(17, 10, (long)(dadHome ? m->homeScore : m->awayScore));  // dad, LEFT
  drawScore(48, 10, (long)(dadHome ? m->awayScore : m->homeScore));  // opponent, RIGHT

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
  cfg.double_buff = true;               // draw to a hidden buffer, swap atomically -
  cfg.latch_blanking = 4;               // more LED blanking at each row latch kills
  panel = new MatrixPanel_I2S_DMA(cfg); // "ghosting" - fast faint dots near digits
  if (!panel) return;

  bool ok = panel->begin();
  Serial.printf("  panel begin()=%s\n", ok ? "true" : "FALSE");
  if (!ok) { Serial.println("  PANEL INIT FAILED - 5V/pins/EN? see library logs above"); }

  panel->setBrightness8(DISPLAY_BRIGHTNESS);
  panel->setTextWrap(false);
  panel->clearScreen();

  // Draw + flip each splash color so it is actually shown (double buffered).
  panel->fillScreen(panel->color565(255, 255, 255));
  panel->flipDMABuffer();
  delay(1000);
  panel->fillScreen(panel->color565(255, 96, 0));
  panel->flipDMABuffer();
  delay(300);
  panel->fillScreen(C_BLACK);
  panel->flipDMABuffer();
  delay(150);

  ready = true;
}

static int s_lastDataGen = -1;         // g_dataGen at the last scoreboard repaint
static const unsigned long LOAD_FRAME_MS = 125;    // football spiral: 8 frames/rev = 1 s
static unsigned long s_lastLoadFrame = 0;

void displayRender() {
  if (!panel) return;

  unsigned long now = millis();

  // Scoreboard mode: the frame is static, so only repaint when the fetched
  // data actually changes. No repaint otherwise -> no flip -> no flicker at all.
  if (g_matchupCount > 0) {
    if (g_dataGen == s_lastDataGen) return;
    s_lastDataGen = g_dataGen;

    panel->fillScreen(cBoard());        // dark green felt
    drawScoreboard();
    panel->flipDMABuffer();             // present the finished frame atomically
    return;
  }

  // No matchup data yet: spin the football loader over the plain board until
  // the first poll lands (Wi-Fi joining, teams/matchups being fetched, or a
  // failed initial poll waiting to retry).
  unsigned long fr = now / LOAD_FRAME_MS;
  if (fr == s_lastLoadFrame) return;      // only flip when the ball advances
  s_lastLoadFrame = fr;
  panel->fillScreen(cBoard());            // dark green felt
  drawFootball(fr);
  panel->flipDMABuffer();                 // present the finished frame atomically
}