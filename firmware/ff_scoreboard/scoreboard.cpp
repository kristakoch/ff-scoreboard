#include "scoreboard.h"
#include "config.h"
#include "http_json.h"

#include <WiFi.h>
#include <ArduinoJson.h>

// Board order must match SportId (SPORT_NFL, SPORT_MLB, SPORT_NHL, SPORT_NBA).
SportBoard g_boards[MAX_SPORTS] = {
    {"NFL", {}, 0, ErrorState::OK},
    {"MLB", {}, 0, ErrorState::OK},
    {"NHL", {}, 0, ErrorState::OK},
    {"NBA", {}, 0, ErrorState::OK},
};

static const char* const kScoreboardUrls[MAX_SPORTS] = {
    SB_NFL_URL, SB_MLB_URL, SB_NHL_URL, SB_NBA_URL,
};

static ErrorState mapResult(const HttpFetch& r) {
  if (r.status == -1) return ErrorState::NO_WIFI;
  if (r.status == HTTP_CODE_OK && r.parsed) return ErrorState::OK;
  return ErrorState::DATA_ERROR;
}

// ESPN sends competitor.score as a string ("7"), but accept a number too.
static int32_t parseScore(JsonVariantConst v) {
  if (v.is<const char*>()) return (int32_t)atoi(v.as<const char*>());
  if (v.is<int32_t>())     return v.as<int32_t>();
  if (v.is<float>())       return (int32_t)v.as<float>();
  return 0;
}

ErrorState scoreboardFetch(int sportIndex) {
  if (sportIndex < 0 || sportIndex >= MAX_SPORTS) return ErrorState::DATA_ERROR;
  SportBoard& board = g_boards[sportIndex];

  // Keep exactly what a scoreboard needs. There is one competition per event;
  // the filter applies the [0] shape to every element of each array.
  DynamicJsonDocument filter(2048);
  filter["events"][0]["status"]["type"]["state"]        = true;
  filter["events"][0]["status"]["type"]["shortDetail"]  = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["homeAway"]        = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["score"]           = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["team"]["color"]        = true;

  DynamicJsonDocument doc(32768);
  HttpFetch r = httpFetchJson(kScoreboardUrls[sportIndex], filter, doc);
  ErrorState e = mapResult(r);
  if (e != ErrorState::OK) {
    board.err = e;      // keep last good games, just record the failure
    return e;
  }

  Game tmp[MAX_GAMES];
  int n = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    if (n >= MAX_GAMES) break;

    JsonObject comp = ev["competitions"][0];
    if (comp.isNull()) continue;  // unexpected shape for this event

    Game g;
    memset(&g, 0, sizeof(g));

    strlcpy(g.state,  ev["status"]["type"]["state"]       | "pre", sizeof(g.state));
    strlcpy(g.status, ev["status"]["type"]["shortDetail"] | "",    sizeof(g.status));

    for (JsonObject c : comp["competitors"].as<JsonArray>()) {
      const char* ha    = c["homeAway"] | "";
      const char* abbr  = c["team"]["abbreviation"] | "";
      const char* color = c["team"]["color"] | "000000";

      GameTeam& gt = (strcmp(ha, "home") == 0) ? g.home : g.away;
      strlcpy(gt.abbrev, abbr, sizeof(gt.abbrev));
      gt.color = strtoul(color, nullptr, 16);   // "002a5c" -> 0x002a5c
      gt.score = parseScore(c["score"]);
    }

    tmp[n++] = g;
  }

  memcpy(board.games, tmp, sizeof(Game) * n);
  board.count = n;
  board.err   = ErrorState::OK;
  return ErrorState::OK;  // n == 0 is a legitimate off-day
}

void scoreboardFetchAll() {
  for (int i = 0; i < MAX_SPORTS; i++) scoreboardFetch(i);
}
