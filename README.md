# Fantasy Football LED Scoreboard

An ESP32 firmware for a 64x32 HUB75 LED matrix that shows your ESPN fantasy
football matchup: both team abbreviations, the live score, and a dotted
win-probability bar underneath.

The board joins Wi-Fi, fetches your league from ESPN's public fantasy API, and
polls the current matchup every 60 seconds. It only repaints the panel when the
data changes, so the display stays flicker-free between updates.

## What you need

- An ESP32 development board
- A 64x32 HUB75 LED matrix panel and a suitable 5 V power supply
- A USB cable and a computer with the Arduino IDE (or `arduino-cli`)
- A 2.4 GHz Wi-Fi network (the ESP32 does not support 5 GHz networks)
- A **public** ESPN fantasy football league

## Software dependencies

Install the ESP32 board package through the Arduino IDE's **Boards Manager**,
and the libraries through **Library Manager**:

- **ESP32 board package** (`esp32` by Espressif) — developed against core 3.3.12
- **ArduinoJson** — v6.20 or newer, or v7 (tested with 6.21.5, 7.4.2 and 7.4.3)
- **ESP32-HUB75-MatrixPanel-I2S-DMA** — the HUB75 panel driver

Wire the panel to the ESP32 according to that library's example for your board.
This firmware is configured for a single 64x32 panel.

## Configuration

All of the per-installation settings live in one file that is **not checked into
git**:

```
firmware/ff_scoreboard/config.h
```

The repository contains a template instead:

```
firmware/ff_scoreboard/config.example.h
```

### First-time setup

1. Make a copy of the template named `config.h`:

   ```sh
   cd firmware/ff_scoreboard
   cp config.example.h config.h
   ```

   (On Windows, copy the file in Explorer and rename it to `config.h`.)

2. Open `config.h` and fill in these values:

   | Setting | What it is |
   | --- | --- |
   | `WIFI_SSID` | Your 2.4 GHz Wi-Fi network name |
   | `WIFI_PASSWORD` | Your Wi-Fi password |
   | `LEAGUE_ID` | Your ESPN fantasy league's numeric id |
   | `TEAM_ID` | Your team's numeric id inside that league |

3. Save the file. `config.h` is gitignored, so it will not be committed by
   accident.

If `config.h` is missing, the sketch will not compile. That is expected; copy
the example first. If you copied the example but left `LEAGUE_ID` or `TEAM_ID`
at `0`, compilation also stops with a reminder to fill them in.

### Finding your league and team ids

The easiest way is from ESPN in a web browser:

1. Open your league. The address looks like:

   ```
   https://fantasy.espn.com/football/league?leagueId=123456789&seasonId=2026
   ```

   The number after `leagueId=` is your `LEAGUE_ID`.

2. Open your team's page. The address includes `teamId=`:

   ```
   https://fantasy.espn.com/football/team?leagueId=123456789&teamId=8&seasonId=2026
   ```

   The number after `teamId=` is your `TEAM_ID`. This is the team's slot id in
   the league, not your ESPN account id.

You can also look the ids up with ESPN's read-only API:

```sh
curl "https://lm-api-reads.fantasy.espn.com/apis/v3/games/ffl/seasons/2026/segments/0/leagues/YOUR_LEAGUE_ID?view=mTeam"
```

Use the year the NFL season started in the URL. In January or February, that is
the previous calendar year. The firmware itself does this automatically; the
year is only needed for this manual lookup.

In the `teams` array, find the object for your team and note its `"id"` and
`"abbrev"`. Set `TEAM_ID` to that `"id"`. The firmware reads the abbreviation
from ESPN automatically and shows it on the panel, so there is no team name to
hardcode.

> **Private leagues:** this firmware only works with public ESPN leagues. No
> ESPN login, cookies, `SWID`, or `espn_s2` values are used. A private league
> will show `LEAGUE_PRIVATE` in the serial log.

### Season selection

