#pragma once
// -----------------------------------------------------------------------------
// scoreboard.h - real-sport ESPN scoreboards (NFL/MLB/NHL/NBA), no login.
// -----------------------------------------------------------------------------
#include "types.h"

// Fixed-size boards, indexed by SportId. Owned by scoreboard.cpp.
extern SportBoard g_boards[MAX_SPORTS];

enum SportId : uint8_t { SPORT_NFL = 0, SPORT_MLB, SPORT_NHL, SPORT_NBA };

// Fetch one sport. On failure the previous good games are kept.
ErrorState scoreboardFetch(int sportIndex);

// Fetch all sports in sequence.
void scoreboardFetchAll();
