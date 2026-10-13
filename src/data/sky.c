/**
 * @file sky.c
 */
#include "sky.h"

#include <math.h>

#include "astronomy.h"

#define J2000_UNIX  946728000.0 /* 2000-01-01 12:00:00 UTC, Astronomy Engine's day zero */
#define SECS_PER_DAY 86400.0
#define MINUTES_DAY 1440.0f

/* Sun's upper limb at the horizon with standard refraction, for the polar fallback */
#define RISE_SET_ALTITUDE (-0.8333)

/*********************
 *  TIME CONVERSION
 *********************/
static double unix_to_ut(time_t t)
{
    return ((double)t - J2000_UNIX) / SECS_PER_DAY;
}

static time_t ut_to_unix(double ut)
{
    return (time_t)llround(ut * SECS_PER_DAY + J2000_UNIX);
}

/* Wall-clock minutes since local midnight, so DST days still line up with the dial */
static float ut_to_minute(double ut, time_t day_end)
{
    time_t t = ut_to_unix(ut);
    if(t >= day_end) return MINUTES_DAY;
    struct tm lt;
    localtime_r(&t, &lt);
    return (float)(lt.tm_hour * 60 + lt.tm_min) + (float)lt.tm_sec / 60.0f;
}

static time_t local_midnight(int year, int month, int day)
{
    struct tm tm = {.tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_isdst = -1};
    return mktime(&tm); /* normalises day overflow, so day + 1 is safe */
}

/*********************
 *  SPANS
 *********************/
typedef struct {
    astro_body_t body;
    astro_observer_t observer;
    bool rise_set;   /* true: rise/set (upper limb, refraction); false: centre crosses `altitude` */
    double altitude;
} crossing_t;

static bool search(const crossing_t * c, astro_direction_t dir, double from_ut, double limit_days, double * ut)
{
    astro_time_t start = Astronomy_TimeFromDays(from_ut);
    astro_search_result_t r = c->rise_set
                                  ? Astronomy_SearchRiseSet(c->body, c->observer, dir, start, limit_days)
                                  : Astronomy_SearchAltitude(c->body, c->observer, dir, start, limit_days, c->altitude);
    if(r.status != ASTRO_SUCCESS) return false;
    *ut = r.time.ut;
    return true;
}

static double altitude_at(const crossing_t * c, double ut)
{
    astro_time_t t = Astronomy_TimeFromDays(ut);
    astro_equatorial_t eq = Astronomy_Equator(c->body, &t, c->observer, EQUATOR_OF_DATE, ABERRATION);
    astro_horizon_t hor = Astronomy_Horizon(&t, c->observer, eq.ra, eq.dec,
                                            c->rise_set ? REFRACTION_NORMAL : REFRACTION_NONE);
    return hor.altitude;
}

static void add_span(sky_spans_t * spans, float start, float end)
{
    if(spans->count < SKY_MAX_SPANS && end > start) {
        spans->span[spans->count].start = start;
        spans->span[spans->count].end = end;
        spans->count++;
    }
}

/**
 * Find when the body is above the limit between day_start and day_end.
 * Optionally reports the first rising and setting within the day (NAN if none).
 */
static void find_spans(const crossing_t * c, time_t day_start, time_t day_end, sky_spans_t * spans,
                       float * first_rise, float * first_set)
{
    double ut0 = unix_to_ut(day_start);
    double ut1 = unix_to_ut(day_end);

    spans->count = 0;
    if(first_rise) *first_rise = NAN;
    if(first_set) *first_set = NAN;

    /* Up at midnight if the next crossing is a setting. Look two days ahead so
     * a short night or day can't hide the answer. */
    double next_rise, next_set;
    bool has_rise = search(c, DIRECTION_RISE, ut0, 2.0, &next_rise);
    bool has_set = search(c, DIRECTION_SET, ut0, 2.0, &next_set);
    bool up;
    if(has_rise || has_set) {
        up = has_set && (!has_rise || next_set < next_rise);
    }
    else {
        /* Midnight sun or polar night */
        up = altitude_at(c, ut0) > (c->rise_set ? RISE_SET_ALTITUDE : c->altitude);
    }

    double cursor = ut0;
    float span_start = 0.0f;

    /* Alternate set/rise searches across the day; each crossing flips `up` */
    for(int i = 0; i < 2 * SKY_MAX_SPANS + 2; i++) {
        double limit = ut1 - cursor;
        double ut;
        if(limit <= 0.0) break;

        if(up) {
            if(!search(c, DIRECTION_SET, cursor, limit, &ut)) break;
            float m = ut_to_minute(ut, day_end);
            add_span(spans, span_start, m);
            if(first_set && isnan(*first_set)) *first_set = m;
            up = false;
        }
        else {
            if(!search(c, DIRECTION_RISE, cursor, limit, &ut)) break;
            span_start = ut_to_minute(ut, day_end);
            if(first_rise && isnan(*first_rise)) *first_rise = span_start;
            up = true;
        }
        cursor = ut;
    }

    if(up) add_span(spans, span_start, MINUTES_DAY);
}

