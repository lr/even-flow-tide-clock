/**
 * @file dial.c
 *
 * Layout (radii from the center of the 480x480 face):
 *
 *   240  edge of glass
 *   224-236  tick ring (15 min minor, hourly major)
 *   206  hour numerals (the hand has a gap here)
 *   124-190  overlay band (tide curve lives here)
 *   112  inner edge of the band
 *   <100 center readout (time, layer name, summary)
 *
 * Angles: time runs clockwise, with a user-selectable hour at the top. The
 * default is noon at the top, so the sun tracks roughly across the dial the
 * way it crosses the sky.
 */
#include "dial.h"
#include "../data/curve.h"
#include "setup_card.h"
#include "weather_icons.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

/*********************
 *  GEOMETRY
 *********************/
#define R_EDGE         240
#define R_TICK_OUT     236
#define R_TICK_MINOR   230
#define R_TICK_MAJOR   224
#define R_NUMERAL      206
#define R_BAND_OUT     190
#define R_BAND_MIN     124 /* lowest water still leaves a visible sliver */
#define R_BAND_IN      112
#define R_HAND_IN      100
#define SUMMARY_MAX_W  196 /* widest readout line that stays inside R_BAND_IN */

#define MINUTES_PER_DAY 1440

/*********************
 *  PALETTE
 *********************/
#define COLOR_FACE        lv_color_hex(0x0b1622)
#define COLOR_TICK_MINOR  lv_color_hex(0x3a4c5e)
#define COLOR_TICK_HOUR   lv_color_hex(0x7f93a8)
#define COLOR_TICK_MAJOR  lv_color_hex(0xd8e2ec)
#define COLOR_NUMERAL     lv_color_hex(0x7f93a8)
#define COLOR_NUMERAL_MAJ lv_color_hex(0xd8e2ec)
#define COLOR_GUIDE       lv_color_hex(0x1c2b3a)
#define COLOR_TIDE_PAST   lv_color_hex(0x173a52)
#define COLOR_TIDE_FUTURE lv_color_hex(0x1f6f9f)
#define COLOR_TIDE_LINE   lv_color_hex(0x7fd1ff)
#define COLOR_HIGH        lv_color_hex(0xe8f6ff)
#define COLOR_LOW         lv_color_hex(0x4a90b8)
#define COLOR_HAND        lv_color_hex(0xffb347)
#define COLOR_NIGHT       lv_color_hex(0x101c2b)
#define COLOR_ASTRO       lv_color_hex(0x172840)
#define COLOR_NAUTICAL    lv_color_hex(0x223a5c)
#define COLOR_CIVIL       lv_color_hex(0x35567f)
#define COLOR_DAY         lv_color_hex(0x5f8fc0)
#define COLOR_SUN_MARK    lv_color_hex(0xf6e7bd)
#define COLOR_MOON_UP     lv_color_hex(0x55667c)
#define COLOR_MOON_MARK   lv_color_hex(0xdfe6ee)
#define COLOR_MOON_DARK   lv_color_hex(0x1f2b3a)
#define COLOR_MOON_LIT    lv_color_hex(0xe6ebf0)
#define COLOR_WX_CLEAR    lv_color_hex(0x173556)
#define COLOR_WX_CLOUD    lv_color_hex(0x2b3a4b)
#define COLOR_WX_RAIN     lv_color_hex(0x3a9be0)
#define COLOR_WX_SNOW     lv_color_hex(0xd5e2ee)
#define COLOR_WX_STORM    lv_color_hex(0xe5533d)

/* Weather band: rain-chance bars on the inside, conditions and icons outside */
#define R_WX_RAIN_OUT     (R_BAND_IN + 30)
#define R_WX_ICON         ((R_WX_RAIN_OUT + R_BAND_OUT) / 2.0f) /* icon and temperature, side by side */
#define WX_ICON_TEMP_GAP  21.0f                  /* px from block centre to icon / temperature centre */
#define R_WX_WIND_LABEL   (R_BAND_IN + 15.0f)    /* wind speed, near the low end of the wind line */
#define R_WX_STORM_W      5
#define COLOR_WIND        lv_color_hex(0xf0453a)
#define COLOR_FLOOD       lv_color_hex(0x2bb3a0)
#define COLOR_FLOOD_PAST  lv_color_hex(0x15514a)
#define COLOR_EBB         lv_color_hex(0x8f6ad8)
#define COLOR_EBB_PAST    lv_color_hex(0x3c2f62)
#define COLOR_SLACK       lv_color_hex(0xe8eef4)
#define COLOR_FLOOD_DART  lv_color_hex(0x7ff5e0) /* brighter than the fill they sit on */
#define COLOR_EBB_DART    lv_color_hex(0xcdb6ff)

/* Current band: slack is a circle mid-band, flood draws outward, ebb inward */
#define R_SLACK           ((R_BAND_IN + R_BAND_OUT) / 2.0f)
#define R_CURRENT_SWING   ((R_BAND_OUT - R_BAND_IN) / 2.0f - 4.0f)

/* Wind curve: calm sits just inside the band, WIND_FULL_KT at its outer edge */
#define WIND_FULL_KT      30.0f
#define R_WIND_MIN        (R_BAND_IN + 6)
#define R_WIND_MAX        (R_BAND_OUT - 6)
#define WIND_DASH_PX      7.0f
#define WIND_GAP_PX       5.0f
#define WIND_CARET_EVERY  2 /* hours */
#define COLOR_TEXT        lv_color_hex(0xe8eef4)
#define COLOR_TEXT_DIM    lv_color_hex(0x7f93a8)
#define COLOR_SCALE       lv_color_hex(0x2c4258) /* faint rings that give a band its scale */
#define TAG_BG_OPA        LV_OPA_80              /* backing behind labels drawn over data */

/*********************
 *  STATE
 *********************/
static struct {
    lv_obj_t * face;
    lv_obj_t * layers[DIAL_LAYER_COUNT];
    lv_obj_t * hand;
    lv_obj_t * time_label;
    lv_obj_t * layer_label;
    lv_obj_t * summary_label;
    lv_obj_t * source_label; /* where the current layer's data comes from */

    const tide_day_t * tide;
    const sky_day_t * sky;
    const sky_moon_t * moon;
    const weather_t * wx;
    temp_unit_t temp_unit;
    wind_unit_t wind_unit;
    bool southern;
    char place_name[48];
    char station_name[48];
    char current_station_name[48];
    const current_day_t * current;
    double latitude; /* NAN when unset */
    double longitude;
    int minute_of_day;
    int top_hour;
    dial_layer_t layer;
    dial_layer_t order[DIAL_LAYER_COUNT];
    int order_count;
    bool detail;
    bool metric;
} dial;

static const char * const layer_names[DIAL_LAYER_COUNT] = {
    [DIAL_LAYER_TIDE] = "TIDE",
    [DIAL_LAYER_SUN] = "SUN",
    [DIAL_LAYER_MOON] = "MOON",
    [DIAL_LAYER_WEATHER] = "WEATHER",
    [DIAL_LAYER_CURRENT] = "CURRENT",
};

/*********************
 *  HELPERS
 *********************/
/* LVGL angles: 0 = 3 o'clock, clockwise positive, so the top is -90 */
static float minute_to_deg(float minute)
{
    float deg = (minute - dial.top_hour * 60.0f) / MINUTES_PER_DAY * 360.0f - 90.0f;
    deg = fmodf(deg, 360.0f);
    return deg < 0.0f ? deg + 360.0f : deg;
}

static float minute_to_rad(float minute)
{
    return minute_to_deg(minute) * (float)M_PI / 180.0f;
}

static lv_point_precise_t polar(const lv_area_t * coords, float minute, float r)
{
    float a = minute_to_rad(minute);
    lv_point_precise_t p = {
        .x = coords->x1 + DIAL_SIZE / 2.0f + r * cosf(a),
        .y = coords->y1 + DIAL_SIZE / 2.0f + r * sinf(a),
    };
    return p;
}

