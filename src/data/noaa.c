/**
 * @file noaa.c
 */
#include "noaa.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define NOAA_BASE "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter"
#define NOAA_COMMON "&units=english&time_zone=lst_ldt&format=json&application=even_flow_clock"

/*********************
 *  DATES
 *********************/
/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, int * y, int * m, int * d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

/* "&begin_date=YYYYMMDD&end_date=YYYYMMDD" for the day before through the day after */
static void date_range(char * buf, size_t len, int year, int month, int day)
{
    long z = days_from_civil(year, month, day);
    int ys, ms, ds, ye, me, de; /* start and end dates */
    civil_from_days(z - 1, &ys, &ms, &ds);
    civil_from_days(z + 1, &ye, &me, &de);
    snprintf(buf, len, "&begin_date=%04d%02d%02d&end_date=%04d%02d%02d", ys, ms, ds, ye, me, de);
}

/* "YYYY-MM-DD HH:MM" -> minutes from the reference day's midnight */
static bool parse_time(const char * s, long ref_days, float * minute)
{
    int y, mo, d, h, mi;
    if(sscanf(s, "%4d-%2d-%2d %2d:%2d", &y, &mo, &d, &h, &mi) != 5) return false;
    *minute = (float)((days_from_civil(y, mo, d) - ref_days) * 1440 + h * 60 + mi);
    return true;
}

static bool station_ok(const char * station)
{
    if(station == NULL || station[0] == '\0' || strlen(station) > 16) return false;
    for(const char * p = station; *p; p++) {
        if(!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) return false;
    }
    return true;
}

/*********************
 *  REQUESTS
 *********************/
size_t noaa_tide_url(char * buf, size_t len, const char * station, int year, int month, int day)
{
    if(!station_ok(station)) return 0;
    char range[48];
    date_range(range, sizeof(range), year, month, day);
    int n = snprintf(buf, len, NOAA_BASE "?station=%s&product=predictions&datum=MLLW&interval=hilo%s" NOAA_COMMON,
                     station, range);
    return n > 0 && (size_t)n < len ? (size_t)n : 0;
}

size_t noaa_current_url(char * buf, size_t len, const char * station, int bin, int year, int month, int day)
{
    if(!station_ok(station)) return 0;
    char range[48], bin_arg[16] = "";
    date_range(range, sizeof(range), year, month, day);
    if(bin > 0) snprintf(bin_arg, sizeof(bin_arg), "&bin=%d", bin);
    int n = snprintf(buf, len, NOAA_BASE "?station=%s&product=currents_predictions&interval=MAX_SLACK%s%s" NOAA_COMMON,
                     station, bin_arg, range);
    return n > 0 && (size_t)n < len ? (size_t)n : 0;
}

/*********************
 *  PARSING
 *********************/
static void fail(char * err, size_t err_len, const char * fmt, ...)
{
    if(err == NULL || err_len == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

/* NOAA sends some numbers as strings ("0.451") and some as numbers */
static bool get_number(const cJSON * obj, const char * key, double * out)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsNumber(item)) {
        *out = item->valuedouble;
        return true;
    }
    if(cJSON_IsString(item) && item->valuestring[0] != '\0') {
        char * end;
        *out = strtod(item->valuestring, &end);
        return *end == '\0';
    }
    return false;
}

static const char * get_string(const cJSON * obj, const char * key)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* NOAA errors look like {"error": {"message": "..."}} */
static bool report_api_error(const cJSON * root, char * err, size_t err_len)
{
    const cJSON * error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if(error == NULL) return false;
    const char * msg = get_string(error, "message");
    while(msg && *msg == ' ') msg++;
    fail(err, err_len, "NOAA: %s", msg ? msg : "unknown error");
    return true;
}

bool noaa_parse_tide(tide_day_t * out, const char * json, int year, int month, int day, char * err, size_t err_len)
{
    if(err && err_len) err[0] = '\0';
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) {
        fail(err, err_len, "not valid JSON");
        return false;
    }

    bool ok = false;
    long ref = days_from_civil(year, month, day);
    tide_event_t events[24];
    size_t count = 0;

    const cJSON * list = cJSON_GetObjectItemCaseSensitive(root, "predictions");
    if(!cJSON_IsArray(list)) {
        if(!report_api_error(root, err, err_len)) fail(err, err_len, "no predictions");
        goto done;
    }

    const cJSON * p;
    cJSON_ArrayForEach(p, list) {
        if(count >= sizeof(events) / sizeof(events[0])) break;
        const char * t = get_string(p, "t");
        const char * type = get_string(p, "type");
        double v;
        tide_event_t * ev = &events[count];
        if(t == NULL || type == NULL || !get_number(p, "v", &v) || !parse_time(t, ref, &ev->minute)) {
            fail(err, err_len, "bad prediction %zu", count);
            goto done;
        }
        ev->height_ft = (float)v;
        ev->high = type[0] == 'H';
        count++;
    }
    if(count == 0) {
        fail(err, err_len, "no predictions");
        goto done;
    }

    tide_build_from_hilo(out, events, count);
    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}

bool noaa_parse_current(current_day_t * out, const char * json, int year, int month, int day, char * err,
                        size_t err_len)
{
    if(err && err_len) err[0] = '\0';
    cJSON * root = cJSON_Parse(json);
    if(root == NULL) {
        fail(err, err_len, "not valid JSON");
        return false;
    }

    bool ok = false;
    long ref = days_from_civil(year, month, day);
    current_event_t events[32];
    size_t count = 0;
    double flood_dir = 0.0, ebb_dir = 0.0;

    const cJSON * cp = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "current_predictions"),
                                                        "cp");
    if(!cJSON_IsArray(cp)) {
        if(!report_api_error(root, err, err_len)) fail(err, err_len, "no current predictions");
        goto done;
    }

    const cJSON * p;
    cJSON_ArrayForEach(p, cp) {
        if(count >= sizeof(events) / sizeof(events[0])) break;
        const char * t = get_string(p, "Time");
        const char * type = get_string(p, "Type");
        double v;
        current_event_t * ev = &events[count];
        if(t == NULL || type == NULL || !get_number(p, "Velocity_Major", &v) || !parse_time(t, ref, &ev->minute)) {
            fail(err, err_len, "bad prediction %zu", count);
            goto done;
        }
        ev->speed_kt = (float)v;
        ev->kind = strcmp(type, "flood") == 0 ? CURRENT_FLOOD : strcmp(type, "ebb") == 0 ? CURRENT_EBB : CURRENT_SLACK;
        get_number(p, "meanFloodDir", &flood_dir);
        get_number(p, "meanEbbDir", &ebb_dir);
        count++;
    }
    if(count == 0) {
        fail(err, err_len, "no current predictions");
        goto done;
    }

    current_build(out, events, count, (uint16_t)((int)flood_dir % 360), (uint16_t)((int)ebb_dir % 360));
    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}
