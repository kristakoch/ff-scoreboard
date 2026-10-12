#pragma once
// -----------------------------------------------------------------------------
// types.h - plain data structs + error enum shared by the whole data layer.
//
// The display layer should only ever read these structs; no display code lives
// here or in the other files of this sketch.
// -----------------------------------------------------------------------------
#include <Arduino.h>

// Fixed caps keep everything statically sized (no heap churn on refresh).
#define MAX_TEAMS     20     // league currently has 14
#define MAX_MATCHUPS  16     // 14 teams -> 7 matchups per period

#define MAX_ABBREV    8
#define MAX_NAME      48

// Error states returned by every fetch helper. On a non-OK result the previous
// good data is intentionally left in place, so callers can keep rendering.
//
// NOTE: this is a scoped enum (enum class) on purpose. The ESP32 ROM headers
// define a global `STATUS OK` (ets_sys.h), so a plain `OK` enumerator would
// collide. Use ErrorState::OK etc. from here on.
enum class ErrorState : uint8_t {
  OK = 0,           // HTTP 200 + JSON parsed + expected shape
  NO_WIFI,          // Wi-Fi not connected / request never left the device
  LEAGUE_PRIVATE,   // fantasy endpoint returned 401 or 403
  DATA_ERROR        // parse failure, unexpected shape, or other HTTP error
};

// ------------------------------- Fantasy -------------------------------------
struct Team {
  int32_t id;
  char    abbrev[MAX_ABBREV];
  char    name[MAX_NAME];
};

struct Matchup {
  int32_t homeId;
  int32_t awayId;
  float   homeScore;
  float   awayScore;
  float   homeWinPct;   // ESPN's estimated win probability (0..1) per side
  float   awayWinPct;
  bool    live;    // true while games are in progress (totalPointsLive is set)
};
