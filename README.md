# Even Flow clock

A round 24-hour desk clock with switchable overlays for tides, tidal currents,
weather and wind, sun and moon. Target hardware is the Elecrow CrowPanel 2.1" ESP32-S3 rotary display
(480x480 round IPS, bezel knob with press). For now it runs in an LVGL desktop
simulator.

## Build and run (macOS)

```bash
brew install cmake sdl2
git submodule update --init
cmake -S . -B build
cmake --build build -j
./build/even_flow_sim
```

Simulator controls:

| Knob action | Simulator               |
|-------------|-------------------------|
| Rotate      | Mouse wheel, Left/Right |
| Press       | Middle click, Enter     |
| Long press  | Hold middle click or Enter (or the left button, as touch) |
| Touch       | Left click              |

A long press opens the setup card: a QR code for the settings page, with its
address. Scan it with a phone; any press or turn closes it. In the simulator
the code points at this Mac's dev server (run it with `--lan`). On first boot
the board will use the same card to show how to join its setup network.

Rotating steps through the overlays in the order set by `screens` (default:
tide, current, sun, moon, weather). Pressing toggles the center readout to
more detail:

| Overlay | Band                                              | Readout                 | Pressed                                |
|---------|---------------------------------------------------|-------------------------|----------------------------------------|
| Tide    | water level through the day; each high and low labelled with time and height; rings at round heights | rising/falling, height  | next two highs and lows                |
| Current | slack circle mid-band; flood (teal) swells outward, ebb (violet) inward; darts at each maximum point where the water goes; maxima labelled with time and speed, slacks with time; rings at round speeds | "Ebb 1.9 kt, 183°", next slack | next two slacks or maxima |
| Sun     | night, astronomical, nautical and civil twilight, daylight | sunrise and sunset | day length, civil twilight             |
| Moon    | when the moon is up, plus a phase disc            | phase and percent lit   | moonrise, moonset, next quarter        |
| Weather | sky by hour, rain chance bars, red rim for storms, an icon and temperature per 3 hours, dashed red wind line (further out = stronger, 0-30 kt) with arrowheads pointing downwind every 2 hours and its speed labelled every 3 hours | icon, a short outlook ("Overcast, rain in 40 min"), temperature, wind | high and low, when rain starts or stops |

Under the readout, each overlay names where its data comes from: the tide
station on the tide overlay, the current station on the current overlay, and
the configured place on the others (or its coordinates, if it has no name).
These differ when the nearest good station isn't at your doorstep.

Tide and current bands have a scale: faint rings at round values (1, 2, 2.5 or
5 times a power of ten), labelled in a vertical stack straight up or straight
down from the center, whichever is further from the day's events.

Options:

```
--date YYYY-MM-DD   show a given local date
--time HH:MM        freeze the clock at a time of day
--layer N           start on overlay N (0 tide, 1 sun, 2 moon, 3 weather, 4 current; default: first in `screens`)
--top H             hour at the top of the dial, overriding the settings (0 = midnight up)
--detail            start with the readout toggled
--setup             start with the setup card open
--config FILE       settings file (see below)
--weather FILE      use a saved Open-Meteo response instead of fetching live
--tide FILE         use a saved NOAA tide response (likewise --current FILE)
--snapshot out.bmp  render one frame without a window and exit
```

Run the unit tests with `ctest --test-dir build`. They use saved real API
responses in `tests/data`, so they don't need a network.

## Settings

The clock's settings are one `key=value` per line: place name, latitude and
longitude, time zone, NOAA tide station, NOAA current station and depth bin,
top hour, units (tide, temperature, wind), and `screens`, the knob order
(e.g. `screens=tide,current,weather,sun,moon`; leave one out to hide it). See
[sim/example.conf](sim/example.conf), set up for The Battery, NY. The simulator reads `--config FILE`,
else `even-flow.conf` in the project root (gitignored, for your own location),
else the example. The simulator watches the file and updates when it changes.

Location drives everything. Weather and sun/moon use the coordinates
directly. The tide and current stations are picked by a person from the
stations nearest those coordinates, then stored. It is never re-derived, because the nearest
station isn't always the right one in bays and rivers.

### Settings page

The board will serve a small page over WiFi ([web/index.html](web/index.html),
plain JavaScript). Your phone does the searching: Open-Meteo for the place,
NOAA's station lists for the nearest tide and current stations (with depths).
Only the finished settings go to the board. All three APIs allow this
(they send `Access-Control-Allow-Origin: *`).

The page and the board talk with two requests, in the same `key=value` text
as the settings file:

    GET  /api/config    current settings
    POST /api/config    new settings; checked by the settings parser, then saved,
                        or rejected with "N problem(s): line L: ..." (HTTP 400)

On the Mac, `tools/dev_server.py` stands in for the board (standard library
only). It checks saves with `build/even_flow_config_check`, the same parser
the board will use, and writes `even-flow.conf`. A running simulator watches
that file and updates within a second:

```bash
python3 tools/dev_server.py        # http://localhost:8080, or --lan to try it from your phone
./build/even_flow_sim              # in another terminal
```

