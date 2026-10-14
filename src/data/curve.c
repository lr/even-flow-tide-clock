/**
 * @file curve.c
 */
#include "curve.h"

#include <math.h>

void curve_fill_cosine(float * samples, uint16_t count, uint16_t interval, const curve_point_t * points,
                       size_t n)
{
    size_t seg = 0; /* index of the point at or before the current sample */
    for(uint16_t i = 0; i < count; i++) {
        float t = (float)(i * interval);
        if(n == 0) {
            samples[i] = 0.0f;
            continue;
        }

        while(seg + 1 < n && points[seg + 1].minute <= t) seg++;

        if(t <= points[0].minute) {
            samples[i] = points[0].value;
        }
        else if(seg + 1 >= n) {
            samples[i] = points[n - 1].value;
        }
        else {
            const curve_point_t * a = &points[seg];
            const curve_point_t * b = &points[seg + 1];
            float span = b->minute - a->minute;
            float frac = span > 0.0f ? (t - a->minute) / span : 1.0f;
            samples[i] = a->value + (b->value - a->value) * (1.0f - cosf((float)M_PI * frac)) * 0.5f;
        }
    }
}

void curve_fill_current(float * samples, uint16_t count, uint16_t interval, const curve_point_t * points,
                        size_t n)
{
    /* Start from the half-cosine curve, then reshape every slack-max segment */
    curve_fill_cosine(samples, count, interval, points, n);

    size_t seg = 0;
    for(uint16_t i = 0; i < count && n >= 2; i++) {
        float t = (float)(i * interval);
        while(seg + 1 < n && points[seg + 1].minute <= t) seg++;
        if(t <= points[0].minute || seg + 1 >= n) continue;

        const curve_point_t * a = &points[seg];
        const curve_point_t * b = &points[seg + 1];
        float span = b->minute - a->minute;
        float frac = span > 0.0f ? (t - a->minute) / span : 1.0f;
        float quarter = (float)M_PI * 0.5f * frac;
        if(a->value == 0.0f && b->value != 0.0f) samples[i] = b->value * sinf(quarter);       /* slack to max */
        else if(a->value != 0.0f && b->value == 0.0f) samples[i] = a->value * cosf(quarter);  /* max to slack */
    }
}

float curve_nice_step(float range, int max_lines)
{
    if(!(range > 0.0f) || max_lines < 1) return 0.0f;
    static const float mantissas[] = {1.0f, 2.0f, 2.5f, 5.0f};
    float base = powf(10.0f, floorf(log10f(range / (float)max_lines)));
    for(int scale = 0; scale < 2; scale++, base *= 10.0f) {
        for(size_t i = 0; i < sizeof(mantissas) / sizeof(mantissas[0]); i++) {
            float step = mantissas[i] * base;
            if(range / step <= (float)max_lines + 1e-4f) return step;
        }
    }
    return base;
}

float curve_sample_at(const float * samples, uint16_t count, uint16_t interval, float minute)
{
    if(count == 0) return 0.0f;

    float pos = minute / (float)interval;
    if(pos <= 0.0f) return samples[0];
    if(pos >= (float)(count - 1)) return samples[count - 1];

    int i = (int)pos;
    float frac = pos - (float)i;
    return samples[i] + (samples[i + 1] - samples[i]) * frac;
}
