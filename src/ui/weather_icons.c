/**
 * @file weather_icons.c
 */
#include "weather_icons.h"

#include "icons/meteocons.h"

typedef struct {
    const lv_image_dsc_t * day;
    const lv_image_dsc_t * night; /* same as day when there's no night variant */
} icon_pair_t;

static const icon_pair_t large[WX_KIND_COUNT] = {
    [WX_CLEAR] = {&mc_clear_day_64, &mc_clear_night_64},
    [WX_PARTLY_CLOUDY] = {&mc_partly_cloudy_day_64, &mc_partly_cloudy_night_64},
    [WX_OVERCAST] = {&mc_overcast_day_64, &mc_overcast_night_64},
    [WX_FOG] = {&mc_fog_day_64, &mc_fog_night_64},
    [WX_DRIZZLE] = {&mc_drizzle_64, &mc_drizzle_64},
    [WX_FREEZING] = {&mc_sleet_64, &mc_sleet_64},
    [WX_LIGHT_RAIN] = {&mc_partly_cloudy_day_rain_64, &mc_partly_cloudy_night_rain_64},
    [WX_RAIN] = {&mc_rain_64, &mc_rain_64},
    [WX_LIGHT_SNOW] = {&mc_partly_cloudy_day_snow_64, &mc_partly_cloudy_night_snow_64},
    [WX_SNOW] = {&mc_snow_64, &mc_snow_64},
    [WX_THUNDER] = {&mc_thunderstorms_day_64, &mc_thunderstorms_night_64},
    [WX_THUNDER_HAIL] = {&mc_thunderstorms_day_rain_64, &mc_thunderstorms_night_rain_64},
};

static const icon_pair_t small[WX_KIND_COUNT] = {
    [WX_CLEAR] = {&mc_clear_day_40, &mc_clear_night_40},
    [WX_PARTLY_CLOUDY] = {&mc_partly_cloudy_day_40, &mc_partly_cloudy_night_40},
    [WX_OVERCAST] = {&mc_overcast_day_40, &mc_overcast_night_40},
    [WX_FOG] = {&mc_fog_day_40, &mc_fog_night_40},
    [WX_DRIZZLE] = {&mc_drizzle_40, &mc_drizzle_40},
    [WX_FREEZING] = {&mc_sleet_40, &mc_sleet_40},
    [WX_LIGHT_RAIN] = {&mc_partly_cloudy_day_rain_40, &mc_partly_cloudy_night_rain_40},
    [WX_RAIN] = {&mc_rain_40, &mc_rain_40},
    [WX_LIGHT_SNOW] = {&mc_partly_cloudy_day_snow_40, &mc_partly_cloudy_night_snow_40},
    [WX_SNOW] = {&mc_snow_40, &mc_snow_40},
    [WX_THUNDER] = {&mc_thunderstorms_day_40, &mc_thunderstorms_night_40},
    [WX_THUNDER_HAIL] = {&mc_thunderstorms_day_rain_40, &mc_thunderstorms_night_rain_40},
};

const lv_image_dsc_t * weather_icon(wx_kind_t kind, bool night, bool large_size)
{
    if(kind >= WX_KIND_COUNT) kind = WX_OVERCAST;
    const icon_pair_t * p = large_size ? &large[kind] : &small[kind];
    return night ? p->night : p->day;
}
