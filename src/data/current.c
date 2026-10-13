/**
 * @file current.c
 */
#include "current.h"

#include <math.h>

#include "curve.h"

void current_build(current_day_t * day, const current_event_t * events, size_t count, uint16_t flood_dir,
                   uint16_t ebb_dir)
{
    day->interval_min = TIDE_SAMPLE_MIN;
    day->count = count ? TIDE_MAX_SAMPLES : 0;
    day->event_count = 0;
    day->flood_dir = flood_dir;
    day->ebb_dir = ebb_dir;
    day->max_abs_kt = 0.0f;

    curve_point_t points[32];
    size_t n = 0;
    for(size_t i = 0; i < count && n < sizeof(points) / sizeof(points[0]); i++) {
        points[n].minute = events[i].minute;
        /* NOAA reports slack as e.g. -0.01; pin it to zero so the curve crosses cleanly */
        points[n].value = events[i].kind == CURRENT_SLACK ? 0.0f : events[i].speed_kt;
        n++;
    }
    curve_fill_current(day->speed_kt, day->count, day->interval_min, points, n);

    for(uint16_t i = 0; i < day->count; i++) {
        if(fabsf(day->speed_kt[i]) > day->max_abs_kt) day->max_abs_kt = fabsf(day->speed_kt[i]);
    }

    for(size_t i = 0; i < count && day->event_count < CURRENT_MAX_EVENTS; i++) {
        if(events[i].minute >= 0.0f && events[i].minute < 1440.0f) {
            day->events[day->event_count++] = events[i];
        }
    }
}

float current_speed_at(const current_day_t * day, float minute)
{
    return curve_sample_at(day->speed_kt, day->count, day->interval_min, minute);
}

const current_event_t * current_next_event(const current_day_t * day, float minute, int kind)
{
    for(uint8_t i = 0; i < day->event_count; i++) {
        const current_event_t * ev = &day->events[i];
        if(ev->minute > minute && (kind < 0 || (int)ev->kind == kind)) return ev;
    }
    return NULL;
}
