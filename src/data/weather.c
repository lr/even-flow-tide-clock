/**
 * @file weather.c
 */
#include "weather.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define HOURLY_FIELDS "temperature_2m,precipitation_probability,precipitation,weather_code," \
                      "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
#define CURRENT_FIELDS "temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
#define QUARTER_FIELDS "precipitation,weather_code"

/*********************
 *  REQUEST
 *********************/
static size_t url_encode(char * dst, size_t len, const char * src)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for(; *src; src++) {
        unsigned char c = (unsigned char)*src;
        bool plain = isalnum(c) || c == '-' || c == '_' || c == '.';
        size_t need = plain ? 1 : 3;
        if(n + need >= len) return 0;
        if(plain) {
            dst[n++] = (char)c;
        }
        else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 0xf];
        }
    }
    dst[n] = '\0';
    return n;
}

size_t weather_build_url(char * buf, size_t len, const clock_config_t * cfg)
{
    if(!config_has_location(cfg)) return 0;

    char tz[3 * sizeof(cfg->tz_name)];
    if(cfg->tz_name[0] == '\0') snprintf(tz, sizeof(tz), "auto");
    else if(url_encode(tz, sizeof(tz), cfg->tz_name) == 0) return 0;

    /* Always °F and knots; the dial converts for display */
    int n = snprintf(buf, len,
                     "https://api.open-meteo.com/v1/forecast"
                     "?latitude=%.4f&longitude=%.4f"
                     "&hourly=" HOURLY_FIELDS
                     "&current=" CURRENT_FIELDS
                     "&daily=temperature_2m_max,temperature_2m_min"
                     "&minutely_15=" QUARTER_FIELDS "&forecast_minutely_15=%d"
                     "&temperature_unit=fahrenheit&wind_speed_unit=kn&precipitation_unit=inch"
                     "&timezone=%s&forecast_days=2",
                     cfg->latitude, cfg->longitude, WEATHER_MAX_QUARTERS, tz);
    if(n < 0 || (size_t)n >= len) return 0;
    return (size_t)n;
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

/* Open-Meteo uses null for missing values */
static double num_at(const cJSON * arr, int i, double fallback)
{
    const cJSON * item = cJSON_GetArrayItem(arr, i);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static double num_field(const cJSON * obj, const char * key, double fallback)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static bool unit_is(const cJSON * units, const char * key, const char * expected)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(units, key);
    return cJSON_IsString(item) && strcmp(item->valuestring, expected) == 0;
}

/* "YYYY-MM-DDTHH:MM" -> date part and minutes into the day */
static bool parse_iso_hour(const char * s, char date[11], int * minute)
{
    int h, m;
    if(strlen(s) < 16 || s[10] != 'T' || sscanf(s + 11, "%2d:%2d", &h, &m) != 2) return false;
    memcpy(date, s, 10);
    date[10] = '\0';
    *minute = h * 60 + m;
    return true;
}

static uint8_t clamp_u8(double v)
{
    if(isnan(v) || v < 0.0) return 0;
    return v > 255.0 ? 255 : (uint8_t)lround(v);
}

static uint16_t direction(double deg)
{
    if(isnan(deg)) return 0;
    long d = lround(fmod(deg, 360.0));
    return (uint16_t)(d < 0 ? d + 360 : d);
}

bool weather_parse_open_meteo(weather_t * out, const char * json, char * err, size_t err_len)
{
    memset(out, 0, sizeof(*out));
    if(err && err_len) err[0] = '\0';

    cJSON * root = cJSON_Parse(json);
    if(root == NULL) {
        fail(err, err_len, "not valid JSON");
        return false;
    }

    bool ok = false;
    const cJSON * hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON * units = cJSON_GetObjectItemCaseSensitive(root, "hourly_units");
    const cJSON * times = cJSON_GetObjectItemCaseSensitive(hourly, "time");

    if(!cJSON_IsArray(times) || cJSON_GetArraySize(times) == 0) {
        const cJSON * reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        fail(err, err_len, "no hourly data%s%s", cJSON_IsString(reason) ? ": " : "",
             cJSON_IsString(reason) ? reason->valuestring : "");
        goto done;
    }
    /* Guard against a request built with other units */
    if(!unit_is(units, "temperature_2m", "\xC2\xB0" "F") || !unit_is(units, "wind_speed_10m", "kn")) {
        fail(err, err_len, "unexpected units (want °F and kn)");
        goto done;
    }

    const cJSON * temp = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    const cJSON * prob = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
    const cJSON * precip = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation");
    const cJSON * code = cJSON_GetObjectItemCaseSensitive(hourly, "weather_code");
    const cJSON * wind = cJSON_GetObjectItemCaseSensitive(hourly, "wind_speed_10m");
    const cJSON * wdir = cJSON_GetObjectItemCaseSensitive(hourly, "wind_direction_10m");
    const cJSON * gust = cJSON_GetObjectItemCaseSensitive(hourly, "wind_gusts_10m");

    int count = cJSON_GetArraySize(times);
    if(count > WEATHER_MAX_HOURS) count = WEATHER_MAX_HOURS;

    char prev_date[11] = "";
    int day = 0;
    for(int i = 0; i < count; i++) {
        const cJSON * t = cJSON_GetArrayItem(times, i);
        char date[11];
        int minute;
        if(!cJSON_IsString(t) || !parse_iso_hour(t->valuestring, date, &minute)) {
            fail(err, err_len, "bad time at hour %d", i);
            goto done;
        }
        if(i == 0) memcpy(out->first_date, date, sizeof(date));
        else if(strcmp(date, prev_date) != 0) day++;
        memcpy(prev_date, date, sizeof(date));

        weather_hour_t * h = &out->hours[i];
        h->minute = (float)(day * 1440 + minute);
        h->temp_f = (float)num_at(temp, i, NAN);
        h->precip_prob = clamp_u8(num_at(prob, i, 0.0));
        h->precip_in = (float)num_at(precip, i, 0.0);
        h->code = clamp_u8(num_at(code, i, 0.0));
        h->wind_kt = (float)num_at(wind, i, NAN);
        h->gust_kt = (float)num_at(gust, i, NAN);
        h->wind_from_deg = direction(num_at(wdir, i, NAN));
    }
    out->hour_count = (uint8_t)count;

    const cJSON * cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON * cur_time = cJSON_GetObjectItemCaseSensitive(cur, "time");
    char cur_date[11];
    int cur_minute;
    out->current.minute = NAN;
    if(cJSON_IsString(cur_time) && parse_iso_hour(cur_time->valuestring, cur_date, &cur_minute)) {
        out->current.minute = (float)((strcmp(cur_date, out->first_date) == 0 ? 0 : 1440) + cur_minute);
    }
    out->current.temp_f = (float)num_field(cur, "temperature_2m", NAN);
    out->current.code = clamp_u8(num_field(cur, "weather_code", 0.0));
    out->current.wind_kt = (float)num_field(cur, "wind_speed_10m", NAN);
    out->current.gust_kt = (float)num_field(cur, "wind_gusts_10m", NAN);
    out->current.wind_from_deg = direction(num_field(cur, "wind_direction_10m", NAN));

    /* 15-minute data is optional: older saved responses don't have it */
    const cJSON * q15 = cJSON_GetObjectItemCaseSensitive(root, "minutely_15");
    const cJSON * q_times = cJSON_GetObjectItemCaseSensitive(q15, "time");
    const cJSON * q_precip = cJSON_GetObjectItemCaseSensitive(q15, "precipitation");
    const cJSON * q_code = cJSON_GetObjectItemCaseSensitive(q15, "weather_code");
    int q_count = cJSON_IsArray(q_times) ? cJSON_GetArraySize(q_times) : 0;
    for(int i = 0; i < q_count && out->quarter_count < WEATHER_MAX_QUARTERS; i++) {
        const cJSON * t = cJSON_GetArrayItem(q_times, i);
        char date[11];
        int minute;
        if(!cJSON_IsString(t) || !parse_iso_hour(t->valuestring, date, &minute)) break;
        int cmp = strcmp(date, out->first_date);
        if(cmp < 0) continue; /* before the hourly data starts */
        weather_quarter_t * q = &out->quarters[out->quarter_count++];
        q->minute = (float)((cmp == 0 ? 0 : 1440) + minute);
        q->precip_in = (float)num_at(q_precip, i, 0.0);
        q->code = clamp_u8(num_at(q_code, i, 0.0));
    }

    const cJSON * daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    out->today_high_f = (float)num_at(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max"), 0, NAN);
    out->today_low_f = (float)num_at(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min"), 0, NAN);

    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}

/*********************
 *  CLASSIFICATION
 *********************/
wx_kind_t weather_kind(uint8_t c)
{
    if(c == 0) return WX_CLEAR;
    if(c <= 2) return WX_PARTLY_CLOUDY;
    if(c == 3) return WX_OVERCAST;
    if(c == 45 || c == 48) return WX_FOG;
    if(c >= 51 && c <= 55) return WX_DRIZZLE;
    if(c == 56 || c == 57 || c == 66 || c == 67) return WX_FREEZING;
    if(c == 61 || c == 80 || c == 81) return WX_LIGHT_RAIN;
    if(c == 63 || c == 65 || c == 82) return WX_RAIN;
    if(c == 71 || c == 85 || c == 86) return WX_LIGHT_SNOW;
    if(c == 73 || c == 75 || c == 77) return WX_SNOW;
    if(c == 95) return WX_THUNDER;
    if(c == 96 || c == 99) return WX_THUNDER_HAIL;
    return WX_OVERCAST; /* unknown code: show something neutral */
}

bool weather_hour_is_wet(const weather_hour_t * h)
{
    if(h->precip_prob >= 50) return true;
    wx_kind_t k = weather_kind(h->code);
    bool wet_code = k != WX_CLEAR && k != WX_PARTLY_CLOUDY && k != WX_OVERCAST && k != WX_FOG;
    return wet_code && h->precip_in >= 0.01f;
}

bool weather_hour_is_stormy(const weather_hour_t * h)
{
    wx_kind_t k = weather_kind(h->code);
    return k == WX_THUNDER || k == WX_THUNDER_HAIL || (!isnan(h->gust_kt) && h->gust_kt >= WEATHER_GALE_KT);
}

int weather_severity(const weather_hour_t * h)
{
    static const int rank[WX_KIND_COUNT] = {
        [WX_CLEAR] = 0,      [WX_PARTLY_CLOUDY] = 5, [WX_OVERCAST] = 10, [WX_FOG] = 20,
        [WX_DRIZZLE] = 30,   [WX_LIGHT_RAIN] = 40,   [WX_LIGHT_SNOW] = 45, [WX_RAIN] = 50,
        [WX_SNOW] = 60,      [WX_FREEZING] = 70,     [WX_THUNDER] = 90,  [WX_THUNDER_HAIL] = 100,
    };
    int s = rank[weather_kind(h->code)];
    if(!isnan(h->gust_kt) && h->gust_kt >= WEATHER_GALE_KT && s < 85) s = 85;
    return s;
}

const char * weather_code_name(uint8_t c)
{
    switch(c) {
        case 0: return "Clear";
        case 1: return "Mostly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Freezing fog";
        case 51: return "Light drizzle";
        case 53: return "Drizzle";
        case 55: return "Heavy drizzle";
        case 56:
        case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66:
        case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80:
        case 81: return "Showers";
        case 82: return "Downpours";
        case 85: return "Snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96:
        case 99: return "Thunder and hail";
        default: return "Cloudy";
    }
}

/*********************
 *  OUTLOOK
 *********************/
static bool code_is_wet(uint8_t code)
{
    wx_kind_t k = weather_kind(code);
    return k != WX_CLEAR && k != WX_PARTLY_CLOUDY && k != WX_OVERCAST && k != WX_FOG;
}

static bool quarter_is_wet(const weather_quarter_t * q)
{
    return code_is_wet(q->code) && q->precip_in > 0.0f;
}

/* Lower-case word for what's falling: "rain", "snow", "drizzle", "sleet", "storms" */
static const char * precip_word(uint8_t code)
{
    switch(weather_kind(code)) {
        case WX_DRIZZLE: return "drizzle";
        case WX_FREEZING: return "sleet";
        case WX_LIGHT_SNOW:
        case WX_SNOW: return "snow";
        case WX_THUNDER:
        case WX_THUNDER_HAIL: return "storms";
        default: return "rain";
    }
}

static void hhmm(char * buf, size_t len, float minute)
{
    int m = (int)lroundf(minute) % 1440;
    snprintf(buf, len, "%02d:%02d", m / 60, m % 60);
}

static const weather_quarter_t * quarter_at(const weather_t * wx, float now)
{
    for(uint8_t i = 0; i < wx->quarter_count; i++) {
        if(now >= wx->quarters[i].minute && now < wx->quarters[i].minute + 15.0f) return &wx->quarters[i];
    }
    return NULL;
}

/* Best guess at the weather right now, and whether anything is falling */
static uint8_t code_now(const weather_t * wx, float now, bool * wet_out)
{
    const weather_quarter_t * q_now = quarter_at(wx, now);
    const weather_hour_t * h_now = weather_hour_at(wx, now);

    /* "current" is one 15-minute reading; trust it only near the time it was for */
    bool current_applies = !isnan(wx->current.minute) && fabsf(now - wx->current.minute) < 30.0f;
    uint8_t code = current_applies ? wx->current.code : h_now != NULL ? h_now->code : wx->current.code;

    bool wet;
    if(q_now != NULL) {
        wet = quarter_is_wet(q_now);
        if(wet) code = q_now->code;
    }
    else {
        wet = h_now != NULL && weather_hour_is_wet(h_now);
        /* "current" is finer than the hour, so keep its code if it agrees it's wet */
        if(wet && !code_is_wet(code) && code_is_wet(h_now->code)) code = h_now->code;
    }
    if(!wet && code_is_wet(code)) {
        /* The code says wet but the amounts say dry: fall back to the sky */
        code = h_now != NULL && !code_is_wet(h_now->code) ? h_now->code : 3;
    }

    if(wet_out) *wet_out = wet;
    return code;
}

uint8_t weather_code_now(const weather_t * wx, float now_minute)
{
    return code_now(wx, now_minute, NULL);
}

void weather_outlook(const weather_t * wx, float now, char * text, size_t text_len, char * short_text,
                     size_t short_len)
{
    bool wet_now;
    uint8_t now_code = code_now(wx, now, &wet_now);
    const weather_quarter_t * q_now = quarter_at(wx, now);

    /* First change, from the 15-minute steps and then the hours after them */
    bool found = false;
    float change_at = 0.0f;
    uint8_t change_code = 0;
    float covered_until = now;

    for(uint8_t i = 0; i < wx->quarter_count && !found; i++) {
        const weather_quarter_t * q = &wx->quarters[i];
        if(q->minute + 15.0f <= now) continue;
        if(q != q_now && q->minute > now && quarter_is_wet(q) != wet_now) {
            found = true;
            change_at = q->minute;
            change_code = q->code;
        }
        covered_until = q->minute + 15.0f;
    }
    for(uint8_t i = 0; i < wx->hour_count && !found; i++) {
        const weather_hour_t * h = &wx->hours[i];
        if(h->minute + 60.0f <= covered_until) continue;
        if(h->minute > now + 12 * 60) break;
        if(weather_hour_is_wet(h) != wet_now) {
            found = true;
            change_at = h->minute > covered_until ? h->minute : covered_until;
            change_code = h->code;
        }
    }

    const char * name = weather_code_name(now_code);
    char t[8];

    if(!wet_now && !found) {
        snprintf(text, text_len, "%s", name);
        snprintf(short_text, short_len, "%s", name);
        return;
    }

    if(!wet_now) {
        const char * word = precip_word(change_code);
        char when[24];
        int mins = (int)lroundf((change_at - now) / 5.0f) * 5;
        if(mins < 60) {
            snprintf(when, sizeof(when), "in %d min", mins < 5 ? 5 : mins);
        }
        else {
            hhmm(t, sizeof(t), change_at);
            snprintf(when, sizeof(when), "at %s", t);
        }
        snprintf(text, text_len, "%s, %s %s", name, word, when);
        snprintf(short_text, short_len, "%c%s %s", toupper((unsigned char)word[0]), word + 1, when);
        return;
    }

    /* Wet now: say when it stops, or that it carries on past the window */
    const char * word = precip_word(now_code);
    if(found) {
        hhmm(t, sizeof(t), change_at);
        snprintf(text, text_len, "%s until %s", name, t);
        snprintf(short_text, short_len, "%c%s until %s", toupper((unsigned char)word[0]), word + 1, t);
    }
    else {
        hhmm(t, sizeof(t), now + 12 * 60);
        snprintf(text, text_len, "%s past %s", name, t);
        snprintf(short_text, short_len, "%c%s past %s", toupper((unsigned char)word[0]), word + 1, t);
    }
}

const char * weather_compass(uint16_t deg)
{
    static const char * const points[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return points[((deg % 360) * 16 + 180) / 360 % 16];
}

const weather_hour_t * weather_hour_at(const weather_t * wx, float minute)
{
    for(uint8_t i = 0; i < wx->hour_count; i++) {
        float start = wx->hours[i].minute;
        if(minute >= start && minute < start + 60.0f) return &wx->hours[i];
    }
    return NULL;
}
