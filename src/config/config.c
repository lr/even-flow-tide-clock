/**
 * @file config.c
 */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 128

void config_defaults(clock_config_t * cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->latitude = NAN;
    cfg->longitude = NAN;
    snprintf(cfg->tz_posix, sizeof(cfg->tz_posix), "UTC0");
    cfg->top_hour = 12;
    cfg->units = UNITS_FEET;
    static const screen_t default_order[SCREEN_COUNT] = {SCREEN_TIDE, SCREEN_CURRENT, SCREEN_SUN, SCREEN_MOON,
                                                         SCREEN_WEATHER};
    memcpy(cfg->screens, default_order, sizeof(default_order));
    cfg->screen_count = SCREEN_COUNT;
}

static const char * const screen_names[SCREEN_COUNT] = {
    [SCREEN_TIDE] = "tide",
    [SCREEN_SUN] = "sun",
    [SCREEN_MOON] = "moon",
    [SCREEN_WEATHER] = "weather",
    [SCREEN_CURRENT] = "current",
};

const char * config_screen_name(screen_t screen)
{
    return screen < SCREEN_COUNT ? screen_names[screen] : "";
}

bool config_has_location(const clock_config_t * cfg)
{
    return !isnan(cfg->latitude) && !isnan(cfg->longitude);
}

bool config_has_station(const clock_config_t * cfg)
{
    return cfg->station_id[0] != '\0';
}

bool config_has_current_station(const clock_config_t * cfg)
{
    return cfg->current_station_id[0] != '\0';
}

/*********************
 *  PARSING
 *********************/
