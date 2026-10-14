/**
 * @file test_main.c
 * Unit tests for the portable, non-UI code. No framework: each check prints
 * on failure and the process exits non-zero if any failed.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config/config.h"
#include "data/sky.h"
#include "data/tide.h"
#include "data/curve.h"
#include "data/noaa.h"
#include "data/weather.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if(!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while(0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

/*********************
 *  CONFIG
 *********************/
static void test_config_defaults(void)
{
    clock_config_t cfg;
    config_defaults(&cfg);
    CHECK(!config_has_location(&cfg));
    CHECK(!config_has_station(&cfg));
    CHECK(cfg.top_hour == 12);
    CHECK(cfg.units == UNITS_FEET);
    CHECK(cfg.temp_unit == TEMP_FAHRENHEIT);
    CHECK(cfg.wind_unit == WIND_KNOTS);
    CHECK(cfg.screen_count == SCREEN_COUNT);
    CHECK(cfg.screens[0] == SCREEN_TIDE && cfg.screens[1] == SCREEN_CURRENT && cfg.screens[4] == SCREEN_WEATHER);
    CHECK(!config_has_current_station(&cfg));
    CHECK(cfg.current_bin == 0);
    CHECK(strcmp(cfg.tz_posix, "UTC0") == 0);
}

static void test_config_parse(void)
{
    clock_config_t cfg;
    config_defaults(&cfg);

    const char * text =
        "# comment\r\n"
        "\n"
        "  place_name = Sausalito, CA  \r\n"
        "latitude=37.8591\n"
        "longitude=-122.4853\n"
        "tz_posix=PST8PDT,M3.2.0,M11.1.0\n"
        "station_id=9414806\n"
        "station_name=Sausalito\n"
        "top_hour=0\n"
        "units=m\n"
        "some_future_key=whatever"; /* no trailing newline, unknown key */

    char err[64];
    CHECK(config_parse(&cfg, text, err, sizeof(err)) == 0);
    CHECK(strcmp(cfg.place_name, "Sausalito, CA") == 0);
    CHECK_NEAR(cfg.latitude, 37.8591, 1e-9);
    CHECK_NEAR(cfg.longitude, -122.4853, 1e-9);
    CHECK(strcmp(cfg.tz_posix, "PST8PDT,M3.2.0,M11.1.0") == 0);
    CHECK(strcmp(cfg.station_id, "9414806") == 0);
    CHECK(cfg.top_hour == 0);
    CHECK(cfg.units == UNITS_METRES);
    CHECK(config_has_location(&cfg));
    CHECK(config_has_station(&cfg));
}

static void test_config_rejects_bad_values(void)
{
    clock_config_t cfg;
    config_defaults(&cfg);
    cfg.top_hour = 6;

    char err[64];
    int errors = config_parse(&cfg,
                              "latitude=91\n"
                              "longitude=abc\n"
                              "top_hour=24\n"
                              "units=furlongs\n"
                              "wind_unit=beaufort\n"
                              "station_id=94 14\n"
                              "tz_posix=\n"
                              "no equals sign\n",
                              err, sizeof(err));
    CHECK(errors == 8);
    CHECK(strcmp(err, "line 1: bad value for latitude") == 0);

    /* Bad values leave fields untouched */
    CHECK(!config_has_location(&cfg));
    CHECK(cfg.top_hour == 6);
    CHECK(cfg.units == UNITS_FEET);
    CHECK(!config_has_station(&cfg));
    CHECK(strcmp(cfg.tz_posix, "UTC0") == 0);

    /* Too long for the field */
    char long_name[128];
    memset(long_name, 'x', sizeof(long_name));
    memcpy(long_name, "place_name=", 11);
    long_name[100] = '\0';
    CHECK(config_parse(&cfg, long_name, NULL, 0) == 1);
    CHECK(cfg.place_name[0] == '\0');
}

