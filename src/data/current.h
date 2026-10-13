/**
 * @file current.h
 * Tidal current for one day, built from slack and maximum flood/ebb times
 * (NOAA's MAX_SLACK predictions), the way the tide is built from highs and lows.
 *
 * Speeds are signed: positive is flood, negative is ebb. Directions are the
 * set, in degrees true: where the water is going, as NOAA gives them.
 */
#ifndef CURRENT_H
#define CURRENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide.h" /* for the sample grid */

#define CURRENT_MAX_EVENTS 12 /* about 8 a day: four slacks, two floods, two ebbs */
#define CURRENT_SLACK_KT   0.1f /* slower than this reads as slack */

typedef enum {
    CURRENT_SLACK,
    CURRENT_FLOOD,
    CURRENT_EBB,
} current_kind_t;

typedef struct {
    float minute;   /* minutes since local midnight; may be <0 or >=1440 for neighbouring days */
    float speed_kt; /* signed: + flood, - ebb; about 0 at slack */
    current_kind_t kind;
} current_event_t;

typedef struct {
    uint16_t interval_min;
    uint16_t count;
    float speed_kt[TIDE_MAX_SAMPLES]; /* signed */
    float max_abs_kt;                 /* strongest flood or ebb today */

    /* Slacks and maxima that fall within this day, in time order */
    current_event_t events[CURRENT_MAX_EVENTS];
    uint8_t event_count;

    uint16_t flood_dir; /* degrees true the flood sets toward */
    uint16_t ebb_dir;
} current_day_t;

/**
 * Build the day's curve from events sorted by time. As with tides, include the
 * last event before midnight and the first after the next one.
 */
void current_build(current_day_t * day, const current_event_t * events, size_t count, uint16_t flood_dir,
                   uint16_t ebb_dir);

/** Signed speed at a minute of the day. */
float current_speed_at(const current_day_t * day, float minute);

/** Next event strictly after `minute`; pass a kind to look for just that kind, or -1 for any. */
const current_event_t * current_next_event(const current_day_t * day, float minute, int kind);

#endif /* CURRENT_H */
