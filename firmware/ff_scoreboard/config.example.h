#pragma once
// -----------------------------------------------------------------------------
// config.example.h - copy this file to config.h and fill in your own values.
//
// config.h is gitignored so Wi-Fi credentials and league/team ids stay local to
// your machine. The sketch will not compile until config.h exists.
//
// See the README at the repository root for how to find your league and team
// ids, and for build/flash instructions.
// -----------------------------------------------------------------------------

// ------------------------------- Wi-Fi ---------------------------------------
// The board joins a 2.4 GHz Wi-Fi network. (The ESP32 does not support 5 GHz.)
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

// ------------------------- ESPN fantasy league -------------------------------
// LEAGUE_ID is the number in the ESPN league URL.
// TEAM_ID is your team's numeric id within that league. The team abbreviation
// shown on the LED panel is looked up from ESPN automatically.
//
// This firmware reads public leagues only: no ESPN login, cookies, SWID or
// espn_s2 values are needed or used.
//
// The NFL season is detected automatically from the network clock, so there is
// no season value to update each year.
#define LEAGUE_ID       0
#define TEAM_ID         0

// ------------------------------- Timing --------------------------------------
#define POLL_INTERVAL_MS      60000UL   // poll matchups every 60 s
#define WIFI_RETRY_MS         2000UL    // kick WiFi.begin() every 2 s until joined
#define POLL_INITIAL_RETRY_MS 3000UL    // retry an empty first poll every 3 s
#define HTTP_CONNECT_MS       10000
#define HTTP_READ_MS          15000
#define HTTP_JSON_NESTING_LIMIT  24

// ------------------------------- Display -------------------------------------
// 0..255; lower is dimmer and has less LED ghosting.
#define DISPLAY_BRIGHTNESS    60

// ------------------------------- Endpoints -----------------------------------
// ESPN's unofficial read-only fantasy API host. The full URL is assembled in
// fantasy.cpp from the detected season and LEAGUE_ID.
#define FF_API_BASE  "https://lm-api-reads.fantasy.espn.com/apis/v3/games/ffl/seasons"

// Fail loudly if the template was copied but the league/team ids were left at
// their placeholders. ESPN ids are always positive.
#if LEAGUE_ID == 0 || TEAM_ID == 0
#error "Set LEAGUE_ID and TEAM_ID in config.h (copied from config.example.h)."
#endif