static void test_config_screens(void)
{
    clock_config_t cfg;
    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "screens = weather, tide,moon\n", NULL, 0) == 0);
    CHECK(cfg.screen_count == 3);
    CHECK(cfg.screens[0] == SCREEN_WEATHER);
    CHECK(cfg.screens[1] == SCREEN_TIDE);
    CHECK(cfg.screens[2] == SCREEN_MOON);

    /* Unknown, repeated or empty lists are refused and leave the order alone */
    CHECK(config_parse(&cfg, "screens=tide,wind\n", NULL, 0) == 1);
    CHECK(config_parse(&cfg, "screens=tide,tide\n", NULL, 0) == 1);
    CHECK(config_parse(&cfg, "screens=\n", NULL, 0) == 1);
    CHECK(config_parse(&cfg, "screens=tides\n", NULL, 0) == 1);
    CHECK(cfg.screen_count == 3 && cfg.screens[0] == SCREEN_WEATHER);

    char text[CONFIG_TEXT_MAX];
    CHECK(config_format(&cfg, text, sizeof(text)) > 0);
    CHECK(strstr(text, "screens=weather,tide,moon\n") != NULL);
    clock_config_t back;
    config_defaults(&back);
    CHECK(config_parse(&back, text, NULL, 0) == 0);
    CHECK(back.screen_count == 3 && back.screens[2] == SCREEN_MOON);
}

static void test_config_round_trip(void)
{
    clock_config_t a;
    config_defaults(&a);
    config_parse(&a,
                 "place_name=Friday Harbor, WA\n"
                 "latitude=48.5465\n"
                 "longitude=-123.0130\n"
                 "tz_name=America/Los_Angeles\n"
                 "tz_posix=PST8PDT,M3.2.0,M11.1.0\n"
                 "station_id=9449880\n"
                 "station_name=Friday Harbor\n"
                 "top_hour=3\n"
                 "units=m\n"
                 "current_station_id=PUG1515\n"
                 "current_station_name=Admiralty Inlet\n"
                 "current_bin=4\n"
                 "temp_unit=c\n"
                 "wind_unit=kmh\n",
                 NULL, 0);

    char text[CONFIG_TEXT_MAX];
    size_t n = config_format(&a, text, sizeof(text));
    CHECK(n > 0);

    clock_config_t b;
    config_defaults(&b);
    CHECK(config_parse(&b, text, NULL, 0) == 0);
    CHECK(strcmp(a.place_name, b.place_name) == 0);
    CHECK_NEAR(a.latitude, b.latitude, 1e-6);
    CHECK_NEAR(a.longitude, b.longitude, 1e-6);
    CHECK(strcmp(a.tz_name, b.tz_name) == 0);
    CHECK(strcmp(a.tz_posix, b.tz_posix) == 0);
    CHECK(strcmp(a.station_id, b.station_id) == 0);
    CHECK(strcmp(a.station_name, b.station_name) == 0);
    CHECK(a.top_hour == b.top_hour);
    CHECK(a.units == b.units);
    CHECK(b.temp_unit == TEMP_CELSIUS);
    CHECK(strcmp(b.current_station_id, "PUG1515") == 0);
    CHECK(strcmp(b.current_station_name, "Admiralty Inlet") == 0);
    CHECK(b.current_bin == 4);
    CHECK(b.wind_unit == WIND_KMH);

    /* An unset location survives the round trip as unset */
    clock_config_t c, d;
    config_defaults(&c);
    CHECK(config_format(&c, text, sizeof(text)) > 0);
    config_defaults(&d);
    d.latitude = 1.0;
    d.longitude = 2.0;
    CHECK(config_parse(&d, text, NULL, 0) == 0);
    CHECK(!config_has_location(&d));

    /* Too-small buffer and corrupting newlines are refused */
    CHECK(config_format(&a, text, 16) == 0);
    strcpy(a.place_name, "bad\nname");
    CHECK(config_format(&a, text, sizeof(text)) == 0);
}

/*********************
 *  TIDE
 *********************/