static float tide_radius(const tide_day_t * day, float height_ft)
{
    float span = day->max_ft - day->min_ft;
    float norm = span > 0.0f ? (height_ft - day->min_ft) / span : 0.5f;
    return R_BAND_MIN + norm * (R_BAND_OUT - R_BAND_MIN);
}

/* Integer formatting keeps us independent of printf float support on the MCU */
static void format_height(char * buf, size_t len, float ft)
{
    float v = dial.metric ? ft * 0.3048f : ft;
    int tenths = (int)lroundf(v * 10.0f);
    const char * sign = tenths < 0 ? "-" : "";
    if(tenths < 0) tenths = -tenths;
    snprintf(buf, len, "%s%d.%d %s", sign, tenths / 10, tenths % 10, dial.metric ? "m" : "ft");
}

static void format_hhmm(char * buf, size_t len, float minute)
{
    int m = (int)lroundf(minute) % MINUTES_PER_DAY;
    snprintf(buf, len, "%02d:%02d", m / 60, m % 60);
}

static void format_hhmm_or_dash(char * buf, size_t len, float minute)
{
    if(isnan(minute)) snprintf(buf, len, "--:--");
    else format_hhmm(buf, len, minute);
}

static void draw_text(lv_layer_t * layer, lv_point_precise_t center, const char * text,
                      const lv_font_t * font, lv_color_t color)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = 1; /* draw tasks run later; copy the stack buffer */
    dsc.font = font;
    dsc.color = color;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    int32_t w = 40;
    int32_t h = lv_font_get_line_height(font);
    lv_area_t area = {
        .x1 = (int32_t)lroundf(center.x) - w / 2,
        .y1 = (int32_t)lroundf(center.y) - h / 2,
    };
    area.x2 = area.x1 + w - 1;
    area.y2 = area.y1 + h - 1;
    lv_draw_label(layer, &dsc, &area);
}

static void draw_dot(lv_layer_t * layer, lv_point_precise_t c, int32_t r, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = color;
    dsc.border_color = COLOR_FACE;
    dsc.border_width = 2;

    int32_t x = (int32_t)lroundf(c.x);
    int32_t y = (int32_t)lroundf(c.y);
    lv_area_t area = {x - r, y - r, x + r, y + r};
    lv_draw_rect(layer, &dsc, &area);
}

/* Text on a small dark backing, so it stays legible over fills and lines.
 * Multi-line text is centred line by line. */
static void draw_tag(lv_layer_t * layer, lv_point_precise_t center, const char * text, const lv_font_t * font,
                     lv_color_t color)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t x = (int32_t)lroundf(center.x) - size.x / 2;
    int32_t y = (int32_t)lroundf(center.y) - size.y / 2;

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = COLOR_FACE;
    bg.bg_opa = TAG_BG_OPA;
    bg.radius = 4;
    lv_area_t box = {x - 3, y - 1, x + size.x + 2, y + size.y};
    lv_draw_rect(layer, &bg, &box);

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = 1;
    dsc.font = font;
    dsc.color = color;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t area = {x, y, x + size.x - 1, y + size.y - 1};
    lv_draw_label(layer, &dsc, &area);
}

/* A faint full circle marking one value on a band's scale */
static void draw_scale_ring(lv_layer_t * layer, const lv_area_t * coords, float r)
{
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = coords->x1 + DIAL_SIZE / 2;
    arc.center.y = coords->y1 + DIAL_SIZE / 2;
    arc.radius = (uint16_t)lroundf(r);
    arc.width = 1;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.color = COLOR_SCALE;
    lv_draw_arc(layer, &arc);
}

/* Where to stack scale labels: straight up or straight down from the centre
 * (the only directions where labels a ring apart don't overlap sideways),
 * whichever is further from the day's events (`busy`, in minutes). Events
 * don't move during the day, so neither do the labels. */
static float quiet_minute(const float * busy, int n)
{
    float best = dial.top_hour * 60.0f, best_gap = -1.0f;
    for(int k = 0; k < 2; k++) {
        float m = fmodf((dial.top_hour + 12 * k) * 60.0f, MINUTES_PER_DAY), gap = MINUTES_PER_DAY;
        for(int i = 0; i < n; i++) {
            float d = fabsf(fmodf(m - busy[i] + MINUTES_PER_DAY * 2, MINUTES_PER_DAY));
            if(d > MINUTES_PER_DAY / 2) d = MINUTES_PER_DAY - d;
            if(d < gap) gap = d;
        }
        if(gap > best_gap) {
            best_gap = gap;
            best = m;
        }
    }
    return best;
}

static void draw_scale_label(lv_layer_t * layer, const lv_area_t * coords, float minute, float r,
                             const char * text, lv_color_t color)
{
    draw_tag(layer, polar(coords, minute, r), text, &lv_font_montserrat_12, color);
}

/* "2", "2.5" or "-0.5": as many decimals as the step needs, at most one */
static void format_scale_value(char * buf, size_t len, float v, float step)
{
    int tenths = (int)lroundf(v * 10.0f);
    const char * sign = tenths < 0 ? "-" : "";
    if(tenths < 0) tenths = -tenths;
    bool whole = fmodf(step, 1.0f) == 0.0f || tenths % 10 == 0;
    if(whole) snprintf(buf, len, "%s%d", sign, (tenths + 5) / 10);
    else snprintf(buf, len, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

/* Fill the overlay band between two times of day */
static void draw_band(lv_layer_t * layer, const lv_area_t * coords, float start, float end, lv_color_t color)
{
    if(end <= start) return;

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = coords->x1 + DIAL_SIZE / 2;
    arc.center.y = coords->y1 + DIAL_SIZE / 2;
    arc.radius = R_BAND_OUT;
    arc.width = R_BAND_OUT - R_BAND_IN;
    arc.color = color;
    if(end - start >= MINUTES_PER_DAY - 0.5f) {
        arc.start_angle = 0; /* equal angles would draw nothing */
        arc.end_angle = 360;
    }
    else {
        arc.start_angle = minute_to_deg(start);
        arc.end_angle = minute_to_deg(end);
    }
    lv_draw_arc(layer, &arc);
}

static void draw_spans(lv_layer_t * layer, const lv_area_t * coords, const sky_spans_t * spans, lv_color_t color)
{
    for(uint8_t i = 0; i < spans->count; i++) {
        draw_band(layer, coords, spans->span[i].start, spans->span[i].end, color);
    }
}

/* A transparent full-face child that ignores input and has no theme styling */
static lv_obj_t * create_plain_layer(lv_obj_t * parent)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, DIAL_SIZE, DIAL_SIZE);
    lv_obj_center(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/*********************
 *  FACE (always visible)
 *********************/
static void face_draw_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    /* Guide circle marking the inner edge of the overlay band */
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = coords.x1 + DIAL_SIZE / 2;
    arc.center.y = coords.y1 + DIAL_SIZE / 2;
    arc.radius = R_BAND_IN;
    arc.width = 1;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.color = COLOR_GUIDE;
    lv_draw_arc(layer, &arc);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);

    for(int q = 0; q < 24 * 4; q++) {
        float minute = q * 15.0f;
        bool hour = q % 4 == 0;
        bool major = q % 24 == 0;

        line.color = major ? COLOR_TICK_MAJOR : hour ? COLOR_TICK_HOUR : COLOR_TICK_MINOR;
        line.width = major ? 4 : hour ? 2 : 1;
        line.p1 = polar(&coords, minute, hour ? R_TICK_MAJOR : R_TICK_MINOR);
        line.p2 = polar(&coords, minute, R_TICK_OUT);
        lv_draw_line(layer, &line);
    }

    for(int h = 0; h < 24; h++) {
        bool major = h % 6 == 0;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", h);
        draw_text(layer, polar(&coords, h * 60.0f, R_NUMERAL), buf,
                  major ? &lv_font_montserrat_20 : &lv_font_montserrat_14,
                  major ? COLOR_NUMERAL_MAJ : COLOR_NUMERAL);
    }
}