The time zone rule comes from `web/zones.json`, an IANA-to-POSIX table from
[posix_tz_db](https://github.com/nayarsystems/posix_tz_db) (MIT).

Still to come on the board: WiFi setup on first boot, saving to flash, and the
web server itself.

## Tides and currents

Both come from NOAA CO-OPS predictions (free, no key), fetched once a day for
yesterday through tomorrow so the day is bracketed at both midnights
(`src/data/noaa.c`). A failed fetch shows no data, never invented data.

The tide curve is built from high and low times alone, joined with half
cosines, which are flat at the turns as the water is. The current is built
from slack and maximum times (NOAA's `MAX_SLACK`), joined with quarter sines:
the current changes fastest at slack and is flat at a maximum
(`src/data/curve.c`). Against NOAA's own 6-minute predictions for the Hudson
River Entrance this is within 0.04 kt on average, and the tests hold it there. Most NOAA stations are subordinate stations that only
publish those events, so the one path works everywhere.

Currents have their own station (`current_station_id`) and depth bin
(`current_bin`; NOAA predicts several depths at many stations, and the
shallowest is what a boat feels). Speeds are signed (flood +, ebb -), and
directions are NOAA's mean set, where the water goes. Don't infer the current
from the tide: at The Battery on 2026-10-13, slack came nearly four hours
after high water. The example settings use The Battery with the Hudson River
Entrance current.

## Sun and moon

Computed on the device with [Astronomy Engine](https://github.com/cosinekitty/astronomy)
(v2.1.19, MIT, vendored in `lib/astronomy`), no network needed. `src/data/sky.c`
turns it into spans of minutes for the dial. The tests check The Battery, NY,
on 2026-10-13 against the US Naval Observatory, to within a minute.

The library works in double precision, which the ESP32-S3 does in software, so
day events are computed once a day and the moon phase every few minutes.

## Weather

Hourly forecast for today and tomorrow from [Open-Meteo](https://open-meteo.com)
(free, no API key), refreshed every 30 minutes and at midnight. Data is kept
in °F and knots and converted for display (`temp_unit`, `wind_unit` in the
settings). An hour counts as stormy for thunder or gusts of 34 kt (gale)
and up.

The outlook line uses Open-Meteo's 15-minute forecast for the next four
hours, then the hourly one out to 12 hours. Its "current" reading is trusted
only within 30 minutes of the time it was taken. The center icon follows the
same logic as the words, so they always agree. All of this is model output,
not a rain gauge: it can say overcast while it's raining on you.

The simulator fetches with `curl`; `tests/data` holds real
responses for The Battery and for Scotia, NY (with 15-minute data), and a
synthetic stormy copy, for tests and previews, plus real NOAA tide and
current responses for The Battery.

Icons are [Meteocons](https://github.com/basmilius/weather-icons) by Bas
Milius (MIT), 64 px in the center and 40 px on the ring. The SVGs live in
`assets/meteocons`. `tools/make_icons.py` turns them into LVGL image data in
`src/ui/icons`. Re-run it only when icons or sizes change; it needs
`brew install librsvg` and Pillow. The images are RGB565A8, drawn straight
from flash with no decoding, about 334 KB in all.

## Layout

```
config/lv_conf.h   LVGL configuration for the simulator
lib/lvgl           LVGL v9.4.0 (git submodule)
lib/astronomy      Astronomy Engine v2.1.19 (vendored)
lib/cjson          cJSON v1.7.19 (vendored)
assets/meteocons   weather icon sources (SVG)
tools/             icon converter, settings dev server and checker
web/               settings page and time zone table
src/config/        settings record and its text format (plain C)
src/data/          tide, current, sun, moon and weather models, NOAA and Open-Meteo parsers (plain C)
src/ui/            the dial, setup card and generated icons; depends only on LVGL and the above
sim/               SDL harness (window, input, clock, fetching, snapshot) and example settings
tests/             unit tests for src/config and src/data, with saved API responses in tests/data
```

Anything under `src/` has to build unchanged for the ESP32. Platform-specific
code (SDL, and later the display, touch and knob drivers) stays in the harness.

## Notes for the port

- On the simulator the round panel is faked with a black ring over the corners
  (`create_bezel_mask` in `sim/main.c`). The UI itself never clips, since the
  real panel has no pixels out there.
- The dial takes keys from an LVGL group in edit mode. On the board, register
  the knob as an `LV_INDEV_TYPE_ENCODER` and call `dial_attach_group()`, and the
  UI code needs no changes.
- `config/lv_conf.h` uses the C library's malloc and printf. The firmware will
  need its own config: LVGL's builtin allocator, or one pointed at PSRAM.
- Tide times are minutes since local midnight. Request NOAA data with
  `time_zone=lst_ldt` so it arrives in that form.
- The simulator fetches on the UI thread with `curl`, which can stall the
  window. On the board, fetch from a background task and hand finished data to
  the UI (`dial_set_tide()`, `dial_set_current()`, `dial_set_weather()`).
- ESP-IDF ships its own cJSON. Use one copy, not both, to avoid duplicate symbols.
- The firmware's `lv_conf.h` needs `LV_USE_QRCODE 1` for the setup card, and
  the Montserrat sizes the dial uses (12, 14, 20, 48).

## Licence

BSD 3-Clause, see [LICENSE](LICENSE). Bundled third-party code and data keep
their own licences, all MIT:

| Component | Where | Licence file |
|---|---|---|
| [LVGL](https://github.com/lvgl/lvgl) v9.4.0 | `lib/lvgl` (submodule) | `lib/lvgl/LICENCE.txt` |
| [Astronomy Engine](https://github.com/cosinekitty/astronomy) v2.1.19 | `lib/astronomy` | `lib/astronomy/LICENSE` |
| [cJSON](https://github.com/DaveGamble/cJSON) v1.7.19 | `lib/cjson` | `lib/cjson/LICENSE` |
| [Meteocons](https://github.com/basmilius/weather-icons) v2.0.0 | `assets/meteocons`, `src/ui/icons` | `assets/meteocons/LICENSE` |
| [posix_tz_db](https://github.com/nayarsystems/posix_tz_db) | `web/zones.json` | `web/zones.LICENSE` |

Tide and current predictions come from NOAA CO-OPS, weather from
[Open-Meteo](https://open-meteo.com) (CC BY 4.0, free for non-commercial use).