static void test_tide_hilo_curve(void)
{
    /* Low before midnight through high after the next midnight */
    const tide_event_t ev[] = {
        {-120.0f, 0.5f, false},
        {250.0f, 5.5f, true},
        {640.0f, 1.0f, false},
        {1010.0f, 6.0f, true},
        {1500.0f, -0.4f, false},
    };
    tide_day_t day;
    tide_build_from_hilo(&day, ev, sizeof(ev) / sizeof(ev[0]));

    CHECK(day.count == TIDE_MAX_SAMPLES);
    CHECK(day.interval_min == TIDE_SAMPLE_MIN);

    /* Only today's turns are kept */
    CHECK(day.event_count == 3);
    CHECK(day.events[0].high && day.events[0].minute == 250.0f);
    CHECK(!day.events[1].high);

    /* Curve passes through the turns (within one sample's interpolation) */
    CHECK_NEAR(tide_height_at(&day, 250.0f), 5.5, 0.01);
    CHECK_NEAR(tide_height_at(&day, 640.0f), 1.0, 0.01);
    CHECK_NEAR(tide_height_at(&day, 1010.0f), 6.0, 0.01);

    /* Half-cosine: halfway between two turns is the mean height */
    CHECK_NEAR(tide_height_at(&day, 445.0f), (5.5 + 1.0) / 2.0, 0.02);

    /* Monotonic between turns: falling from 250 to 640 (start where m - 6 is past the high) */
    for(float m = 256.0f; m <= 640.0f; m += 6.0f) {
        CHECK(tide_height_at(&day, m) <= tide_height_at(&day, m - 6.0f) + 1e-4f);
    }

    /* Midnight is bracketed by the turns either side, so not held flat */
    CHECK(day.height_ft[0] > 0.5f && day.height_ft[0] < 5.5f);

    /* Range spans today's samples only; the -0.4 low is tomorrow */
    CHECK_NEAR(day.max_ft, 6.0, 0.01);
    CHECK(day.min_ft > -0.4f);

    /* Next event lookup */
    const tide_event_t * next = tide_next_event(&day, 300.0f);
    CHECK(next != NULL && next->minute == 640.0f);
    CHECK(tide_next_event(&day, 1200.0f) == NULL);
}

static void test_nice_step(void)
{
    CHECK_NEAR(curve_nice_step(5.2f, 4), 2.0, 1e-6);   /* The Battery's day: rings at 0, 2, 4 ft */
    CHECK_NEAR(curve_nice_step(4.0f, 4), 1.0, 1e-6);
    CHECK_NEAR(curve_nice_step(1.6f, 4), 0.5, 1e-6);   /* metres */
    CHECK_NEAR(curve_nice_step(2.5f, 3), 1.0, 1e-6);   /* a 2.5 kt current scale */
    CHECK_NEAR(curve_nice_step(9.0f, 4), 2.5, 1e-6);
    CHECK_NEAR(curve_nice_step(0.3f, 3), 0.1, 1e-6);
    CHECK(curve_nice_step(0.0f, 4) == 0.0f);
    CHECK(curve_nice_step(5.0f, 0) == 0.0f);
}

static void test_tide_unbracketed_and_empty(void)
{
    /* A single event: the whole day holds flat at its height */
    const tide_event_t one = {600.0f, 3.0f, true};
    tide_day_t day;
    tide_build_from_hilo(&day, &one, 1);
    CHECK_NEAR(day.height_ft[0], 3.0, 1e-6);
    CHECK_NEAR(day.height_ft[day.count - 1], 3.0, 1e-6);
    CHECK(day.event_count == 1);

    tide_build_from_hilo(&day, NULL, 0);
    CHECK(day.count == 0);
    CHECK(day.event_count == 0);
    CHECK(tide_height_at(&day, 100.0f) == 0.0f);
}

static void test_tide_fake(void)
{
    tide_day_t day;
    tide_fake_generate(&day, 20000);
    CHECK(day.count == TIDE_MAX_SAMPLES);
    /* Mixed semi-diurnal: 3 to 5 turns in a day */
    CHECK(day.event_count >= 3 && day.event_count <= 5);
    for(uint8_t i = 1; i < day.event_count; i++) {
        CHECK(day.events[i].minute > day.events[i - 1].minute);
        CHECK(day.events[i].high != day.events[i - 1].high); /* highs and lows alternate */
    }
}

