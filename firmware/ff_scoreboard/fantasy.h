#pragma once
// -----------------------------------------------------------------------------
// fantasy.h - ESPN fantasy football data layer.
// -----------------------------------------------------------------------------
#include "types.h"

// Cached league data (owned by fantasy.cpp). Read these from the display layer.
extern Team    g_teams[MAX_TEAMS];
extern int     g_teamCount;
extern Matchup g_matchups[MAX_MATCHUPS];
extern int     g_matchupCount;
extern int     g_currentMatchupPeriod;

// Incremented on every successful fetch that (re)writes the cached data, so the
// display layer can repaint only when the shown content has actually changed.
extern int     g_dataGen;

// Fetch the team list once at boot and cache it. The season's teams are static.
// Also caches g_currentMatchupPeriod from status.currentMatchupPeriod.
ErrorState fantasyFetchTeams();

// Fetch only the current matchup period. Poll this on an interval. On failure
// the previously cached teams/matchups are left untouched.
ErrorState fantasyFetchMatchups();

// Convenience accessors for the display layer.
const Matchup* fantasyMatchupForTeam(int32_t teamId);  // matchup involving TEAM_ID
const Team*    fantasyTeamById(int32_t teamId);
