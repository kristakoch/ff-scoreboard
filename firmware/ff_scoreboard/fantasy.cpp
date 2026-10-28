#include "fantasy.h"
#include "config.h"
#include "http_json.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Season year
// -----------------------------------------------------------------------------
// ESPN's season parameter is the calendar year the NFL season starts in. Derive
// it from the clock instead of making the user edit config.h every year.
// January/February still belong to the previous season (playoffs/Super Bowl).
//
// ff_scoreboard.ino syncs the clock via NTP after Wi-Fi joins. If NTP is
// unavailable, fall back to the year this firmware was compiled so the board
// can still start.
static int seasonForDate(int year, int month) {
  return (month <= 2) ? year - 1 : year;
}

static int buildSeasonYear() {
  static int cached = 0;
  if (cached) return cached;

  static const char* months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  char mon[4] = {0};
  int day = 1;
  int year = 2026;  // only used if __DATE__ cannot be parsed
  int month = 0;
  if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) == 3) {
    for (int i = 0; i < 12; i++) {
      if (strncmp(mon, months[i], 3) == 0) {
        month = i + 1;
        break;
      }
    }
  }
  cached = month ? seasonForDate(year, month) : year;
  return cached;
}

int fantasySeasonYear() {
  time_t now = time(nullptr);
  struct tm t;
  // Anything before late 2023 means the clock has not been set by NTP yet.
  if (now > 1700000000 && gmtime_r(&now, &t)) {
    return seasonForDate(t.tm_year + 1900, t.tm_mon + 1);
  }
  return buildSeasonYear();
}

// -----------------------------------------------------------------------------
// Cached data (see fantasy.h).
// -----------------------------------------------------------------------------
Team    g_teams[MAX_TEAMS];
int     g_teamCount = 0;
Matchup g_matchups[MAX_MATCHUPS];
int     g_matchupCount = 0;
int     g_currentMatchupPeriod = 0;
int     g_dataGen = 0;

// Measured against the live 2026 league (14 teams, 98 schedule rows):
//   view=mTeam          ~ 50 KB
//   view=mMatchup       ~ 2.5 MB   (also includes teams[])
//   view=mMatchupScore  ~ 273 KB   <-- used for polling, ~9x smaller than mMatchup
// -----------------------------------------------------------------------------

static ErrorState mapResult(const HttpFetch& r) {
  if (r.status == -1) return ErrorState::NO_WIFI;
  if (r.status == 401 || r.status == 403) return ErrorState::LEAGUE_PRIVATE;
  if (r.status == HTTP_CODE_OK && r.parsed) return ErrorState::OK;
  return ErrorState::DATA_ERROR;
}

// Prefer the running per-period total (totalPointsLive); fall back to the
// "final" total this period (totalPoints) once games have wrapped.
static float scoreOf(JsonVariantConst slot) {
  if (!slot["totalPointsLive"].isNull()) return slot["totalPointsLive"] | 0.0f;
  return slot["totalPoints"] | 0.0f;
}

static void buildMatchupUrl(char* buf, size_t len) {
  snprintf(buf, len,
           "%s/%d/segments/0/leagues/%ld?view=mMatchupScore",
           FF_API_BASE, fantasySeasonYear(), (long)LEAGUE_ID);
}

ErrorState fantasyFetchTeams() {
  char url[256];
  snprintf(url, sizeof(url),
           "%s/%d/segments/0/leagues/%ld?view=mTeam",
           FF_API_BASE, fantasySeasonYear(), (long)LEAGUE_ID);

  // Filter: keep only id/abbrev/name per team plus the current period.
  // Assigning through [0] tells ArduinoJson to apply the shape to every array
  // element (see the library's "Filter" documentation).
  DynamicJsonDocument filter(1024);
  filter["status"]["currentMatchupPeriod"] = true;
  filter["teams"][0]["id"]                  = true;
  filter["teams"][0]["abbrev"]              = true;
  filter["teams"][0]["name"]                = true;

  DynamicJsonDocument doc(8192);
  HttpFetch r = httpFetchJson(url, filter, doc);
  ErrorState e = mapResult(r);
  if (e != ErrorState::OK) return e;  // keep the previously cached teams

  // Update the current period even on the team fetch; it is authoritative here.
  int period = doc["status"]["currentMatchupPeriod"] | 0;
  if (period > 0) g_currentMatchupPeriod = period;

  Team tmp[MAX_TEAMS];
  int n = 0;
  for (JsonObject t : doc["teams"].as<JsonArray>()) {
    if (n >= MAX_TEAMS) break;
    Team& team = tmp[n++];
    team.id = t["id"] | 0;
    strlcpy(team.abbrev, t["abbrev"] | "", sizeof(team.abbrev));
    strlcpy(team.name,   t["name"]   | "", sizeof(team.name));
  }

  if (n == 0) return ErrorState::DATA_ERROR;  // unexpected shape: no teams at all

  memcpy(g_teams, tmp, sizeof(Team) * n);
  g_teamCount = n;
  g_dataGen++;
  return ErrorState::OK;
}

