/**
 * @file dial.h
 * The 24-hour clock face and its switchable overlays.
 *
 * Platform-independent: depends only on LVGL and the data model. The simulator
 * and the ESP32 firmware each provide time, data and an encoder input device.
 */
#ifndef DIAL_H
#define DIAL_H

#include "lvgl.h"
#include "../data/current.h"
#include "../data/sky.h"
#include "../data/tide.h"
#include "../data/weather.h"
#include "setup_card.h"

#define DIAL_SIZE 480

typedef enum {
    DIAL_LAYER_TIDE,
    DIAL_LAYER_SUN,
    DIAL_LAYER_MOON,
    DIAL_LAYER_WEATHER,
    DIAL_LAYER_CURRENT,
    DIAL_LAYER_COUNT,
} dial_layer_t;

/** Build the dial as a child of `parent` (normally the active screen). */
void dial_create(lv_obj_t * parent);

/**
 * Route an encoder group to the dial: rotation cycles layers, press toggles
 * the center readout. Puts the group in edit mode so rotation arrives as keys.
 */
void dial_attach_group(lv_group_t * group);

/** Set the displayed time of day (local). Cheap to call every second. */
void dial_set_time(int hour, int minute);

/**
 * Choose which hour sits at the top of the dial (0-23, wraps). Default is 12:
 * noon up, midnight down. 0 gives midnight up.
 */
void dial_set_top_hour(int hour);
int dial_get_top_hour(void);

/** Show heights in metres instead of feet. Data stays in feet internally. */
void dial_set_metric(bool metric);

/** Tide station name, shown on the tide layer (copied). NULL or "" hides it. */
void dial_set_station_name(const char * name);

/** Current station name, shown on the current layer (copied). */
void dial_set_current_station_name(const char * name);

/** Point the dial at today's tidal current, or NULL. The data must outlive the dial. */
void dial_set_current(const current_day_t * current);

/**
 * The configured place, shown on the sun, moon and weather layers since they
 * are computed from its coordinates (copied). With no name, the coordinates
 * are shown instead. Pass NAN coordinates when no location is set.
 */
void dial_set_place(const char * name, double latitude, double longitude);

/** Point the dial at today's sun and moon events. The data must outlive the dial. */
void dial_set_sky(const sky_day_t * sky);

/**
 * Point the dial at the current moon phase. `southern` mirrors the phase disc,
 * since the moon's lit side is flipped south of the equator.
 */
void dial_set_moon(const sky_moon_t * moon, bool southern);

/** Point the dial at the latest forecast, or NULL when there is none. The data must outlive the dial. */
void dial_set_weather(const weather_t * wx);

/** Units for the weather readout. Data stays in °F and knots internally. */
void dial_set_weather_units(temp_unit_t temp, wind_unit_t wind);

/** Point the dial at a day of tide data. The data must outlive the dial. */
void dial_set_tide(const tide_day_t * day);

/**
 * Which layers the knob visits, in order. Layers left out are skipped. If the
 * current layer isn't in the new order, the first one is shown.
 */
void dial_set_layer_order(const dial_layer_t * order, int count);

void dial_set_layer(dial_layer_t layer);
dial_layer_t dial_get_layer(void);

/** Step forwards (+1) or backwards (-1) through the layer order, wrapping around. */
void dial_step_layer(int dir);

/** Flip the center readout between the current state and upcoming detail (knob press). */
void dial_toggle_detail(void);

/** What the setup card (long press) shows: settings address, or how to join the setup network. */
void dial_set_setup_info(const setup_card_info_t * info);

/** Open or close the setup card from code, e.g. on first boot. */
void dial_show_setup(bool show);

#endif /* DIAL_H */
