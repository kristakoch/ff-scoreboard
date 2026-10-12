// -----------------------------------------------------------------------------
// ff_scoreboard.ino - ESP32 64x32 HUB75 scoreboard.
//
// Parts:
//   1. ESPN fantasy football: team list cached at boot, current-period matchups
//      polled every POLL_INTERVAL_MS.
//   2. Display: a single fixed fantasy view (display.cpp) - dad's team left,
//      opponent right, score with a dotted win-probability bar underneath.
//
// Data is printed to Serial each poll. The display only repaints when the
// fetched data actually changes (plus a boot blink while it joins).
//
// Requires: ArduinoJson (v6.20+ or v7), ESP32-HUB75-MatrixPanel-I2S-DMA and
// the ESP32 board package.
// Verified compiling with esp32:esp32:esp32 (core 3.3.12) against ArduinoJson
// 6.21.5, 7.4.2 and 7.4.3.
//
// ASSUMPTIONS ABOUT THE ESPN JSON (all confirmed against the live 2026 data):
//   * Fantasy team fields are teams[].id / .abbrev / .name (no location/nickname
//     fields are present; each team already carries a full display name).
//   * Current period is status.currentMatchupPeriod.
//   * Each schedule row has home/away objects with teamId + totalPoints; the
//     current period's live/scheduled games can legitimately be 0.00.
//   * view=mMatchupScore is ~273 KB vs ~2.5 MB for view=mMatchup, and unlike
//     mMatchup it does NOT include teams[], so teams are fetched once via mTeam.
// -----------------------------------------------------------------------------
#include <WiFi.h>

#include "config.h"
#include "types.h"
#include "fantasy.h"
#include "display.h"

// ------------------------------- helpers -------------------------------------
static const char* errorStateName(ErrorState e) {
  switch (e) {
    case ErrorState::OK:             return "OK";
    case ErrorState::NO_WIFI:        return "NO_WIFI";
    case ErrorState::LEAGUE_PRIVATE: return "LEAGUE_PRIVATE";
    case ErrorState::DATA_ERROR:     return "DATA_ERROR";
    default:             return "UNKNOWN";
  }
}

static void printHeap(const char* when) {
  Serial.printf("    heap %-22s %u bytes\n", when, ESP.getFreeHeap());
}

// ----------------------------- printers --------------------------------------
static void printTeams() {
  Serial.printf("  Teams cached (%d):\n", g_teamCount);
  for (int i = 0; i < g_teamCount; i++) {
    const Team& t = g_teams[i];
    Serial.printf("    %2ld  %-5s %s\n", (long)t.id, t.abbrev, t.name);
  }
}

static void printFantasy() {
  Serial.printf("  Fantasy matchups, period %d (%d games):\n",
                g_currentMatchupPeriod, g_matchupCount);
  for (int i = 0; i < g_matchupCount; i++) {
    const Matchup& m = g_matchups[i];
    const Team* ht = fantasyTeamById(m.homeId);
    const Team* at = fantasyTeamById(m.awayId);
    Serial.printf("    %-5s %6.2f  vs  %6.2f %-5s%s\n",
                  ht ? ht->abbrev : "?",
                  m.homeScore,
                  m.awayScore,
                  at ? at->abbrev : "?",
                  (m.homeId == TEAM_ID || m.awayId == TEAM_ID) ? "   <-- TAK" : "");
  }

  const Matchup* mine = fantasyMatchupForTeam(TEAM_ID);
  if (mine) {
    const Team* opp = fantasyTeamById(mine->homeId == TEAM_ID ? mine->awayId : mine->homeId);
    Serial.printf("  TAK is %s vs %s\n",
                  mine->homeId == TEAM_ID ? "HOME" : "AWAY",
                  opp ? opp->abbrev : "?");
  } else {
    Serial.println("  TAK has no matchup this period");
  }
}

// ---------------------------- fetch cycle -------------------------------------
// Fantasy matchups only (fast poll). The static team list is cached once when
// the board first connects. Each fetch is a blocking network read (~1s for the
// 273 KB payload), so this all runs on core 0 in fetchTask() - loop() / the
// display never wait on it.
static void fetchAndPrint() {
  Serial.println("\n[fantasy] refresh");
  printHeap("before matchup fetch");
  ErrorState fe = fantasyFetchMatchups();
  printHeap("after matchup fetch");
  Serial.printf("  result: %s\n", errorStateName(fe));
  printFantasy();
}

// Core 0 worker: Wi-Fi join + team cache + matchups poll/retry, forever. Writes
// only to the g_* globals the display reads, so the loader spins uninterrupted.
void fetchTask(void* arg) {
  static unsigned long lastWifiTry = 0;
  static unsigned long lastPoll = 0;
  for (;;) {
    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWifiTry >= WIFI_RETRY_MS) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        lastWifiTry = now;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Joined: cache the static team list once.
    if (!g_teamCount) {
      Serial.println("\n[fantasy] initial team fetch (once)");
      printHeap("before team fetch");
      ErrorState te = fantasyFetchTeams();
      printHeap("after team fetch");
      Serial.printf("  result: %s\n", errorStateName(te));
      if (te == ErrorState::OK) printTeams();
    }

    // Matchups: normal 60 s poll, but while we still have no data (first boot,
    // or a failed poll) retry every POLL_INITIAL_RETRY_MS so the scoreboard
    // appears promptly after joining.
    if (now - lastPoll >= POLL_INTERVAL_MS ||
        (!g_matchupCount && now - lastPoll >= POLL_INITIAL_RETRY_MS)) {
      lastPoll = now;
      fetchAndPrint();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ------------------------------- Arduino -------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000) delay(10);

  displayInit();

  Serial.println();
  Serial.println("ESPN scoreboard data layer");
  Serial.printf("League %ld | team %d | season %d\n",
                (long)LEAGUE_ID, TEAM_ID, SEASON);
  Serial.printf("Free heap at boot: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Display panel: %s\n", displayReady() ? "ready" : "unavailable");

  // Run all the network work on core 0 so loop() (core 1) never blocks - the
  // football loader keeps spinning through Wi-Fi joins and long JSON fetches.
  xTaskCreatePinnedToCore(fetchTask, "ff_fetch", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  displayRender();   // never blocks: fetching + WiFi live on the other core
  delay(10);
}