/*********************
 *  PUBLIC API
 *********************/
bool sky_compute_day(sky_day_t * out, double latitude, double longitude, int year, int month, int day)
{
    time_t day_start = local_midnight(year, month, day);
    time_t day_end = local_midnight(year, month, day + 1);
    if(day_start == (time_t)-1 || day_end == (time_t)-1) return false;

    astro_observer_t obs = Astronomy_MakeObserver(latitude, longitude, 0.0);

    crossing_t sun = {.body = BODY_SUN, .observer = obs, .rise_set = true};
    find_spans(&sun, day_start, day_end, &out->day, &out->sunrise, &out->sunset);

    sun.rise_set = false;
    sun.altitude = -6.0;
    find_spans(&sun, day_start, day_end, &out->civil, NULL, NULL);
    sun.altitude = -12.0;
    find_spans(&sun, day_start, day_end, &out->nautical, NULL, NULL);
    sun.altitude = -18.0;
    find_spans(&sun, day_start, day_end, &out->astro, NULL, NULL);

    astro_hour_angle_t noon = Astronomy_SearchHourAngle(BODY_SUN, obs, 0.0,
                                                        Astronomy_TimeFromDays(unix_to_ut(day_start)));
    out->solar_noon = (noon.status == ASTRO_SUCCESS && ut_to_unix(noon.time.ut) < day_end)
                          ? ut_to_minute(noon.time.ut, day_end)
                          : NAN;

    crossing_t moon = {.body = BODY_MOON, .observer = obs, .rise_set = true};
    find_spans(&moon, day_start, day_end, &out->moon_up, &out->moonrise, &out->moonset);

    return true;
}

bool sky_compute_moon(sky_moon_t * out, time_t now)
{
    astro_time_t t = Astronomy_TimeFromDays(unix_to_ut(now));

    astro_angle_result_t phase = Astronomy_MoonPhase(t);
    astro_illum_t illum = Astronomy_Illumination(BODY_MOON, t);
    if(phase.status != ASTRO_SUCCESS || illum.status != ASTRO_SUCCESS) return false;
    out->phase_deg = (float)phase.angle;
    out->illumination = (float)illum.phase_fraction;

    astro_moon_quarter_t mq = Astronomy_SearchMoonQuarter(t);
    for(int i = 0; i < 2; i++) {
        if(mq.status != ASTRO_SUCCESS) return false;
        out->next[i].quarter = (moon_quarter_t)mq.quarter;
        out->next[i].when = ut_to_unix(mq.time.ut);
        mq = Astronomy_NextMoonQuarter(mq);
    }
    return true;
}

const char * sky_phase_name(float phase_deg)
{
    /* Eight 45-degree bins centred on the quarters */
    static const char * const names[] = {
        "New moon",       "Waxing crescent", "First quarter", "Waxing gibbous",
        "Full moon",      "Waning gibbous",  "Last quarter",  "Waning crescent",
    };
    float p = fmodf(phase_deg + 22.5f, 360.0f);
    if(p < 0.0f) p += 360.0f;
    return names[(int)(p / 45.0f) % 8];
}

const char * sky_quarter_name(moon_quarter_t quarter)
{
    switch(quarter) {
        case MOON_NEW: return "New moon";
        case MOON_FIRST_QUARTER: return "First quarter";
        case MOON_FULL: return "Full moon";
        case MOON_LAST_QUARTER: return "Last quarter";
    }
    return "";
}