/*********************
 *  TIDE LAYER
 *********************/
static void tide_draw_cb(lv_event_t * e)
{
    const tide_day_t * day = dial.tide;
    if(day == NULL || day->count < 2) return;

    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float now = (float)dial.minute_of_day;

    /* Filled band as overlapping radial spokes, one per sample. Triangles were
     * tried first, but every triangle edge is anti-aliased and the shared edges
     * showed as seams. Spokes are wide enough to overlap at the outer edge
     * (arc length per 6 min at R_BAND_OUT is ~5 px). Past water is dimmed. */
    float spoke_w = 2.0f * (float)M_PI * R_BAND_OUT * day->interval_min / MINUTES_PER_DAY + 1.5f;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = (int32_t)ceilf(spoke_w);

    for(uint16_t i = 0; i < day->count; i++) {
        float m = (float)(i * day->interval_min);
        /* Stop short of the water line so square spoke ends hide under it */
        float r = tide_radius(day, day->height_ft[i]) - 1.5f;
        line.color = m < now ? COLOR_TIDE_PAST : COLOR_TIDE_FUTURE;
        line.p1 = polar(&coords, m, R_BAND_IN);
        line.p2 = polar(&coords, m, r);
        lv_draw_line(layer, &line);
    }

    /* Height scale: rings at round heights in the display unit */
    float unit = dial.metric ? 0.3048f : 1.0f;
    float lo = day->min_ft * unit, hi = day->max_ft * unit;
    float step = curve_nice_step(hi - lo, 4);
    if(step > 0.0f) {
        for(float v = ceilf(lo / step) * step; v <= hi + 1e-3f; v += step) draw_scale_ring(layer, &coords, tide_radius(day, v / unit));
    }

    /* Water line on top of the fill */
    line.color = COLOR_TIDE_LINE;
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;

    for(uint16_t i = 0; i + 1 < day->count; i++) {
        line.p1 = polar(&coords, (float)(i * day->interval_min), tide_radius(day, day->height_ft[i]));
        line.p2 = polar(&coords, (float)((i + 1) * day->interval_min), tide_radius(day, day->height_ft[i + 1]));
        lv_draw_line(layer, &line);
    }

    for(uint8_t i = 0; i < day->event_count; i++) {
        const tide_event_t * ev = &day->events[i];
        draw_dot(layer, polar(&coords, ev->minute, tide_radius(day, ev->height_ft)), 5,
                 ev->high ? COLOR_HIGH : COLOR_LOW);
    }

    /* Where the hand meets the water */
    draw_dot(layer, polar(&coords, now, tide_radius(day, tide_height_at(day, now))), 6, COLOR_HAND);

    /* Labels last, over everything. Scale values where no event is, then each
     * high (inside the water, clear of the numerals) and low (above the
     * water, where the band is empty) with its time and height. */
    float busy[TIDE_MAX_EVENTS];
    for(uint8_t i = 0; i < day->event_count; i++) busy[i] = day->events[i].minute;
    float label_at = quiet_minute(busy, day->event_count);
    if(step > 0.0f) {
        for(float v = ceilf(lo / step) * step; v <= hi + 1e-3f; v += step) {
            char buf[16], val[12];
            format_scale_value(val, sizeof(val), v, step);
            snprintf(buf, sizeof(buf), "%s %s", val, dial.metric ? "m" : "ft");
            draw_scale_label(layer, &coords, label_at, tide_radius(day, v / unit), buf, COLOR_TEXT_DIM);
        }
    }
    for(uint8_t i = 0; i < day->event_count; i++) {
        const tide_event_t * ev = &day->events[i];
        char t[8], h[16], buf[32];
        format_hhmm(t, sizeof(t), ev->minute);
        format_height(h, sizeof(h), ev->height_ft);
        snprintf(buf, sizeof(buf), "%s\n%s", t, h);
        float r = tide_radius(day, ev->height_ft) + (ev->high ? -22.0f : 22.0f);
        draw_tag(layer, polar(&coords, ev->minute, r), buf, &lv_font_montserrat_12, ev->high ? COLOR_HIGH : COLOR_TEXT);
    }
}

/*********************
 *  SUN LAYER
 *********************/
static void sun_draw_cb(lv_event_t * e)
{
    const sky_day_t * sky = dial.sky;
    if(sky == NULL) return;

    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    /* Night underneath, then each brighter band on top of the one before */
    draw_band(layer, &coords, 0.0f, MINUTES_PER_DAY, COLOR_NIGHT);
    draw_spans(layer, &coords, &sky->astro, COLOR_ASTRO);
    draw_spans(layer, &coords, &sky->nautical, COLOR_NAUTICAL);
    draw_spans(layer, &coords, &sky->civil, COLOR_CIVIL);
    draw_spans(layer, &coords, &sky->day, COLOR_DAY);

    float mid = (R_BAND_IN + R_BAND_OUT) / 2.0f;
    if(!isnan(sky->sunrise)) draw_dot(layer, polar(&coords, sky->sunrise, mid), 5, COLOR_SUN_MARK);
    if(!isnan(sky->sunset)) draw_dot(layer, polar(&coords, sky->sunset, mid), 5, COLOR_SUN_MARK);
}

/*********************
 *  MOON LAYER
 *********************/
/* Phase disc: dark disc, then the lit part row by row. Each row is lit from
 * the terminator (an ellipse, x = w*cos(phase)) to the limb. */
static void draw_moon_disc(lv_layer_t * layer, float cx, float cy, float r, float phase_deg, bool southern)
{
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.radius = LV_RADIUS_CIRCLE;
    disc.bg_color = COLOR_MOON_DARK;
    lv_area_t area = {(int32_t)(cx - r), (int32_t)(cy - r), (int32_t)(cx + r), (int32_t)(cy + r)};
    lv_draw_rect(layer, &disc, &area);

    float c = cosf(phase_deg * (float)M_PI / 180.0f);
    bool waxing = fmodf(phase_deg, 360.0f) < 180.0f;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COLOR_MOON_LIT;
    line.width = 1;

    for(float y = -r + 0.5f; y < r; y += 1.0f) {
        float w = sqrtf(r * r - y * y);
        /* Northern hemisphere: waxing moon is lit on the right */
        float left = waxing ? w * c : -w;
        float right = waxing ? w : -w * c;
        if(southern) {
            float t = left;
            left = -right;
            right = -t;
        }
        if(right - left < 0.5f) continue;
        line.p1.x = cx + left;
        line.p1.y = cy + y;
        line.p2.x = cx + right;
        line.p2.y = cy + y;
        lv_draw_line(layer, &line);
    }
}

static void moon_draw_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    if(dial.sky != NULL) {
        const sky_day_t * sky = dial.sky;
        draw_band(layer, &coords, 0.0f, MINUTES_PER_DAY, COLOR_NIGHT);
        draw_spans(layer, &coords, &sky->moon_up, COLOR_MOON_UP);

        float mid = (R_BAND_IN + R_BAND_OUT) / 2.0f;
        if(!isnan(sky->moonrise)) draw_dot(layer, polar(&coords, sky->moonrise, mid), 5, COLOR_MOON_MARK);
        if(!isnan(sky->moonset)) draw_dot(layer, polar(&coords, sky->moonset, mid), 5, COLOR_MOON_MARK);
    }

    if(dial.moon != NULL) {
        draw_moon_disc(layer, coords.x1 + DIAL_SIZE / 2.0f, coords.y1 + DIAL_SIZE / 2.0f - 88.0f, 20.0f,
                       dial.moon->phase_deg, dial.southern);
    }
}

/*********************
 *  WEATHER LAYER
 *********************/