/*********************
 *  SKY
 *********************/
static void set_tz(const char * tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

static float hm(int h, int m)
{
    return (float)(h * 60 + m);
}

/* Reference: US Naval Observatory, The Battery NY, 2026-10-13 (EDT).
 * USNO rounds to the minute, so allow a little over one minute. */
static void test_sky_battery(void)
{
    set_tz("EST5EDT,M3.2.0,M11.1.0");

    sky_day_t d;
    CHECK(sky_compute_day(&d, 40.700554, -74.01417, 2026, 10, 13));

    CHECK_NEAR(d.sunrise, hm(7, 5), 1.1);
    CHECK_NEAR(d.sunset, hm(18, 19), 1.1);
    CHECK_NEAR(d.solar_noon, hm(12, 42), 1.1);
    CHECK(d.day.count == 1);
    CHECK_NEAR(d.day.span[0].start, d.sunrise, 1e-3);
    CHECK_NEAR(d.day.span[0].end, d.sunset, 1e-3);

    CHECK(d.civil.count == 1);
    CHECK_NEAR(d.civil.span[0].start, hm(6, 38), 1.1);
    CHECK_NEAR(d.civil.span[0].end, hm(18, 46), 1.1);

    /* Twilight bands nest: each starts earlier and ends later than the last */
    CHECK(d.nautical.count == 1 && d.astro.count == 1);
    CHECK(d.astro.span[0].start < d.nautical.span[0].start);
    CHECK(d.nautical.span[0].start < d.civil.span[0].start);
    CHECK(d.civil.span[0].end < d.nautical.span[0].end);
    CHECK(d.nautical.span[0].end < d.astro.span[0].end);

    CHECK_NEAR(d.moonrise, hm(10, 21), 1.1);
    CHECK_NEAR(d.moonset, hm(19, 34), 1.1);
    CHECK(d.moon_up.count == 1);

    /* Noon EDT = 16:00 UTC */
    struct tm noon = {.tm_year = 126, .tm_mon = 9, .tm_mday = 13, .tm_hour = 12, .tm_isdst = -1};
    sky_moon_t m;
    CHECK(sky_compute_moon(&m, mktime(&noon)));
    CHECK(strcmp(sky_phase_name(m.phase_deg), "Waxing crescent") == 0);
    CHECK_NEAR(m.illumination, 0.10, 0.04);

    /* USNO: first quarter 2026-10-18 16:12 UT, full 2026-10-26 04:12 UT */
    struct tm fq = {.tm_year = 126, .tm_mon = 9, .tm_mday = 18, .tm_hour = 16, .tm_min = 12};
    struct tm full = {.tm_year = 126, .tm_mon = 9, .tm_mday = 26, .tm_hour = 4, .tm_min = 12};
    CHECK(m.next[0].quarter == MOON_FIRST_QUARTER);
    CHECK(m.next[1].quarter == MOON_FULL);
    CHECK(llabs((long long)(m.next[0].when - timegm(&fq))) <= 90);
    CHECK(llabs((long long)(m.next[1].when - timegm(&full))) <= 90);
}

/* Tromsø: midnight sun at the June solstice, polar night at the December one */
static void test_sky_polar(void)
{
    set_tz("CET-1CEST,M3.5.0,M10.5.0/3");

    sky_day_t d;
    CHECK(sky_compute_day(&d, 69.6496, 18.9560, 2026, 6, 21));
    CHECK(isnan(d.sunrise) && isnan(d.sunset));
    CHECK(d.day.count == 1);
    CHECK(d.day.span[0].start == 0.0f && d.day.span[0].end == 1440.0f);

    CHECK(sky_compute_day(&d, 69.6496, 18.9560, 2026, 12, 21));
    CHECK(isnan(d.sunrise) && isnan(d.sunset));
    CHECK(d.day.count == 0);
    CHECK(d.civil.count == 1); /* but there is twilight around midday */
}

static void test_sky_names(void)
{
    CHECK(strcmp(sky_phase_name(0.0f), "New moon") == 0);
    CHECK(strcmp(sky_phase_name(359.0f), "New moon") == 0);
    CHECK(strcmp(sky_phase_name(90.0f), "First quarter") == 0);
    CHECK(strcmp(sky_phase_name(180.0f), "Full moon") == 0);
    CHECK(strcmp(sky_phase_name(300.0f), "Waning crescent") == 0);
    CHECK(strcmp(sky_quarter_name(MOON_LAST_QUARTER), "Last quarter") == 0);
}

/*********************
 *  WEATHER
 *********************/
static char * read_fixture(const char * name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/tests/data/%s", EVEN_FLOW_SOURCE_DIR, name);
    FILE * f = fopen(path, "rb");
    if(f == NULL) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char * buf = malloc((size_t)len + 1);
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* A real response for The Battery, saved 2026-10-13 */
static void test_weather_parse_real(void)
{
    char * json = read_fixture("open-meteo-battery-2026-10-13.json");
    CHECK(json != NULL);
    if(json == NULL) return;

    weather_t wx;
    char err[80];
    CHECK(weather_parse_open_meteo(&wx, json, err, sizeof(err)));
    free(json);

    CHECK(wx.hour_count == 48);
    CHECK(strcmp(wx.first_date, "2026-10-13") == 0);
    CHECK(wx.hours[0].minute == 0.0f);
    CHECK(wx.hours[23].minute == 23 * 60.0f);
    CHECK(wx.hours[24].minute == 1440.0f); /* tomorrow continues the count */
    CHECK_NEAR(wx.current.temp_f, 68.5, 1e-4);
    CHECK(wx.current.code == 0);
    CHECK_NEAR(wx.current.wind_kt, 9.2, 1e-4);
    CHECK(wx.current.wind_from_deg == 158);
    CHECK_NEAR(wx.today_high_f, 70.3, 1e-4);
    CHECK_NEAR(wx.today_low_f, 57.6, 1e-4);
    CHECK(wx.hours[6].code == 3);
    CHECK(!weather_hour_is_stormy(&wx.hours[12]));
}

/* The same response with an invented afternoon thunderstorm */
static void test_weather_parse_storm(void)
{
    char * json = read_fixture("open-meteo-synthetic-storm.json");
    CHECK(json != NULL);
    if(json == NULL) return;

    weather_t wx;
    CHECK(weather_parse_open_meteo(&wx, json, NULL, 0));
    free(json);

    const weather_hour_t * h = weather_hour_at(&wx, 16 * 60 + 30);
    CHECK(h != NULL && h->code == 95);
    CHECK(h != NULL && h->precip_prob == 90);
    CHECK(h != NULL && weather_hour_is_stormy(h));
    CHECK(!weather_hour_is_stormy(&wx.hours[15])); /* gusts 28 kt but rain only: not stormy */
    CHECK(weather_severity(&wx.hours[16]) > weather_severity(&wx.hours[15]));
    CHECK(weather_kind(wx.hours[7].code) == WX_FOG);
    CHECK(weather_hour_at(&wx, 48 * 60.0f) == NULL);

    /* Wet by chance alone, by the model's own call, or not at all */
    weather_hour_t h2 = {.code = 3, .precip_prob = 60};
    CHECK(weather_hour_is_wet(&h2));
    h2 = (weather_hour_t){.code = 53, .precip_prob = 12, .precip_in = 0.02f}; /* Scotia, 17:00 */
    CHECK(weather_hour_is_wet(&h2));
    h2 = (weather_hour_t){.code = 53, .precip_prob = 12, .precip_in = 0.0f};
    CHECK(!weather_hour_is_wet(&h2));
    h2 = (weather_hour_t){.code = 3, .precip_prob = 40, .precip_in = 0.05f}; /* amount but dry code */
    CHECK(!weather_hour_is_wet(&h2));
}

static void check_outlook(const weather_t * wx, int h, int m, const char * want, const char * want_short)
{
    char text[48], short_text[32];
    weather_outlook(wx, (float)(h * 60 + m), text, sizeof(text), short_text, sizeof(short_text));
    if(strcmp(text, want) != 0 || strcmp(short_text, want_short) != 0) {
        fprintf(stderr, "  outlook at %02d:%02d: got \"%s\" / \"%s\", want \"%s\" / \"%s\"\n", h, m, text,
                short_text, want, want_short);
    }
    CHECK(strcmp(text, want) == 0);
    CHECK(strcmp(short_text, want_short) == 0);
}

/* Real Scotia, NY response with 15-minute data, saved 2026-10-13 16:15:
 * rain 17:00-17:30, a little drizzle at 17:45, heavier rain from 21:00 */
static void test_weather_outlook_scotia(void)
{
    char * json = read_fixture("open-meteo-scotia-2026-10-13.json");
    CHECK(json != NULL);
    if(json == NULL) return;
    weather_t wx;
    CHECK(weather_parse_open_meteo(&wx, json, NULL, 0));
    free(json);

    CHECK(wx.quarter_count == 16);
    CHECK(wx.quarters[0].minute == 16 * 60 + 15);
    CHECK(wx.quarters[3].code == 61);

    check_outlook(&wx, 16, 19, "Overcast, rain in 40 min", "Rain in 40 min");
    check_outlook(&wx, 17, 5, "Light rain until 17:30", "Rain until 17:30");
    check_outlook(&wx, 17, 35, "Overcast, drizzle in 10 min", "Drizzle in 10 min");
    check_outlook(&wx, 18, 10, "Overcast, rain at 21:00", "Rain at 21:00");

    /* The icon follows the same "now" as the words */
    CHECK(weather_code_now(&wx, 16 * 60 + 19) == 3);
    CHECK(weather_code_now(&wx, 17 * 60 + 5) == 61);
}

static void test_weather_outlook_hourly_only(void)
{
    /* Clear all day, no 15-minute data */
    char * json = read_fixture("open-meteo-battery-2026-10-13.json");
    CHECK(json != NULL);
    if(json == NULL) return;
    weather_t wx;
    CHECK(weather_parse_open_meteo(&wx, json, NULL, 0));
    free(json);
    CHECK(wx.quarter_count == 0);
    check_outlook(&wx, 16, 12, "Clear", "Clear");

    /* Storm. The response's "current" reading (thunderstorm) is for 16:00, so it
     * is used near then and the hourly code otherwise */
    json = read_fixture("open-meteo-synthetic-storm.json");
    CHECK(json != NULL);
    if(json == NULL) return;
    CHECK(weather_parse_open_meteo(&wx, json, NULL, 0));
    free(json);
    check_outlook(&wx, 16, 5, "Thunderstorm until 20:00", "Storms until 20:00");
    check_outlook(&wx, 15, 10, "Rain until 20:00", "Rain until 20:00");
    check_outlook(&wx, 12, 30, "Partly cloudy, rain at 14:00", "Rain at 14:00");
}

/*********************
 *  NOAA
 *********************/
/* Real predictions for The Battery, 2026-10-12 to 14 */
static void test_noaa_tide(void)
{
    char * json = read_fixture("noaa-tide-8518750-20261012-14.json");
    CHECK(json != NULL);
    if(json == NULL) return;
    tide_day_t day;
    char err[80];
    CHECK(noaa_parse_tide(&day, json, 2026, 10, 13, err, sizeof(err)));
    free(json);

    /* Today's four turns, matching NOAA's table */
    CHECK(day.event_count == 4);
    CHECK(!day.events[0].high && day.events[0].minute == 4 * 60 + 27);
    CHECK(day.events[1].high && day.events[1].minute == 10 * 60 + 30);
    CHECK_NEAR(day.events[1].height_ft, 5.449, 1e-3);
    CHECK(day.events[3].minute == 22 * 60 + 56);
    CHECK_NEAR(tide_height_at(&day, 10 * 60 + 30), 5.449, 0.01);
    /* Midnight is bracketed by yesterday's 22:12 high and today's 04:27 low */
    CHECK(day.height_ft[0] < 4.774f && day.height_ft[0] > 0.451f);

    /* Same data, seen as the next day: the 04:27 low is now yesterday */
    json = read_fixture("noaa-tide-8518750-20261012-14.json");
    CHECK(noaa_parse_tide(&day, json, 2026, 10, 14, NULL, 0));
    free(json);
    CHECK(day.event_count == 4);
    CHECK(day.events[0].minute > 0.0f && day.events[0].minute < 1440.0f);

    CHECK(!noaa_parse_tide(&day, "{\"error\": {\"message\":\" The station is not a valid station.\"}}",
                           2026, 10, 13, err, sizeof(err)));
    CHECK(strcmp(err, "NOAA: The station is not a valid station.") == 0);
}

/* Real current predictions for Hudson River Entrance (NYH1927), 7 ft depth */
static void test_noaa_current(void)
{
    char * json = read_fixture("noaa-current-NYH1927-bin13-20261012-14.json");
    CHECK(json != NULL);
    if(json == NULL) return;
    current_day_t day;
    char err[80];
    CHECK(noaa_parse_current(&day, json, 2026, 10, 13, err, sizeof(err)));
    free(json);

    CHECK(day.flood_dir == 11);
    CHECK(day.ebb_dir == 183);
    CHECK(day.event_count == 8);

    /* NOAA's table for the day: slack 14:16, max ebb 17:40 at 2.21 kt */
    CHECK(fabsf(current_speed_at(&day, 14 * 60 + 16)) < 0.05f);
    CHECK_NEAR(current_speed_at(&day, 17 * 60 + 40), -2.21, 0.02);
    CHECK_NEAR(current_speed_at(&day, 10 * 60 + 55), 1.52, 0.02);
    CHECK(current_speed_at(&day, 12 * 60) > 0.0f);      /* still flooding after 10:30 high water */
    CHECK(current_speed_at(&day, 16 * 60) < 0.0f);
    CHECK_NEAR(day.max_abs_kt, 2.21, 0.02);

    const current_event_t * slack = current_next_event(&day, 15 * 60, CURRENT_SLACK);
    CHECK(slack != NULL && slack->minute == 21 * 60 + 34);
    const current_event_t * flood = current_next_event(&day, 15 * 60, CURRENT_FLOOD);
    CHECK(flood != NULL && flood->minute == 23 * 60 + 26);
}

/* The whole curve, not just the events, against NOAA's own 6-minute
 * predictions for the same station and day */
static void test_noaa_current_shape(void)
{
    char * json = read_fixture("noaa-current-NYH1927-bin13-20261012-14.json");
    char * fine = read_fixture("noaa-current-NYH1927-bin13-20261013-6min.json");
    CHECK(json != NULL && fine != NULL);
    if(json == NULL || fine == NULL) return;
    current_day_t day;
    CHECK(noaa_parse_current(&day, json, 2026, 10, 13, NULL, 0));

    cJSON * root = cJSON_Parse(fine);
    const cJSON * cp = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "current_predictions"), "cp");
    const cJSON * e;
    double sum = 0.0, worst = 0.0;
    int n = 0;
    cJSON_ArrayForEach(e, cp) {
        int h, m;
        sscanf(cJSON_GetObjectItem(e, "Time")->valuestring + 11, "%d:%d", &h, &m);
        double d = fabs(current_speed_at(&day, (float)(h * 60 + m)) - cJSON_GetObjectItem(e, "Velocity_Major")->valuedouble);
        sum += d;
        if(d > worst) worst = d;
        n++;
    }
    cJSON_Delete(root);
    free(json);
    free(fine);

    CHECK(n == 240);
    CHECK(sum / n < 0.08); /* 0.04 kt today; the half-cosine shape was 0.21 */
    CHECK(worst < 0.25);   /* 0.17 kt today; the half-cosine shape was 0.50 */
}