The NFL season is **automatic**. After joining Wi-Fi, the board syncs its clock
via NTP and uses the current season year. January and February are treated as
part of the previous season (playoffs and the Super Bowl). If NTP is
unreachable, the board falls back to the year the firmware was compiled.

There is no `SEASON` value in `config.h`.

### Other settings

These have sensible defaults and usually do not need changing:

| Setting | Default | Description |
| --- | --- | --- |
| `POLL_INTERVAL_MS` | `60000` | How often matchups are fetched (ms) |
| `WIFI_RETRY_MS` | `2000` | How often to retry the Wi-Fi join (ms) |
| `POLL_INITIAL_RETRY_MS` | `3000` | Retry delay while waiting for the first data (ms) |
| `HTTP_CONNECT_MS` | `10000` | HTTP connect timeout (ms) |
| `HTTP_READ_MS` | `15000` | HTTP read timeout (ms) |
| `DISPLAY_BRIGHTNESS` | `60` | Panel brightness, `0`–`255`; lower is dimmer and has less ghosting |

The API host is defined as `FF_API_BASE`. The full URL is built in `fantasy.cpp`
from that host, the detected season year, and `LEAGUE_ID`.

## Build and upload

1. Install the ESP32 board package in the Arduino IDE (Boards Manager).
2. Install the three libraries listed above (Library Manager).
3. Open `firmware/ff_scoreboard/ff_scoreboard.ino`.
4. Select your ESP32 board under **Tools → Board** and select the correct USB
   port.
5. Click **Verify** to compile, then **Upload**.
6. Open **Tools → Serial Monitor** at **115200 baud** to watch the boot and
   polling messages.

On boot the panel flashes white and orange, then shows a spinning football while
it joins Wi-Fi and loads the first matchup. Once data arrives it switches to the
scoreboard.

## How it works

- Core 0 handles Wi-Fi, the clock sync, the initial team-list fetch, and the
  matchup poll.
- Core 1 runs the display loop, so animations and repaints never wait on the
  network.
- The season year is derived from the NTP-synced clock, so it rolls over
  automatically.
- The team list is cached once at boot. Only the current matchup period is
  polled after that.
- Your team is always drawn on the left and the opponent on the right. The gold
  portion of the dotted bar is ESPN's estimated win probability for your team.
- The panel only repaints when the fetched data changes.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `config.h: No such file or directory` | Copy `config.example.h` to `config.h` |
| Panel stays black | Check the panel's 5 V power, ribbon cable, and HUB75 wiring; watch the library's `panel begin()` log |
| Serial log says `NO_WIFI` | Confirm the network is 2.4 GHz and the SSID/password are correct |
| Serial log says `LEAGUE_PRIVATE` | The league is private; this firmware only supports public leagues |
| Serial log says `DATA_ERROR` | Check `LEAGUE_ID`; if NTP is unreachable the board falls back to its build year, so recompile after New Year if the clock cannot sync. ESPN may also have changed its JSON |
| No scores during the offseason | There are no matchups to show; the board resumes automatically when the new season starts |
| Board resets or flickers | The panel may be drawing more current than the power supply can provide |

The serial log prints the fetched teams once at boot and the matchups on every
successful poll, which is the quickest way to confirm the ids are right.

## Project layout

```
firmware/ff_scoreboard/
  ff_scoreboard.ino    Arduino sketch: boot, Wi-Fi join, polling task
  config.example.h     Template configuration (committed)
  config.h             Your local settings (gitignored)
  fantasy.cpp/.h       ESPN fetching and cached league data
  display.cpp/.h       64x32 HUB75 rendering
  http_json.cpp/.h     HTTPS + streaming JSON helper
  http_body_stream.h   HTTP body framing decoder
  types.h              Shared structs and error states
```

## Customizing the display

Most of the visual code lives in `display.cpp`. Colors, fonts, the seven-segment
digits, the football loader, and the win-probability bar are all defined there.
Brightness is in `config.h`.