static bool is_night(float minute)
{
    float m = fmodf(minute, MINUTES_PER_DAY);
    if(dial.sky == NULL) return m < 6 * 60 || m >= 18 * 60;
    for(uint8_t i = 0; i < dial.sky->day.count; i++) {
        if(m >= dial.sky->day.span[i].start && m < dial.sky->day.span[i].end) return false;
    }
    return true;
}

static lv_color_t condition_color(wx_kind_t kind)
{
    return kind == WX_CLEAR || kind == WX_PARTLY_CLOUDY ? COLOR_WX_CLEAR : COLOR_WX_CLOUD;
}

static bool is_snowy(wx_kind_t kind)
{
    return kind == WX_LIGHT_SNOW || kind == WX_SNOW || kind == WX_FREEZING;
}

static void draw_ring_arc(lv_layer_t * layer, const lv_area_t * coords, float start, float end,
                          int32_t r_out, int32_t width, lv_color_t color)
{
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = coords->x1 + DIAL_SIZE / 2;
    arc.center.y = coords->y1 + DIAL_SIZE / 2;
    arc.radius = r_out;
    arc.width = width;
    arc.color = color;
    if(end - start >= MINUTES_PER_DAY - 0.5f) {
        arc.start_angle = 0; /* equal angles would draw nothing */
        arc.end_angle = 360;
    }
    else {
        arc.start_angle = minute_to_deg(start);
        arc.end_angle = minute_to_deg(end);
    }
    lv_draw_arc(layer, &arc);
}

static void draw_icon(lv_layer_t * layer, const lv_image_dsc_t * img, lv_point_precise_t c)
{
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = img;
    int32_t x = (int32_t)lroundf(c.x) - img->header.w / 2;
    int32_t y = (int32_t)lroundf(c.y) - img->header.h / 2;
    lv_area_t area = {x, y, x + img->header.w - 1, y + img->header.h - 1};
    lv_draw_image(layer, &dsc, &area);
}

static float wind_radius(float kt)
{
    if(isnan(kt) || kt < 0.0f) kt = 0.0f;
    if(kt > WIND_FULL_KT) kt = WIND_FULL_KT;
    return R_WIND_MIN + kt / WIND_FULL_KT * (R_WIND_MAX - R_WIND_MIN);
}

/* Wind speed at any minute today, interpolated between hourly values (each hour's value sits at its start) */
static float wind_at(const weather_t * wx, float minute)
{
    for(uint8_t i = 0; i + 1 < wx->hour_count; i++) {
        const weather_hour_t * a = &wx->hours[i];
        const weather_hour_t * b = &wx->hours[i + 1];
        if(minute >= a->minute && minute <= b->minute) {
            float f = (minute - a->minute) / (b->minute - a->minute);
            return a->wind_kt + (b->wind_kt - a->wind_kt) * f;
        }
    }
    return wx->hour_count ? wx->hours[wx->hour_count - 1].wind_kt : NAN;
}

/* A filled arrowhead (a dart with a notched back) centred on `p`, pointing
 * toward `toward_deg` (degrees true, north up the screen) */
static void draw_dart(lv_layer_t * layer, lv_point_precise_t p, uint16_t toward_deg, lv_color_t color)
{
    float to = (float)(toward_deg % 360) * (float)M_PI / 180.0f;
    float dx = sinf(to), dy = -cosf(to); /* unit vector on screen */
    float nx = -dy, ny = dx;             /* perpendicular */
    const float len = 12.0f, half = 6.0f, notch = 4.0f;

    /* Centre the dart on the curve */
    lv_point_precise_t tip = {p.x + dx * len * 0.5f, p.y + dy * len * 0.5f};
    lv_point_precise_t back = {tip.x - dx * len, tip.y - dy * len};
    lv_point_precise_t left = {back.x + nx * half, back.y + ny * half};
    lv_point_precise_t right = {back.x - nx * half, back.y - ny * half};
    lv_point_precise_t mid = {back.x + dx * notch, back.y + dy * notch};

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.color = color;
    tri.p[0] = tip;
    tri.p[1] = left;
    tri.p[2] = mid;
    lv_draw_triangle(layer, &tri);
    tri.p[1] = mid;
    tri.p[2] = right;
    lv_draw_triangle(layer, &tri);
}

/* Weathergraph-style wind: a dashed red line whose distance from the center is
 * the speed, with chevrons along it showing direction */
static void draw_wind(lv_layer_t * layer, const lv_area_t * coords, const weather_t * wx)
{
    if(wx->hour_count < 2) return;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COLOR_WIND;
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;

    /* Walk the curve in small steps and lay dashes along its length, so the
     * pattern stays even however the curve bends */
    const float step = 2.0f; /* minutes */
    lv_point_precise_t prev = polar(coords, 0.0f, wind_radius(wind_at(wx, 0.0f)));
    float along = 0.0f;
    bool drawing = true;
    lv_point_precise_t dash_start = prev;
    for(float m = step; m <= MINUTES_PER_DAY; m += step) {
        lv_point_precise_t p = polar(coords, m, wind_radius(wind_at(wx, m)));
        along += sqrtf((p.x - prev.x) * (p.x - prev.x) + (p.y - prev.y) * (p.y - prev.y));
        if(drawing && along >= WIND_DASH_PX) {
            line.p1 = dash_start;
            line.p2 = p;
            lv_draw_line(layer, &line);
            drawing = false;
            along = 0.0f;
        }
        else if(!drawing && along >= WIND_GAP_PX) {
            drawing = true;
            dash_start = p;
            along = 0.0f;
        }
        prev = p;
    }

    for(uint8_t i = 0; i < wx->hour_count; i++) {
        const weather_hour_t * h = &wx->hours[i];
        if(h->minute >= MINUTES_PER_DAY) break;
        if(((int)h->minute / 60) % WIND_CARET_EVERY != 0 || isnan(h->wind_kt)) continue;
        /* Point downwind: the way the wind blows */
        draw_dart(layer, polar(coords, h->minute, wind_radius(h->wind_kt)), (uint16_t)((h->wind_from_deg + 180) % 360),
                  COLOR_WIND);
    }
}

static void format_temp(char * buf, size_t len, float temp_f, bool with_unit);
static int wind_in_unit(float kt);
static const char * wind_unit_name(void);

/* Temperature at any minute, interpolated between hourly values */
static float temp_at(const weather_t * wx, float minute)
{
    for(uint8_t i = 0; i + 1 < wx->hour_count; i++) {
        const weather_hour_t * a = &wx->hours[i];
        const weather_hour_t * b = &wx->hours[i + 1];
        if(minute >= a->minute && minute <= b->minute) {
            return a->temp_f + (b->temp_f - a->temp_f) * (minute - a->minute) / (b->minute - a->minute);
        }
    }
    return NAN;
}

/* Minutes along the ring that move a mark WX_ICON_TEMP_GAP px at the icon
 * radius: icon one side of the block's centre, temperature the other */
static float wx_side_offset(void)
{
    return WX_ICON_TEMP_GAP / (2.0f * (float)M_PI * R_WX_ICON) * MINUTES_PER_DAY;
}