static void test_noaa_urls(void)
{
    char url[512];
    CHECK(noaa_tide_url(url, sizeof(url), "8518750", 2026, 1, 1) > 0);
    CHECK(strstr(url, "station=8518750") != NULL);
    CHECK(strstr(url, "begin_date=20251231&end_date=20260102") != NULL); /* across the year */
    CHECK(strstr(url, "interval=hilo") != NULL && strstr(url, "time_zone=lst_ldt") != NULL);

    CHECK(noaa_current_url(url, sizeof(url), "NYH1927", 13, 2028, 2, 28) > 0);
    CHECK(strstr(url, "begin_date=20280227&end_date=20280229") != NULL); /* leap day */
    CHECK(strstr(url, "&bin=13") != NULL && strstr(url, "MAX_SLACK") != NULL);
    CHECK(noaa_current_url(url, sizeof(url), "NYH1927", 0, 2026, 10, 13) > 0);
    CHECK(strstr(url, "bin=") == NULL);

    CHECK(noaa_tide_url(url, sizeof(url), "", 2026, 10, 13) == 0);
    CHECK(noaa_tide_url(url, sizeof(url), "85&x=1", 2026, 10, 13) == 0);
    CHECK(noaa_tide_url(url, 40, "8518750", 2026, 10, 13) == 0);
}

