/**
 * @file sky.h
 * Sun and moon for a place and day, computed locally (Astronomy Engine).
 *
 * Like the tide model, times are minutes since local midnight (wall clock), so
 * the dial can draw them directly. Conversion uses the C library's
 * mktime/localtime_r, so the process time zone (TZ) must already be set to the
 * clock's zone. The simulator and the firmware both do that from the config.
 *
 * Double-precision maths is slow on the ESP32-S3 (no hardware doubles), so
 * sky_compute_day() is meant to run once a day and sky_compute_moon() every
 * few minutes at most.
 */
#ifndef SKY_H
#define SKY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define SKY_MAX_SPANS 3 /* a body can rise, set and rise again within a day */

typedef struct {
    float start; /* minutes since local midnight; 0 if already up at midnight */
    float end;   /* 1440 if still up at the end of the day */
} sky_span_t;

typedef struct {
    sky_span_t span[SKY_MAX_SPANS];
    uint8_t count;
} sky_spans_t;

typedef struct {
    /* Sun above the horizon, and above each twilight limit. Each set contains
     * the one before it: day within civil within nautical within astronomical. */
    sky_spans_t day;      /* sunrise to sunset (upper limb, with refraction) */
    sky_spans_t civil;    /* centre above -6 degrees */
    sky_spans_t nautical; /* centre above -12 degrees */
    sky_spans_t astro;    /* centre above -18 degrees */

    float sunrise;    /* NAN when the sun doesn't rise today (polar night, or day) */
    float sunset;     /* NAN when it doesn't set today */
    float solar_noon; /* sun due south (north in the southern hemisphere) */

    sky_spans_t moon_up;
    float moonrise; /* NAN when the moon doesn't rise today, which happens about monthly */
    float moonset;
} sky_day_t;

typedef enum {
    MOON_NEW,
    MOON_FIRST_QUARTER,
    MOON_FULL,
    MOON_LAST_QUARTER,
} moon_quarter_t;

typedef struct {
    float phase_deg;    /* 0 new, 90 first quarter, 180 full, 270 last quarter */
    float illumination; /* fraction of the disc lit, 0-1 */
    struct {
        moon_quarter_t quarter;
        time_t when; /* UTC epoch seconds */
    } next[2];       /* the next two quarters after `now` */
} sky_moon_t;

/** Compute the sun and moon events for the local date year-month-day. Returns false on failure. */
bool sky_compute_day(sky_day_t * out, double latitude, double longitude, int year, int month, int day);

/** Phase and upcoming quarters at `now`. Returns false on failure. */
bool sky_compute_moon(sky_moon_t * out, time_t now);

/** Common name for a phase angle, e.g. "Waxing gibbous". */
const char * sky_phase_name(float phase_deg);

/** Name of a quarter, e.g. "Full moon". */
const char * sky_quarter_name(moon_quarter_t quarter);

#endif /* SKY_H */
