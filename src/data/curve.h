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

/** Linear interpolation into samples made by curve_fill_cosine(), clamped at the ends. */
float curve_sample_at(const float * samples, uint16_t count, uint16_t interval, float minute);

#endif /* CURVE_H */
