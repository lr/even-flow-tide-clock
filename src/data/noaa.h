/**
 * @file noaa.h
 * NOAA CO-OPS tide and current predictions (https://api.tidesandcurrents.noaa.gov,
 * no API key).
 *
 * Each request covers the day before through the day after the one shown, so
 * the curve is bracketed at both midnights. Times come back in local standard
 * or daylight time (time_zone=lst_ldt) and are turned into minutes from the
 * shown day's midnight.
 */
#ifndef NOAA_H
#define NOAA_H

#include <stdbool.h>
#include <stddef.h>

#include "current.h"
#include "tide.h"

/** Highs and lows for `station` around year-month-day. Returns the length, or 0 on failure. */
size_t noaa_tide_url(char * buf, size_t len, const char * station, int year, int month, int day);

/**
 * Slack and maximum current for `station` around year-month-day. `bin` picks
 * the depth; 0 uses the station's default.
 */
size_t noaa_current_url(char * buf, size_t len, const char * station, int bin, int year, int month, int day);

/** Parse a highs-and-lows response into the curve for year-month-day. */
bool noaa_parse_tide(tide_day_t * out, const char * json, int year, int month, int day, char * err,
                     size_t err_len);

/** Parse a MAX_SLACK current response into the curve for year-month-day. */
bool noaa_parse_current(current_day_t * out, const char * json, int year, int month, int day, char * err,
                        size_t err_len);

#endif /* NOAA_H */