static void weather_draw_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    draw_band(layer, &coords, 0.0f, MINUTES_PER_DAY, COLOR_NIGHT);

    const weather_t * wx = dial.wx;
    if(wx == NULL) return;

    /* Sky colour across the outer band. Runs of equal colour are drawn as one
     * arc: anti-aliased arc ends would otherwise leave seams between hours. */
    float run_start = 0.0f;
    for(uint8_t i = 0; i < wx->hour_count && wx->hours[i].minute < MINUTES_PER_DAY; i++) {
        const weather_hour_t * h = &wx->hours[i];
        bool last = i + 1 >= wx->hour_count || wx->hours[i + 1].minute >= MINUTES_PER_DAY;
        lv_color_t color = condition_color(weather_kind(h->code));
        if(last || !lv_color_eq(color, condition_color(weather_kind(wx->hours[i + 1].code)))) {
            draw_ring_arc(layer, &coords, run_start, h->minute + 60.0f, R_BAND_OUT, R_BAND_OUT - R_WX_RAIN_OUT, color);
            run_start = h->minute + 60.0f;
        }
    }

    /* Rain chance as inner bars, and a warning rim on stormy hours */
    for(uint8_t i = 0; i < wx->hour_count; i++) {
        const weather_hour_t * h = &wx->hours[i];
        if(h->minute >= MINUTES_PER_DAY) break;
        float start = h->minute;
        float end = h->minute + 60.0f;
        wx_kind_t kind = weather_kind(h->code);

        if(h->precip_prob > 0) {
            int32_t len = (R_WX_RAIN_OUT - R_BAND_IN) * h->precip_prob / 100;
            if(len > 0) {
                draw_ring_arc(layer, &coords, start, end, R_BAND_IN + len, len,
                              is_snowy(kind) ? COLOR_WX_SNOW : COLOR_WX_RAIN);
            }
        }

    }

    /* Warning rim over stormy hours, merged into runs like the sky colour */
    float storm_start = -1.0f;
    for(uint8_t i = 0; i < wx->hour_count && wx->hours[i].minute < MINUTES_PER_DAY; i++) {
        const weather_hour_t * h = &wx->hours[i];
        bool stormy = weather_hour_is_stormy(h);
        bool last = i + 1 >= wx->hour_count || wx->hours[i + 1].minute >= MINUTES_PER_DAY;
        if(stormy && storm_start < 0.0f) storm_start = h->minute;
        if(storm_start >= 0.0f && (last || !stormy || !weather_hour_is_stormy(&wx->hours[i + 1]))) {
            float end = stormy ? h->minute + 60.0f : h->minute;
            draw_ring_arc(layer, &coords, storm_start, end, R_BAND_OUT, R_WX_STORM_W, COLOR_WX_STORM);
            storm_start = -1.0f;
        }
    }

    /* One icon per 3-hour block, showing the worst hour in it */
    for(int block = 0; block < 8; block++) {
        const weather_hour_t * worst = NULL;
        for(int k = 0; k < 3; k++) {
            const weather_hour_t * h = weather_hour_at(wx, (float)((block * 3 + k) * 60));
            if(h != NULL && (worst == NULL || weather_severity(h) > weather_severity(worst))) worst = h;
        }
        if(worst == NULL) continue;
        float mid = (float)(block * 180 + 90);
        draw_icon(layer, weather_icon(weather_kind(worst->code), is_night(mid), false),
                  polar(&coords, mid - wx_side_offset(), R_WX_ICON));
    }

    /* Current conditions above the time */
    /* Wind on top of the icons so its line stays continuous */
    draw_wind(layer, &coords, wx);

    /* For each icon's 3 hours: the temperature beside the icon (side by side
     * works at every angle; stacked, they collide at 3 and 9 o'clock) and the
     * wind speed further in, in the wind line's red. Both curves can then be
     * read around the dial, and the temperature only meets the wind line in
     * a near gale. */
    for(int block = 0; block < 8; block++) {
        float mid = (float)(block * 180 + 90);
        char buf[16];
        float t = temp_at(wx, mid);
        if(!isnan(t)) {
            format_temp(buf, sizeof(buf), t, false);
            draw_tag(layer, polar(&coords, mid + wx_side_offset(), R_WX_ICON), buf, &lv_font_montserrat_14, COLOR_TEXT);
        }
        float w = wind_at(wx, mid);
        if(!isnan(w)) {
            snprintf(buf, sizeof(buf), "%d %s", wind_in_unit(w), wind_unit_name());
            draw_tag(layer, polar(&coords, mid, R_WX_WIND_LABEL), buf, &lv_font_montserrat_12, COLOR_WIND);
        }
    }

    /* Same "now" as the readout's words, so icon and text agree */
    float now = (float)dial.minute_of_day;
    draw_icon(layer, weather_icon(weather_kind(weather_code_now(wx, now)), is_night(now), true),
              (lv_point_precise_t){coords.x1 + DIAL_SIZE / 2.0f, coords.y1 + DIAL_SIZE / 2.0f - 80.0f});
}

/*********************
 *  CURRENT LAYER
 *********************/
/* Knots that reach the edge of the band: today's strongest, rounded up to a half knot */
static float current_scale(const current_day_t * c)
{
    float s = ceilf(c->max_abs_kt * 2.0f) / 2.0f;
    return s < 1.0f ? 1.0f : s;
}

static float current_radius(const current_day_t * c, float kt)
{
    float f = kt / current_scale(c);
    if(f > 1.0f) f = 1.0f;
    if(f < -1.0f) f = -1.0f;
    return R_SLACK + f * R_CURRENT_SWING;
}

static void current_draw_cb(lv_event_t * e)
{
    const current_day_t * c = dial.current;
    if(c == NULL || c->count < 2) return;

    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    float now = (float)dial.minute_of_day;

    /* Fill from the slack circle to the curve with overlapping spokes, as for
     * the tide: flood outward, ebb inward, past dimmed */
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = (int32_t)ceilf(2.0f * (float)M_PI * R_BAND_OUT * c->interval_min / MINUTES_PER_DAY + 1.5f);
    for(uint16_t i = 0; i < c->count; i++) {
        float m = (float)(i * c->interval_min);
        float v = c->speed_kt[i];
        bool past = m < now;
        line.color = v >= 0.0f ? (past ? COLOR_FLOOD_PAST : COLOR_FLOOD) : (past ? COLOR_EBB_PAST : COLOR_EBB);
        float r = current_radius(c, v);
        /* Stop short of the line so the square spoke ends hide under it */
        float r_end = r + (v >= 0.0f ? -1.5f : 1.5f);
        if(fabsf(r_end - R_SLACK) < 0.5f) continue;
        line.p1 = polar(&coords, m, R_SLACK);
        line.p2 = polar(&coords, m, r_end);
        lv_draw_line(layer, &line);
    }

    /* Speed scale: rings at round speeds on both sides of slack */
    float scale = current_scale(c);
    float step = curve_nice_step(scale, 3);
    if(step > 0.0f) {
        for(float v = step; v <= scale + 1e-3f; v += step) {
            draw_scale_ring(layer, &coords, current_radius(c, v));
            draw_scale_ring(layer, &coords, current_radius(c, -v));
        }
    }

    /* Slack circle and the speed line */
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = coords.x1 + DIAL_SIZE / 2;
    arc.center.y = coords.y1 + DIAL_SIZE / 2;
    arc.radius = (uint16_t)R_SLACK;
    arc.width = 1;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.color = COLOR_TICK_HOUR;
    lv_draw_arc(layer, &arc);

    lv_draw_line_dsc_init(&line);
    line.color = COLOR_TIDE_LINE;
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;
    for(uint16_t i = 0; i + 1 < c->count; i++) {
        line.p1 = polar(&coords, (float)(i * c->interval_min), current_radius(c, c->speed_kt[i]));
        line.p2 = polar(&coords, (float)((i + 1) * c->interval_min), current_radius(c, c->speed_kt[i + 1]));
        lv_draw_line(layer, &line);
    }

    /* Slack: dots on the circle. Max flood and ebb: darts pointing where the water goes. */
    for(uint8_t i = 0; i < c->event_count; i++) {
        const current_event_t * ev = &c->events[i];
        if(ev->kind == CURRENT_SLACK) {
            draw_dot(layer, polar(&coords, ev->minute, R_SLACK), 4, COLOR_SLACK);
        }
        else {
            bool flood = ev->kind == CURRENT_FLOOD;
            draw_dart(layer, polar(&coords, ev->minute, current_radius(c, ev->speed_kt)),
                      flood ? c->flood_dir : c->ebb_dir, flood ? COLOR_FLOOD_DART : COLOR_EBB_DART);
        }
    }

    draw_dot(layer, polar(&coords, now, current_radius(c, current_speed_at(c, now))), 6, COLOR_HAND);

    /* Labels last. Scale where no event is (flood outward, ebb inward), then
     * each maximum with its time and speed, inside its own bulge, and each
     * slack with its time just off the slack circle. */
    float busy[CURRENT_MAX_EVENTS];
    for(uint8_t i = 0; i < c->event_count; i++) busy[i] = c->events[i].minute;
    float label_at = quiet_minute(busy, c->event_count);
    if(step > 0.0f) {
        for(float v = step; v <= scale + 1e-3f; v += step) {
            /* Unit only on the outermost ring each side, to keep the stack narrow */
            char val[12], buf[16];
            format_scale_value(val, sizeof(val), v, step);
            snprintf(buf, sizeof(buf), v + step > scale + 1e-3f ? "%s kt" : "%s", val);
            draw_scale_label(layer, &coords, label_at, current_radius(c, v), buf, COLOR_FLOOD_DART);
            draw_scale_label(layer, &coords, label_at, current_radius(c, -v), buf, COLOR_EBB_DART);
        }
    }
    for(uint8_t i = 0; i < c->event_count; i++) {
        const current_event_t * ev = &c->events[i];
        char t[8], buf[24];
        format_hhmm(t, sizeof(t), ev->minute);
        if(ev->kind == CURRENT_SLACK) {
            draw_tag(layer, polar(&coords, ev->minute, R_SLACK + 13.0f), t, &lv_font_montserrat_12, COLOR_SLACK);
            continue;
        }
        bool flood = ev->kind == CURRENT_FLOOD;
        int tenths = (int)lroundf(fabsf(ev->speed_kt) * 10.0f);
        snprintf(buf, sizeof(buf), "%s\n%d.%d kt", t, tenths / 10, tenths % 10);
        float r = current_radius(c, ev->speed_kt) + (flood ? -20.0f : 20.0f);
        draw_tag(layer, polar(&coords, ev->minute, r), buf, &lv_font_montserrat_12,
                 flood ? COLOR_FLOOD_DART : COLOR_EBB_DART);
    }
}

