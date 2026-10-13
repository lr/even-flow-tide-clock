/**
 * @file weather_icons.h
 * Pick a Meteocons image for a weather condition.
 */
#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

#include <stdbool.h>

#include "lvgl.h"
#include "../data/weather.h"

#define WEATHER_ICON_LARGE 64
#define WEATHER_ICON_SMALL 40

/** Icon for `kind`, using the night variant where one exists. */
const lv_image_dsc_t * weather_icon(wx_kind_t kind, bool night, bool large);

#endif /* WEATHER_ICONS_H */