ErrorState fantasyFetchMatchups() {
  char url[256];
  buildMatchupUrl(url, sizeof(url));

  // Filter: only the fields we need. Value-based filtering (matchupPeriodId)
  // is not possible in the filter itself, so we skip other periods while
  // copying out of the parsed document below.
  //
  // NOTE on totalPointsLive: while a matchup's games are in progress the server
  // keeps totalPoints at 0.0 and reports the running score in totalPointsLive.
  // After the period wraps it is null again and totalPoints holds the final,
  // so we keep both and prefer the live value when it is present.
  DynamicJsonDocument filter(1024);
  filter["status"]["currentMatchupPeriod"]          = true;
  filter["schedule"][0]["matchupPeriodId"]           = true;
  filter["schedule"][0]["home"]["teamId"]            = true;
  filter["schedule"][0]["home"]["totalPoints"]       = true;
  filter["schedule"][0]["home"]["totalPointsLive"]   = true;
  filter["schedule"][0]["home"]["winProbability"]    = true;
  filter["schedule"][0]["away"]["teamId"]            = true;
  filter["schedule"][0]["away"]["totalPoints"]       = true;
  filter["schedule"][0]["away"]["totalPointsLive"]   = true;
  filter["schedule"][0]["away"]["winProbability"]    = true;

  DynamicJsonDocument doc(32768);
  HttpFetch r = httpFetchJson(url, filter, doc);
  ErrorState e = mapResult(r);
  if (e != ErrorState::OK) return e;  // keep the last good matchups

  int period = doc["status"]["currentMatchupPeriod"] | g_currentMatchupPeriod;
  if (period <= 0) return ErrorState::DATA_ERROR;

  JsonArray schedule = doc["schedule"].as<JsonArray>();
  if (schedule.isNull()) return ErrorState::DATA_ERROR;  // unexpected shape

  Matchup tmp[MAX_MATCHUPS];
  int n = 0;
  for (JsonObject m : schedule) {
    if ((m["matchupPeriodId"] | -1) != period) continue;  // only current period
    if (n >= MAX_MATCHUPS) break;
    Matchup& mu = tmp[n++];
    mu.homeId = m["home"]["teamId"] | 0;
    mu.awayId = m["away"]["teamId"] | 0;
    mu.homeScore = scoreOf(m["home"]);
    mu.awayScore = scoreOf(m["away"]);
    mu.homeWinPct = m["home"]["winProbability"] | 0.0f;
    mu.awayWinPct = m["away"]["winProbability"] | 0.0f;
    mu.live = !m["home"]["totalPointsLive"].isNull() ||
              !m["away"]["totalPointsLive"].isNull();
  }

  g_currentMatchupPeriod = period;
  g_matchupCount = n;
  memcpy(g_matchups, tmp, sizeof(Matchup) * n);
  g_dataGen++;
  return ErrorState::OK;  // n == 0 is valid (e.g. between periods), still "last good"
}

const Matchup* fantasyMatchupForTeam(int32_t teamId) {
  for (int i = 0; i < g_matchupCount; i++) {
    if (g_matchups[i].homeId == teamId || g_matchups[i].awayId == teamId) {
      return &g_matchups[i];
    }
  }
  return nullptr;
}

const Team* fantasyTeamById(int32_t teamId) {
  for (int i = 0; i < g_teamCount; i++) {
    if (g_teams[i].id == teamId) return &g_teams[i];
  }
  return nullptr;
}
