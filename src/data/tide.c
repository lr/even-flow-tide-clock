/**
 * @file tide.c
 */
#include "tide.h"

#include "curve.h"

#include <math.h>

#define TWO_PI      6.28318530718
#define MINUTES_DAY 1440

void tide_build_from_hilo(tide_day_t * day, const tide_event_t * events, size_t count)
{
    day->interval_min = TIDE_SAMPLE_MIN;
    day->count = TIDE_MAX_SAMPLES;
    day->event_count = 0;

    if(count == 0) {
        day->count = 0;
        day->min_ft = day->max_ft = 0.0f;
        return;
    }

    curve_point_t points[32];
    size_t n = 0;
    for(size_t i = 0; i < count && n < sizeof(points) / sizeof(points[0]); i++) {
        points[n].minute = events[i].minute;
        points[n].value = events[i].height_ft;
        n++;
    }
    curve_fill_cosine(day->height_ft, day->count, day->interval_min, points, n);

    day->min_ft = day->max_ft = day->height_ft[0];
    for(uint16_t i = 1; i < day->count; i++) {
        if(day->height_ft[i] < day->min_ft) day->min_ft = day->height_ft[i];
        if(day->height_ft[i] > day->max_ft) day->max_ft = day->height_ft[i];
    }

    for(size_t i = 0; i < count && day->event_count < TIDE_MAX_EVENTS; i++) {
        if(events[i].minute >= 0.0f && events[i].minute < MINUTES_DAY) {
            day->events[day->event_count++] = events[i];
        }
    }
}

float tide_height_at(const tide_day_t * day, float minute)
{
    return curve_sample_at(day->height_ft, day->count, day->interval_min, minute);
}

const tide_event_t * tide_next_event(const tide_day_t * day, float minute)
{
    for(uint8_t i = 0; i < day->event_count; i++) {
        if(day->events[i].minute > minute) return &day->events[i];
    }
    return NULL;
}

/*********************
 *  FAKE DATA
 *********************/
static double harmonic_ft(double hours)
{
    /* Four main tidal constituents: period in hours, amplitude in feet.
     * Amplitudes are loosely modelled on a US west coast mixed tide. */
    static const struct {
        double period_h;
        double amp_ft;
    } constituents[] = {
        {12.4206, 1.90}, /* M2 principal lunar */
        {12.0000, 0.50}, /* S2 principal solar */
        {23.9345, 1.15}, /* K1 luni-solar diurnal */
        {25.8193, 0.72}, /* O1 principal lunar diurnal */
    };

    double y = 3.2; /* mean level above MLLW */
    for(size_t c = 0; c < sizeof(constituents) / sizeof(constituents[0]); c++) {
        y += constituents[c].amp_ft * cos(TWO_PI * hours / constituents[c].period_h + (double)c);
    }
    return y;
}

void tide_fake_generate(tide_day_t * day, uint32_t seed)
{
    /* Double precision: seed may be days-since-epoch, which floats can't resolve in hours */
    double day_h = (double)seed * 24.0;

    /* Scan yesterday through tomorrow at 1-minute steps so today is bracketed */
    tide_event_t events[24];
    size_t count = 0;

    double prev2 = harmonic_ft(day_h + (-MINUTES_DAY - 2) / 60.0);
    double prev1 = harmonic_ft(day_h + (-MINUTES_DAY - 1) / 60.0);
    for(int m = -MINUTES_DAY; m <= 2 * MINUTES_DAY && count < sizeof(events) / sizeof(events[0]); m++) {
        double cur = harmonic_ft(day_h + m / 60.0);
        bool high = prev1 > prev2 && prev1 >= cur;
        bool low = prev1 < prev2 && prev1 <= cur;
        if(high || low) {
            events[count].minute = (float)(m - 1);
            events[count].height_ft = (float)prev1;
            events[count].high = high;
            count++;
        }
        prev2 = prev1;
        prev1 = cur;
    }

    tide_build_from_hilo(day, events, count);
}