static void test_weather_parse_errors(void)
{
    weather_t wx;
    char err[80];
    CHECK(!weather_parse_open_meteo(&wx, "{not json", err, sizeof(err)));
    CHECK(strcmp(err, "not valid JSON") == 0);

    CHECK(!weather_parse_open_meteo(&wx, "{\"error\":true,\"reason\":\"Latitude must be in range\"}", err, sizeof(err)));
    CHECK(strcmp(err, "no hourly data: Latitude must be in range") == 0);

    const char * celsius =
        "{\"hourly_units\":{\"temperature_2m\":\"\u00b0C\",\"wind_speed_10m\":\"km/h\"},"
        "\"hourly\":{\"time\":[\"2026-10-13T00:00\"]}}";
    CHECK(!weather_parse_open_meteo(&wx, celsius, err, sizeof(err)));
}

static void test_weather_url_and_compass(void)
{
    clock_config_t cfg;
    config_defaults(&cfg);
    char url[512];
    CHECK(weather_build_url(url, sizeof(url), &cfg) == 0); /* no location */

    config_parse(&cfg, "latitude=40.700554\nlongitude=-74.01417\ntz_name=America/New_York\n", NULL, 0);
    CHECK(weather_build_url(url, sizeof(url), &cfg) > 0);
    CHECK(strstr(url, "latitude=40.7006&longitude=-74.0142") != NULL);
    CHECK(strstr(url, "timezone=America%2FNew_York") != NULL);
    CHECK(strstr(url, "wind_speed_unit=kn") != NULL);
    CHECK(weather_build_url(url, 40, &cfg) == 0);

    CHECK(strcmp(weather_compass(0), "N") == 0);
    CHECK(strcmp(weather_compass(359), "N") == 0);
    CHECK(strcmp(weather_compass(158), "SSE") == 0);
    CHECK(strcmp(weather_compass(225), "SW") == 0);
    CHECK(strcmp(weather_compass(270), "W") == 0);
}

int main(void)
{
    test_config_defaults();
    test_config_parse();
    test_config_rejects_bad_values();
    test_config_screens();
    test_config_round_trip();
    test_tide_hilo_curve();
    test_tide_unbracketed_and_empty();
    test_nice_step();
    test_tide_fake();
    test_sky_battery();
    test_sky_polar();
    test_sky_names();
    test_weather_parse_real();
    test_weather_parse_storm();
    test_weather_outlook_scotia();
    test_weather_outlook_hourly_only();
    test_weather_parse_errors();
    test_noaa_tide();
    test_noaa_current();
    test_noaa_current_shape();
    test_noaa_urls();
    test_weather_url_and_compass();

    if(failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
