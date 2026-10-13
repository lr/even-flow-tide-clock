/**
 * @file config.h
 * The clock's settings, and a plain-text form of them for storage.
 *
 * The text form is one `key=value` per line, with `#` comments allowed. It is
 * what the simulator reads from a file, what the firmware will keep in flash,
 * and what the settings web page will send to the board.
 *
 * Location is the single source of truth: weather and sun/moon use the
 * coordinates directly. The tide station is chosen by a person from the
 * stations near those coordinates, then stored here and never re-derived.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_TEXT_MAX 768 /* enough for config_format() output */

typedef enum {
    UNITS_FEET,
    UNITS_METRES,
} units_t;

typedef enum {
    TEMP_FAHRENHEIT,
    TEMP_CELSIUS,
} temp_unit_t;

typedef enum {
    WIND_KNOTS,
    WIND_MPH,
    WIND_KMH,
} wind_unit_t;

/* The dial's screens, as named in the settings */
typedef enum {
    SCREEN_TIDE,
    SCREEN_SUN,
    SCREEN_MOON,
    SCREEN_WEATHER,
    SCREEN_CURRENT,
    SCREEN_COUNT,
} screen_t;

typedef struct {
    char place_name[48];  /* shown to the user, e.g. "Sausalito, CA" */
    double latitude;      /* degrees north; NAN when unset */
    double longitude;     /* degrees east; NAN when unset */
    char tz_name[40];     /* IANA name for web APIs, e.g. "America/Los_Angeles" */
    char tz_posix[48];    /* POSIX TZ rule actually applied, e.g. "PST8PDT,M3.2.0,M11.1.0" */
    char station_id[12];  /* NOAA CO-OPS station, e.g. "8518750"; empty when unset */
    char station_name[48];
    char current_station_id[12];   /* NOAA current station, e.g. "NYH1927"; empty when unset */
    char current_station_name[48];
    int current_bin;               /* depth bin at that station; 0 = NOAA's default */
    int top_hour;         /* hour at the top of the dial, 0-23 */
    units_t units;          /* tide heights */
    temp_unit_t temp_unit;
    wind_unit_t wind_unit;
    screen_t screens[SCREEN_COUNT]; /* knob order; screens left out are hidden */
    int screen_count;               /* at least 1 */
} clock_config_t;

/** Fill with defaults: no location, no station, UTC, noon at the top, feet, °F, knots. */
void config_defaults(clock_config_t * cfg);

/** Settings name of a screen, e.g. "tide". */
const char * config_screen_name(screen_t screen);

bool config_has_location(const clock_config_t * cfg);
bool config_has_station(const clock_config_t * cfg);
bool config_has_current_station(const clock_config_t * cfg);

/**
 * Apply `key=value` lines from `text` on top of whatever `cfg` holds.
 * Unknown keys are ignored so older firmware can read newer files. Bad values
 * leave that field unchanged. Returns the number of bad lines; the first
 * problem is described in `err` when it is non-NULL.
 */
int config_parse(clock_config_t * cfg, const char * text, char * err, size_t err_len);

/** Write `cfg` as text. Returns the length written, or 0 if `len` is too small. */
size_t config_format(const clock_config_t * cfg, char * buf, size_t len);

#endif /* CONFIG_H */
