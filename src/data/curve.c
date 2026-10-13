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