/*********************
 *  HAND
 *********************/
static void hand_draw_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COLOR_HAND;
    line.width = 3;
    line.round_start = 1;
    line.round_end = 1;
    /* Two segments with a gap at the numeral ring so the hand never hides a number */
    float m = (float)dial.minute_of_day;
    line.p1 = polar(&coords, m, R_HAND_IN);
    line.p2 = polar(&coords, m, R_BAND_OUT + 4);
    lv_draw_line(layer, &line);

    line.p1 = polar(&coords, m, R_TICK_MAJOR - 2);
    line.p2 = polar(&coords, m, R_TICK_OUT);
    lv_draw_line(layer, &line);
}

/*********************
 *  CENTER READOUT
 *********************/
static void tide_summary(char * buf, size_t len)
{
    const tide_day_t * day = dial.tide;
    if(day == NULL || day->count < 2) {
        snprintf(buf, len, "No tide station set");
        return;
    }
    float now = (float)dial.minute_of_day;

    if(dial.detail) {
        /* Next two turns of the tide */
        const tide_event_t * ev = tide_next_event(day, now);
        size_t used = 0;
        for(int n = 0; n < 2 && ev != NULL && ev < day->events + day->event_count; n++, ev++) {
            char t[8], h[16];
            format_hhmm(t, sizeof(t), ev->minute);
            format_height(h, sizeof(h), ev->height_ft);
            used += (size_t)snprintf(buf + used, len - used, "%s%s %s  %s",
                                     n ? "\n" : "", ev->high ? "High" : "Low", t, h);
            if(used >= len) break;
        }
        if(used == 0) snprintf(buf, len, "No more turns today");
    }
    else {
        char h[16];
        format_height(h, sizeof(h), tide_height_at(day, now));
        bool rising = tide_height_at(day, now + 1.0f) >= tide_height_at(day, now);
        snprintf(buf, len, "%s  %s", rising ? "Rising" : "Falling", h);
    }
}

static float spans_total(const sky_spans_t * spans)
{
    float total = 0.0f;
    for(uint8_t i = 0; i < spans->count; i++) total += spans->span[i].end - spans->span[i].start;
    return total;
}

static void sun_summary(char * buf, size_t len)
{
    const sky_day_t * sky = dial.sky;
    if(sky == NULL) {
        snprintf(buf, len, "No location set");
        return;
    }

    if(dial.detail) {
        int daylight_min = (int)lroundf(spans_total(&sky->day));
        char dawn[8], dusk[8];
        if(sky->civil.count > 0) {
            format_hhmm_or_dash(dawn, sizeof(dawn), sky->civil.span[0].start);
            format_hhmm_or_dash(dusk, sizeof(dusk), sky->civil.span[sky->civil.count - 1].end);
        }
        else {
            format_hhmm_or_dash(dawn, sizeof(dawn), NAN);
            format_hhmm_or_dash(dusk, sizeof(dusk), NAN);
        }
        snprintf(buf, len, "Daylight %dh %02dm\nCivil %s - %s", daylight_min / 60, daylight_min % 60, dawn, dusk);
    }
    else if(isnan(sky->sunrise) && isnan(sky->sunset)) {
        snprintf(buf, len, sky->day.count > 0 ? "Sun up all day" : "Sun down all day");
    }
    else {
        char rise[8], set[8];
        format_hhmm_or_dash(rise, sizeof(rise), sky->sunrise);
        format_hhmm_or_dash(set, sizeof(set), sky->sunset);
        snprintf(buf, len, "Rise %s  Set %s", rise, set);
    }
}

static void moon_summary(char * buf, size_t len)
{
    if(dial.moon == NULL) {
        snprintf(buf, len, "No location set");
        return;
    }

    if(dial.detail) {
        static const char * const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char rise[8], set[8];
        format_hhmm_or_dash(rise, sizeof(rise), dial.sky ? dial.sky->moonrise : NAN);
        format_hhmm_or_dash(set, sizeof(set), dial.sky ? dial.sky->moonset : NAN);

        struct tm lt;
        time_t when = dial.moon->next[0].when;
        localtime_r(&when, &lt);
        snprintf(buf, len, "Rise %s  Set %s\n%s %s %d", rise, set,
                 sky_quarter_name(dial.moon->next[0].quarter), months[lt.tm_mon], lt.tm_mday);
    }
    else {
        snprintf(buf, len, "%s  %d%%", sky_phase_name(dial.moon->phase_deg),
                 (int)lroundf(dial.moon->illumination * 100.0f));
    }
}

/* Whole degrees in the display unit, e.g. "68°F" or "20°" */
static void format_temp(char * buf, size_t len, float temp_f, bool with_unit)
{
    if(isnan(temp_f)) {
        snprintf(buf, len, "--");
        return;
    }
    bool c = dial.temp_unit == TEMP_CELSIUS;
    int v = (int)lroundf(c ? (temp_f - 32.0f) * 5.0f / 9.0f : temp_f);
    snprintf(buf, len, "%d\xC2\xB0%s", v, with_unit ? (c ? "C" : "F") : "");
}

static int wind_in_unit(float kt)
{
    float f = dial.wind_unit == WIND_MPH ? 1.15078f : dial.wind_unit == WIND_KMH ? 1.852f : 1.0f;
    return (int)lroundf(kt * f);
}

static const char * wind_unit_name(void)
{
    return dial.wind_unit == WIND_MPH ? "mph" : dial.wind_unit == WIND_KMH ? "km/h" : "kt";
}

/* "Rain from 15:00", "Rain until 19:00" or "No rain next 12h" */
static void format_rain_outlook(char * buf, size_t len)
{
    const weather_t * wx = dial.wx;
    float now = (float)dial.minute_of_day;
    const weather_hour_t * cur = weather_hour_at(wx, now);
    bool wet_now = cur != NULL && weather_hour_is_wet(cur);

    for(uint8_t i = 0; i < wx->hour_count; i++) {
        const weather_hour_t * h = &wx->hours[i];
        if(h->minute + 60.0f <= now) continue;
        if(h->minute > now + 12 * 60) break;
        bool wet = weather_hour_is_wet(h);
        if(wet != wet_now) {
            char t[8];
            format_hhmm(t, sizeof(t), h->minute);
            bool snow = is_snowy(weather_kind(wet ? h->code : cur->code));
            snprintf(buf, len, "%s %s %s", snow ? "Snow" : "Rain", wet ? "from" : "until", t);
            return;
        }
    }
    snprintf(buf, len, wet_now ? "Wet next 12h" : "No rain next 12h");
}