static char * trim(char * s)
{
    while(*s && isspace((unsigned char)*s)) s++;
    char * end = s + strlen(s);
    while(end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

/* Printable ASCII or UTF-8 continuation bytes; no control characters */
static bool is_clean_text(const char * s)
{
    for(; *s; s++) {
        if((unsigned char)*s < 0x20 || *s == 0x7f) return false;
    }
    return true;
}

static bool set_text(char * dst, size_t size, const char * value)
{
    if(strlen(value) >= size || !is_clean_text(value)) return false;
    memcpy(dst, value, strlen(value) + 1);
    return true;
}

static bool parse_double(const char * value, double lo, double hi, double * out)
{
    if(*value == '\0') {
        *out = NAN;
        return true;
    }
    char * end;
    errno = 0;
    double v = strtod(value, &end);
    if(errno != 0 || *end != '\0' || !isfinite(v) || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static bool set_station_id(char * dst, size_t size, const char * value)
{
    for(const char * p = value; *p; p++) {
        if(!isalnum((unsigned char)*p)) return false;
    }
    return set_text(dst, size, value);
}

static bool set_tz_posix(char * dst, size_t size, const char * value)
{
    if(*value == '\0') return false;
    for(const char * p = value; *p; p++) {
        if(isspace((unsigned char)*p)) return false;
    }
    return set_text(dst, size, value);
}

/* "tide, weather, sun": each name once, at least one */
static bool set_screens(clock_config_t * cfg, const char * value)
{
    screen_t order[SCREEN_COUNT];
    int count = 0;
    bool seen[SCREEN_COUNT] = {false};

    const char * p = value;
    while(*p) {
        while(*p == ' ' || *p == ',') p++;
        if(*p == '\0') break;
        const char * start = p;
        while(*p && *p != ',' && *p != ' ') p++;
        size_t len = (size_t)(p - start);

        int found = -1;
        for(int s = 0; s < SCREEN_COUNT; s++) {
            if(strlen(screen_names[s]) == len && strncmp(start, screen_names[s], len) == 0) found = s;
        }
        if(found < 0 || seen[found]) return false;
        seen[found] = true;
        order[count++] = (screen_t)found;
    }
    if(count == 0) return false;

    memcpy(cfg->screens, order, sizeof(order[0]) * (size_t)count);
    cfg->screen_count = count;
    return true;
}

static bool apply(clock_config_t * cfg, const char * key, const char * value)
{
    if(strcmp(key, "screens") == 0) return set_screens(cfg, value);
    if(strcmp(key, "place_name") == 0) return set_text(cfg->place_name, sizeof(cfg->place_name), value);
    if(strcmp(key, "latitude") == 0) return parse_double(value, -90.0, 90.0, &cfg->latitude);
    if(strcmp(key, "longitude") == 0) return parse_double(value, -180.0, 180.0, &cfg->longitude);
    if(strcmp(key, "tz_name") == 0) return set_text(cfg->tz_name, sizeof(cfg->tz_name), value);
    if(strcmp(key, "tz_posix") == 0) return set_tz_posix(cfg->tz_posix, sizeof(cfg->tz_posix), value);
    if(strcmp(key, "station_id") == 0) return set_station_id(cfg->station_id, sizeof(cfg->station_id), value);
    if(strcmp(key, "station_name") == 0) return set_text(cfg->station_name, sizeof(cfg->station_name), value);
    if(strcmp(key, "current_station_id") == 0) {
        return set_station_id(cfg->current_station_id, sizeof(cfg->current_station_id), value);
    }
    if(strcmp(key, "current_station_name") == 0) {
        return set_text(cfg->current_station_name, sizeof(cfg->current_station_name), value);
    }
    if(strcmp(key, "current_bin") == 0) {
        char * end;
        long v = strtol(value, &end, 10);
        if(*value == '\0' || *end != '\0' || v < 0 || v > 99) return false;
        cfg->current_bin = (int)v;
        return true;
    }

    if(strcmp(key, "top_hour") == 0) {
        char * end;
        long v = strtol(value, &end, 10);
        if(*value == '\0' || *end != '\0' || v < 0 || v > 23) return false;
        cfg->top_hour = (int)v;
        return true;
    }
    if(strcmp(key, "temp_unit") == 0) {
        if(strcmp(value, "f") == 0) cfg->temp_unit = TEMP_FAHRENHEIT;
        else if(strcmp(value, "c") == 0) cfg->temp_unit = TEMP_CELSIUS;
        else return false;
        return true;
    }
    if(strcmp(key, "wind_unit") == 0) {
        if(strcmp(value, "kn") == 0) cfg->wind_unit = WIND_KNOTS;
        else if(strcmp(value, "mph") == 0) cfg->wind_unit = WIND_MPH;
        else if(strcmp(value, "kmh") == 0) cfg->wind_unit = WIND_KMH;
        else return false;
        return true;
    }
    if(strcmp(key, "units") == 0) {
        if(strcmp(value, "ft") == 0) cfg->units = UNITS_FEET;
        else if(strcmp(value, "m") == 0) cfg->units = UNITS_METRES;
        else return false;
        return true;
    }

    return true; /* unknown key: ignore */
}

static void report(int * errors, char * err, size_t err_len, const char * fmt, ...)
{
    if(*errors == 0 && err != NULL && err_len > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
    (*errors)++;
}

int config_parse(clock_config_t * cfg, const char * text, char * err, size_t err_len)
{
    int errors = 0;
    int line_no = 0;
    if(err != NULL && err_len > 0) err[0] = '\0';

    while(*text) {
        line_no++;
        const char * eol = strchr(text, '\n');
        size_t len = eol ? (size_t)(eol - text) : strlen(text);

        if(len >= LINE_MAX_LEN) {
            report(&errors, err, err_len, "line %d: too long", line_no);
        }
        else {
            char line[LINE_MAX_LEN];
            memcpy(line, text, len);
            line[len] = '\0';

            char * s = trim(line);
            if(*s != '\0' && *s != '#') {
                char * eq = strchr(s, '=');
                if(eq == NULL) {
                    report(&errors, err, err_len, "line %d: expected key=value", line_no);
                }
                else {
                    *eq = '\0';
                    char * key = trim(s);
                    char * value = trim(eq + 1);
                    if(!apply(cfg, key, value)) {
                        report(&errors, err, err_len, "line %d: bad value for %s", line_no, key);
                    }
                }
            }
        }

        text += len;
        if(*text == '\n') text++;
    }

    return errors;
}

/*********************
 *  FORMATTING
 *********************/
size_t config_format(const clock_config_t * cfg, char * buf, size_t len)
{
    /* A stray newline in a field set from code would corrupt the file */
    const char * fields[] = {cfg->place_name,  cfg->tz_name,           cfg->tz_posix,           cfg->station_id,
                             cfg->station_name, cfg->current_station_id, cfg->current_station_name};
    for(size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if(!is_clean_text(fields[i])) return 0;
    }

    char screens[64] = "";
    size_t used = 0;
    for(int i = 0; i < cfg->screen_count && i < SCREEN_COUNT; i++) {
        used += (size_t)snprintf(screens + used, sizeof(screens) - used, "%s%s", i ? "," : "",
                                 config_screen_name(cfg->screens[i]));
    }

    char lat[24] = "";
    char lon[24] = "";
    if(config_has_location(cfg)) {
        snprintf(lat, sizeof(lat), "%.6f", cfg->latitude);
        snprintf(lon, sizeof(lon), "%.6f", cfg->longitude);
    }

    int n = snprintf(buf, len,
                     "place_name=%s\n"
                     "latitude=%s\n"
                     "longitude=%s\n"
                     "tz_name=%s\n"
                     "tz_posix=%s\n"
                     "station_id=%s\n"
                     "station_name=%s\n"
                     "current_station_id=%s\n"
                     "current_station_name=%s\n"
                     "current_bin=%d\n"
                     "top_hour=%d\n"
                     "units=%s\n"
                     "temp_unit=%s\n"
                     "wind_unit=%s\n"
                     "screens=%s\n",
                     cfg->place_name, lat, lon, cfg->tz_name, cfg->tz_posix,
                     cfg->station_id, cfg->station_name, cfg->current_station_id, cfg->current_station_name,
                     cfg->current_bin, cfg->top_hour,
                     cfg->units == UNITS_METRES ? "m" : "ft",
                     cfg->temp_unit == TEMP_CELSIUS ? "c" : "f",
                     cfg->wind_unit == WIND_MPH ? "mph" : cfg->wind_unit == WIND_KMH ? "kmh" : "kn",
                     screens);

    if(n < 0 || (size_t)n >= len) return 0;
    return (size_t)n;
}
