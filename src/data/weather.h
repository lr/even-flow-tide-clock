/**
 * @file weather.h
 * Hourly weather from Open-Meteo (https://open-meteo.com, no API key).
 *
 * Values are kept in °F and knots whatever the display units, like tides are
 * kept in feet, so thresholds such as "gale" don't depend on settings. Times
 * are minutes since local midnight of the first day (1440+ is tomorrow).
 */
#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"

#define WEATHER_MAX_HOURS    48 /* today and tomorrow */
#define WEATHER_MAX_QUARTERS 16 /* 15-minute steps: four hours from the time of the request */
#define WEATHER_GALE_KT   34 /* gusts at or above this count as stormy */

/* Broad condition, one per icon (day and night variants where they exist) */
typedef enum {
    WX_CLEAR,
    WX_PARTLY_CLOUDY,
    WX_OVERCAST,
    WX_FOG,
    WX_DRIZZLE,
    WX_FREEZING,
    WX_LIGHT_RAIN,
    WX_RAIN,
    WX_LIGHT_SNOW,
    WX_SNOW,
    WX_THUNDER,
    WX_THUNDER_HAIL,
    WX_KIND_COUNT,
} wx_kind_t;

typedef struct {
    float minute;           /* start of the hour */
    float temp_f;
    float wind_kt;
    float gust_kt;
    float precip_in;        /* amount in the hour */
    uint16_t wind_from_deg; /* direction the wind blows from */
    uint8_t precip_prob;    /* percent */
    uint8_t code;           /* WMO weather code */
} weather_hour_t;

typedef struct {
    float minute; /* start of the 15 minutes */
    float precip_in;
    uint8_t code;
} weather_quarter_t;

typedef struct {
    weather_hour_t hours[WEATHER_MAX_HOURS];
    uint8_t hour_count;

    /* Short-range detail; empty if the response had none */
    weather_quarter_t quarters[WEATHER_MAX_QUARTERS];
    uint8_t quarter_count;

    struct {
        float minute; /* when this reading applies; NAN if unknown */
        float temp_f;
        float wind_kt;
        float gust_kt;
        uint16_t wind_from_deg;
        uint8_t code;
    } current;

    float today_high_f;
    float today_low_f;
    char first_date[11]; /* "YYYY-MM-DD" of hour 0, to spot stale data */
} weather_t;

/**
 * Build the Open-Meteo request for the configured place. Returns the length,
 * or 0 if there is no location or `len` is too small.
 */
size_t weather_build_url(char * buf, size_t len, const clock_config_t * cfg);

/** Parse an Open-Meteo forecast response. Returns false (and describes why in `err`) on bad input. */
bool weather_parse_open_meteo(weather_t * out, const char * json, char * err, size_t err_len);

wx_kind_t weather_kind(uint8_t wmo_code);

/**
 * Rain or snow expected: a 50% chance or more, or the forecast itself calls
 * for measurable precipitation (0.01 in) with a wet weather code. The model's
 * own call matters because chance can be low while it still forecasts drizzle.
 */
bool weather_hour_is_wet(const weather_hour_t * hour);

/** Thunder in the forecast, or gusts of gale force. */
bool weather_hour_is_stormy(const weather_hour_t * hour);

/** Rough badness for picking which hour represents a block: higher is worse. */
int weather_severity(const weather_hour_t * hour);

/** Plain name for a WMO code, e.g. "Light rain". */
const char * weather_code_name(uint8_t wmo_code);

/** Best guess at the WMO code right now, from the 15-minute data, "current" and the hour. */
uint8_t weather_code_now(const weather_t * wx, float now_minute);

/**
 * A short sentence about now and the next change, from the 15-minute data
 * where it reaches and hourly beyond (up to 12 hours):
 *   "Overcast, rain in 40 min"   "Light rain until 17:30"   "Clear"
 * `short_text` gets a tighter version for narrow space ("Rain in 40 min").
 */
void weather_outlook(const weather_t * wx, float now_minute, char * text, size_t text_len, char * short_text,
                     size_t short_len);

/** 16-point compass name for a direction in degrees, e.g. "SW". */
const char * weather_compass(uint16_t deg);

/** The hour containing `minute`, or NULL if outside the forecast. */
const weather_hour_t * weather_hour_at(const weather_t * wx, float minute);

#endif /* WEATHER_H */