static void weather_summary(char * buf, size_t len)
{
    const weather_t * wx = dial.wx;
    if(wx == NULL) {
        snprintf(buf, len, "No weather data");
        return;
    }

    if(dial.detail) {
        char hi[12], lo[12], rain[32];
        format_temp(hi, sizeof(hi), wx->today_high_f, false);
        format_temp(lo, sizeof(lo), wx->today_low_f, false);
        format_rain_outlook(rain, sizeof(rain));
        snprintf(buf, len, "High %s  Low %s\n%s", hi, lo, rain);
        return;
    }

    /* Line 1: what's happening and what changes next. The long form unless it
     * would spill outside the inner circle. */
    char text[48], short_text[32];
    weather_outlook(wx, (float)dial.minute_of_day, text, sizeof(text), short_text, sizeof(short_text));
    lv_point_t size;
    lv_text_get_size(&size, text, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const char * outlook = size.x <= SUMMARY_MAX_W ? text : short_text;

    /* Line 2: temperature and wind */
    char temp[12];
    format_temp(temp, sizeof(temp), wx->current.temp_f, true);
    if(isnan(wx->current.wind_kt)) {
        snprintf(buf, len, "%s\n%s", outlook, temp);
        return;
    }
    int wind = wind_in_unit(wx->current.wind_kt);
    int gust = isnan(wx->current.gust_kt) ? wind : wind_in_unit(wx->current.gust_kt);
    const char * from = weather_compass(wx->current.wind_from_deg);
    /* Mention gusts only when they're meaningfully above the mean */
    if(gust >= wind + 5) {
        snprintf(buf, len, "%s\n%s  %s %d G%d %s", outlook, temp, from, wind, gust, wind_unit_name());
    }
    else {
        snprintf(buf, len, "%s\n%s  %s %d %s", outlook, temp, from, wind, wind_unit_name());
    }
}

/* "Ebb 2.1 kt, 183°" or "Slack" */
static void format_current_state(char * buf, size_t len, const current_day_t * c, float minute)
{
    float v = current_speed_at(c, minute);
    if(fabsf(v) < CURRENT_SLACK_KT) {
        snprintf(buf, len, "Slack");
        return;
    }
    int tenths = (int)lroundf(fabsf(v) * 10.0f);
    snprintf(buf, len, "%s %d.%d kt, %03u\xC2\xB0", v > 0.0f ? "Flood" : "Ebb", tenths / 10, tenths % 10,
             (unsigned)(v > 0.0f ? c->flood_dir : c->ebb_dir));
}

static void format_current_event(char * buf, size_t len, const current_event_t * ev)
{
    char t[8];
    format_hhmm(t, sizeof(t), ev->minute);
    if(ev->kind == CURRENT_SLACK) {
        snprintf(buf, len, "Slack %s", t);
        return;
    }
    int tenths = (int)lroundf(fabsf(ev->speed_kt) * 10.0f);
    snprintf(buf, len, "Max %s %s  %d.%d kt", ev->kind == CURRENT_FLOOD ? "flood" : "ebb", t, tenths / 10,
             tenths % 10);
}

static void current_summary(char * buf, size_t len)
{
    const current_day_t * c = dial.current;
    if(c == NULL || c->count < 2) {
        snprintf(buf, len, "No current station set");
        return;
    }
    float now = (float)dial.minute_of_day;
    char a[40] = "", b[40] = "";

    if(dial.detail) {
        /* The next two events of any kind */
        const current_event_t * ev = current_next_event(c, now, -1);
        if(ev) format_current_event(a, sizeof(a), ev);
        if(ev) ev = current_next_event(c, ev->minute, -1);
        if(ev) format_current_event(b, sizeof(b), ev);
        if(a[0] == '\0') snprintf(a, sizeof(a), "No more changes today");
    }
    else {
        /* Now, then the next slack (or, at slack, the next maximum) */
        format_current_state(a, sizeof(a), c, now);
        bool slack_now = fabsf(current_speed_at(c, now)) < CURRENT_SLACK_KT;
        const current_event_t * ev = slack_now ? current_next_event(c, now + 30.0f, -1)
                                               : current_next_event(c, now, CURRENT_SLACK);
        if(ev) format_current_event(b, sizeof(b), ev);
    }
    snprintf(buf, len, b[0] ? "%s\n%s" : "%s", a, b);
}

static void update_summary(void)
{
    char buf[96] = "";
    switch(dial.layer) {
        case DIAL_LAYER_TIDE: tide_summary(buf, sizeof(buf)); break;
        case DIAL_LAYER_SUN: sun_summary(buf, sizeof(buf)); break;
        case DIAL_LAYER_MOON: moon_summary(buf, sizeof(buf)); break;
        case DIAL_LAYER_WEATHER: weather_summary(buf, sizeof(buf)); break;
        case DIAL_LAYER_CURRENT: current_summary(buf, sizeof(buf)); break;
        default: break;
    }
    lv_label_set_text(dial.summary_label, buf);
}

/* Hundredths of a degree, as integers to stay clear of printf float support */
static void format_coord(char * buf, size_t len, double deg, char pos, char neg)
{
    long h = lround(fabs(deg) * 100.0);
    snprintf(buf, len, "%ld.%02ld\xC2\xB0%c", h / 100, h % 100, deg < 0.0 ? neg : pos);
}

/* Tide and current come from their stations; everything else from the configured coordinates */
static void update_source(void)
{
    char buf[48] = "";

    if(dial.layer == DIAL_LAYER_TIDE) {
        snprintf(buf, sizeof(buf), "%s", dial.station_name);
    }
    else if(dial.layer == DIAL_LAYER_CURRENT) {
        snprintf(buf, sizeof(buf), "%s", dial.current_station_name);
    }
    else if(dial.place_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", dial.place_name);
    }
    else if(!isnan(dial.latitude) && !isnan(dial.longitude)) {
        char lat[16], lon[16];
        format_coord(lat, sizeof(lat), dial.latitude, 'N', 'S');
        format_coord(lon, sizeof(lon), dial.longitude, 'E', 'W');
        snprintf(buf, sizeof(buf), "%s %s", lat, lon);
    }

    lv_label_set_text(dial.source_label, buf);
}

static void update_center(void)
{
    lv_label_set_text_fmt(dial.time_label, "%02d:%02d", dial.minute_of_day / 60, dial.minute_of_day % 60);
    /* The weather icon takes the layer name's place */
    bool icon_shown = dial.layer == DIAL_LAYER_WEATHER && dial.wx != NULL;
    lv_label_set_text(dial.layer_label, icon_shown ? "" : layer_names[dial.layer]);
    update_summary();
    update_source();
}

/*********************
 *  INPUT
 *********************/
static void input_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    /* Long press: the setup card. While it's up, any press or turn closes it. */
    if(code == LV_EVENT_LONG_PRESSED) {
        setup_card_show(!setup_card_visible());
        return;
    }
    if(setup_card_visible()) {
        if(code == LV_EVENT_KEY || code == LV_EVENT_SHORT_CLICKED) setup_card_show(false);
        return;
    }

    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_RIGHT || key == LV_KEY_DOWN) dial_step_layer(+1);
        else if(key == LV_KEY_LEFT || key == LV_KEY_UP) dial_step_layer(-1);
    }
    else if(code == LV_EVENT_SHORT_CLICKED) {
        /* Not CLICKED: LVGL sends that after a long press too */
        dial_toggle_detail();
    }
}

