#pragma once
// -----------------------------------------------------------------------------
// config.h - hardcoded settings for the ESP32 scoreboard data layer.
// -----------------------------------------------------------------------------

// ------------------------- Wi-Fi (placeholders) ------------------------------
#define WIFI_SSID       "637"
#define WIFI_PASSWORD   "Sunshine"

// ------------------------- ESPN fantasy league -------------------------------
// The dad's team is TEAM_ID 8 (abbrev "TAK"). The league is public, so no
// cookies / SWID / espn_s2 are required for these read endpoints.
#define LEAGUE_ID       623760961
#define TEAM_ID         8
#define SEASON          2026

// ------------------------------- Timing --------------------------------------
#define POLL_INTERVAL_MS     60000UL   // poll matchups + scoreboards every 60 s
#define WIFI_TIMEOUT_MS      20000UL   // Wi-Fi association timeout
#define HTTP_CONNECT_MS      10000
#define HTTP_READ_MS         15000
#define HTTP_JSON_NESTING_LIMIT  24

// ------------------------------- Display --------------------------------------
#define DISPLAY_PAGE_MS      7000UL    // seconds per scoreboard card
#define DISPLAY_BLINK_MS     500UL     // boot-screen blink cadence
#define DISPLAY_BRIGHTNESS   90        // 0..255 (panel setBrightness8)
#define TAK_DISPLAY_COLOR    0x00FF00  // accent for the dad's fantasy team

// ------------------------------- Endpoints -----------------------------------
// Fantasy is the *unofficial* "lm-api-reads" host. The full URL is assembled in
// fantasy.cpp from LEAGUE_ID / SEASON so there is a single source of truth.
#define FF_API_BASE  "https://lm-api-reads.fantasy.espn.com/apis/v3/games/ffl/seasons"

// Real-sport scoreboards (public, no login).
#define SB_NFL_URL  "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard"
#define SB_MLB_URL  "https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard"
#define SB_NHL_URL  "https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/scoreboard"
#define SB_NBA_URL  "https://site.api.espn.com/apis/site/v2/sports/basketball/nba/scoreboard"
