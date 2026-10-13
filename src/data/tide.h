/**
 * @file tide.h
 * Tide data model shared by the UI and the data sources (fake now, NOAA later).
 *
 * Times are minutes since local midnight of the displayed day, so the UI never
 * has to deal with time zones. The fetch layer is responsible for converting
 * NOAA timestamps (requested with time_zone=lst_ldt) into this form.
 *
 * The curve is always built from highs and lows alone. Most NOAA stations are
 * subordinate stations that only publish highs and lows, so this one path
 * works for every station.
 */
#ifndef TIDE_H
#define TIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TIDE_SAMPLE_MIN  6                                   /* curve resolution */
#define TIDE_MAX_SAMPLES (24 * 60 / TIDE_SAMPLE_MIN + 1)      /* 00:00 through 24:00 inclusive */
#define TIDE_MAX_EVENTS  8   /* semi-diurnal tides give ~4 per day; leave headroom */

typedef struct {
    float minute;    /* minutes since local midnight; may be <0 or >=1440 for neighbouring days */
    float height_ft; /* relative to station datum (MLLW) */
    bool high;
} tide_event_t;

typedef struct {
    uint16_t interval_min; /* spacing between samples; first sample is at 00:00 */
    uint16_t count;        /* samples cover [0, (count-1)*interval_min] */
    float height_ft[TIDE_MAX_SAMPLES];
    float min_ft;
    float max_ft;

    /* Highs and lows that fall within this day, in time order */
    tide_event_t events[TIDE_MAX_EVENTS];
    uint8_t event_count;
} tide_day_t;

/**
 * Build a day's curve from highs and lows, joining each pair with a half
 * cosine (the method used for printed tide tables).
 *
 * `events` must be sorted by time. Include the last turn before midnight and
 * the first one after the next midnight (i.e. fetch yesterday through
 * tomorrow) so the whole day is bracketed. Any stretch that isn't bracketed is
 * held flat at the nearest known height.
 */
void tide_build_from_hilo(tide_day_t * day, const tide_event_t * events, size_t count);

/** Height at an arbitrary minute of the day, linearly interpolated (clamped at the ends). */
float tide_height_at(const tide_day_t * day, float minute);

/** First event strictly after `minute`, or NULL if none remain today. */
const tide_event_t * tide_next_event(const tide_day_t * day, float minute);

/**
 * Fill `day` with a plausible mixed semi-diurnal tide. Highs and lows are taken
 * from a harmonic model and then fed through tide_build_from_hilo(), so the
 * simulator exercises the same path real data will. `seed` picks the day;
 * mix in a station hash so different stations differ.
 */
void tide_fake_generate(tide_day_t * day, uint32_t seed);

#endif /* TIDE_H */