/*********************
 *  PUBLIC API
 *********************/
void dial_create(lv_obj_t * parent)
{
    dial.top_hour = 12;
    for(int i = 0; i < DIAL_LAYER_COUNT; i++) dial.order[i] = (dial_layer_t)i;
    dial.order_count = DIAL_LAYER_COUNT;
    dial.latitude = NAN;
    dial.longitude = NAN;

    dial.face = lv_obj_create(parent);
    lv_obj_remove_style_all(dial.face); /* no theme focus outline when the encoder focuses it */
    lv_obj_set_size(dial.face, DIAL_SIZE, DIAL_SIZE);
    lv_obj_center(dial.face);
    lv_obj_set_style_radius(dial.face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial.face, COLOR_FACE, 0);
    lv_obj_set_style_bg_opa(dial.face, LV_OPA_COVER, 0);
    /* Scrollable objects swallow encoder presses to toggle edit mode */
    lv_obj_remove_flag(dial.face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dial.face, face_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(dial.face, input_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(dial.face, input_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(dial.face, input_cb, LV_EVENT_LONG_PRESSED, NULL);

    for(int i = 0; i < DIAL_LAYER_COUNT; i++) {
        dial.layers[i] = create_plain_layer(dial.face);
        lv_obj_add_flag(dial.layers[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_event_cb(dial.layers[DIAL_LAYER_TIDE], tide_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(dial.layers[DIAL_LAYER_SUN], sun_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(dial.layers[DIAL_LAYER_MOON], moon_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(dial.layers[DIAL_LAYER_WEATHER], weather_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(dial.layers[DIAL_LAYER_CURRENT], current_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    dial.hand = create_plain_layer(dial.face);
    lv_obj_add_event_cb(dial.hand, hand_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    dial.layer_label = lv_label_create(dial.face);
    lv_obj_set_style_text_font(dial.layer_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dial.layer_label, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_letter_space(dial.layer_label, 3, 0);
    lv_obj_align(dial.layer_label, LV_ALIGN_CENTER, 0, -52);

    dial.time_label = lv_label_create(dial.face);
    lv_obj_set_style_text_font(dial.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(dial.time_label, COLOR_TEXT, 0);
    lv_obj_align(dial.time_label, LV_ALIGN_CENTER, 0, -8);

    dial.summary_label = lv_label_create(dial.face);
    lv_obj_set_style_text_font(dial.summary_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dial.summary_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_align(dial.summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dial.summary_label, LV_ALIGN_TOP_MID, 0, DIAL_SIZE / 2 + 26);

    dial.source_label = lv_label_create(dial.face);
    lv_label_set_text(dial.source_label, "");
    lv_label_set_long_mode(dial.source_label, LV_LABEL_LONG_MODE_DOTS);
    /* Fixed single-line height, otherwise LVGL wraps instead of adding dots */
    lv_obj_set_size(dial.source_label, 160, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_obj_set_style_text_font(dial.source_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dial.source_label, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(dial.source_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dial.source_label, LV_ALIGN_TOP_MID, 0, DIAL_SIZE / 2 + 66);

    /* Last child, so it covers everything when shown */
    setup_card_create(dial.face);

    dial.layer = DIAL_LAYER_TIDE;
    lv_obj_remove_flag(dial.layers[dial.layer], LV_OBJ_FLAG_HIDDEN);
    update_center();
}

void dial_attach_group(lv_group_t * group)
{
    lv_group_add_obj(group, dial.face);
    lv_group_focus_obj(dial.face);
    lv_group_set_editing(group, true);
}

void dial_set_time(int hour, int minute)
{
    int m = hour * 60 + minute;
    if(m == dial.minute_of_day) return;
    dial.minute_of_day = m;

    lv_obj_invalidate(dial.hand);
    /* Tide dims the past and weather picks day/night icons, both by the time */
    if(dial.layer == DIAL_LAYER_TIDE || dial.layer == DIAL_LAYER_WEATHER || dial.layer == DIAL_LAYER_CURRENT) {
        lv_obj_invalidate(dial.layers[dial.layer]);
    }
    update_center();
}

void dial_set_top_hour(int hour)
{
    hour = ((hour % 24) + 24) % 24;
    if(hour == dial.top_hour) return;
    dial.top_hour = hour;
    lv_obj_invalidate(dial.face); /* covers the layers and hand too */
}

int dial_get_top_hour(void)
{
    return dial.top_hour;
}

void dial_set_metric(bool metric)
{
    dial.metric = metric;
    update_summary();
}

void dial_set_current_station_name(const char * name)
{
    snprintf(dial.current_station_name, sizeof(dial.current_station_name), "%s", name != NULL ? name : "");
    update_source();
}

void dial_set_current(const current_day_t * current)
{
    dial.current = current;
    lv_obj_invalidate(dial.layers[DIAL_LAYER_CURRENT]);
    update_summary();
}

void dial_set_station_name(const char * name)
{
    snprintf(dial.station_name, sizeof(dial.station_name), "%s", name != NULL ? name : "");
    update_source();
}

void dial_set_place(const char * name, double latitude, double longitude)
{
    snprintf(dial.place_name, sizeof(dial.place_name), "%s", name != NULL ? name : "");
    dial.latitude = latitude;
    dial.longitude = longitude;
    update_source();
}

void dial_set_sky(const sky_day_t * sky)
{
    dial.sky = sky;
    lv_obj_invalidate(dial.layers[DIAL_LAYER_SUN]);
    lv_obj_invalidate(dial.layers[DIAL_LAYER_MOON]);
    update_summary();
}

void dial_set_moon(const sky_moon_t * moon, bool southern)
{
    dial.moon = moon;
    dial.southern = southern;
    lv_obj_invalidate(dial.layers[DIAL_LAYER_MOON]);
    update_summary();
}

void dial_set_weather(const weather_t * wx)
{
    dial.wx = wx;
    lv_obj_invalidate(dial.layers[DIAL_LAYER_WEATHER]);
    update_center();
}

void dial_set_weather_units(temp_unit_t temp, wind_unit_t wind)
{
    dial.temp_unit = temp;
    dial.wind_unit = wind;
    update_summary();
}

void dial_set_tide(const tide_day_t * day)
{
    dial.tide = day;
    lv_obj_invalidate(dial.layers[DIAL_LAYER_TIDE]);
    update_summary();
}

void dial_set_layer(dial_layer_t layer)
{
    if(layer >= DIAL_LAYER_COUNT || layer == dial.layer) return;

    lv_obj_add_flag(dial.layers[dial.layer], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(dial.layers[layer], LV_OBJ_FLAG_HIDDEN);
    dial.layer = layer;
    dial.detail = false;
    update_center();
}

dial_layer_t dial_get_layer(void)
{
    return dial.layer;
}

void dial_set_layer_order(const dial_layer_t * order, int count)
{
    if(count < 1) return;
    if(count > DIAL_LAYER_COUNT) count = DIAL_LAYER_COUNT;
    bool current_kept = false;
    for(int i = 0; i < count; i++) {
        dial.order[i] = order[i];
        if(order[i] == dial.layer) current_kept = true;
    }
    dial.order_count = count;
    if(!current_kept) dial_set_layer(dial.order[0]);
}

void dial_step_layer(int dir)
{
    int pos = -1;
    for(int i = 0; i < dial.order_count; i++) {
        if(dial.order[i] == dial.layer) pos = i;
    }
    if(pos < 0) {
        dial_set_layer(dial.order[0]); /* shown directly but not in the order */
        return;
    }
    int next = (pos + dir) % dial.order_count;
    if(next < 0) next += dial.order_count;
    dial_set_layer(dial.order[next]);
}

void dial_set_setup_info(const setup_card_info_t * info)
{
    setup_card_set_info(info);
}

void dial_show_setup(bool show)
{
    setup_card_show(show);
}

void dial_toggle_detail(void)
{
    dial.detail = !dial.detail;
    update_summary();
}
