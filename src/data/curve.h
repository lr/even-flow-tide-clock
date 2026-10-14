/**
 * @file curve.h
 * Smooth daily curves from turning points, shared by tides and currents.
 */
#ifndef CURVE_H
#define CURVE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    float minute; /* minutes since local midnight; may fall outside the day */
    float value;
} curve_point_t;

/**
 * Fill `count` samples, `interval` minutes apart from 00:00, joining the
 * points (sorted by time) with half cosines: the method behind printed tide
 * and current tables. Stretches not bracketed by points are held flat at the
 * nearest one. With no points, every sample is 0.
 */
void curve_fill_cosine(float * samples, uint16_t count, uint16_t interval, const curve_point_t * points,
                       size_t n);

/**
 * Like curve_fill_cosine(), for tidal current: points alternate between slack
 * (value 0) and maxima. Speed changes fastest at slack and is flat at a
 * maximum, so slack-to-max is a quarter sine and max-to-slack a quarter
 * cosine. (A half cosine is flat at both ends, which is right for tide highs
 * and lows but makes the current lag after every slack.)
 */
void curve_fill_current(float * samples, uint16_t count, uint16_t interval, const curve_point_t * points,
                        size_t n);

/**
 * A round interval for scale lines (1, 2, 2.5 or 5 times a power of ten) that
 * splits `range` into at most `max_lines` steps. Returns 0 for an empty range.
 */
float curve_nice_step(float range, int max_lines);

/** Linear interpolation into samples made by curve_fill_cosine(), clamped at the ends. */
float curve_sample_at(const float * samples, uint16_t count, uint16_t interval, float minute);

#endif /* CURVE_H */
