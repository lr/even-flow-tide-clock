/**
 * @file main.c
 * Desktop simulator for the Even Flow clock.
 *
 *   even_flow_sim                       interactive SDL window
 *   even_flow_sim --time 14:30          freeze the clock at a time of day
 *   even_flow_sim --date 2026-06-21     show a given local date
 *   even_flow_sim --layer 2             start on a given overlay
 *   even_flow_sim --top 0               put midnight at the top (overrides the config)
 *   even_flow_sim --detail              start with the center readout toggled
 *   even_flow_sim --setup               start with the setup card (QR code) open
 *   even_flow_sim --config my.conf      settings file (see below)
 *   even_flow_sim --weather wx.json     use a saved Open-Meteo response instead of fetching live
 *   even_flow_sim --tide t.json         use a saved NOAA tide response (likewise --current)
 *   even_flow_sim --snapshot out.bmp    render one frame headless and exit
 *
 * Settings come from --config, else even-flow.conf in the project root, else
 * sim/example.conf. The file is watched: save new settings (by hand or from the
 * settings page via tools/dev_server.py) and the clock updates within a second.
 *
 * Controls: mouse wheel or Left/Right = rotate knob, middle click or Enter = press knob.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lvgl.h"
#include "config/config.h"
#include "data/current.h"
#include "data/noaa.h"
#include "data/sky.h"
#include "data/tide.h"
#include "data/weather.h"
#include "ui/dial.h"

#define HOR_RES DIAL_SIZE
#define VER_RES DIAL_SIZE

#define LOCAL_CONFIG   EVEN_FLOW_SOURCE_DIR "/even-flow.conf" /* where tools/dev_server.py saves */
#define EXAMPLE_CONFIG EVEN_FLOW_SOURCE_DIR "/sim/example.conf"

typedef struct {
    int fixed_minute; /* -1 = follow the system clock */
    int fixed_year;   /* 0 = follow the system clock */
    int fixed_month;
    int fixed_day;
    int layer;        /* -1 = first in the configured order */
    int top_hour;     /* -1 = use the config */
    bool detail;
    bool setup;
    const char * config_path;
    const char * weather_path;
    const char * tide_path;
    const char * current_path;
    const char * snapshot_path;
} options_t;

static clock_config_t config;
static const char * watch_path;  /* settings file to watch for changes */
static time_t watch_mtime;
static tide_day_t tide_today;
static current_day_t current_today;
static sky_day_t sky_today;
static sky_moon_t moon_now;
static long data_day_number = -1; /* local day the tide and sky data were built for */
static int moon_minute = -1;      /* local minute the moon phase was computed for */
static weather_t weather;
static time_t weather_fetched = 0;

#define WEATHER_REFRESH_SECS (30 * 60)

typedef struct {
    time_t t; /* the instant being displayed */
    int year, month, day, hour, minute;
    long day_number; /* days since epoch of the local date */
} clock_now_t;

/*********************
 *  CONFIG
 *********************/
static int load_config(const char * path)
{
    FILE * f = fopen(path, "r");
    if(f == NULL) {
        perror(path);
        return -1;
    }
    char text[4096];
    size_t len = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[len] = '\0';

    char err[96];
    int errors = config_parse(&config, text, err, sizeof(err));
    if(errors > 0) fprintf(stderr, "%s: %d problem(s), first: %s\n", path, errors, err);
    printf("config: %s\n", path);
    return 0;
}

static char * read_file(const char * path);
static char * fetch_url(const char * url);

/*********************
 *  TIME AND DATA
 *********************/
static clock_now_t current_time(const options_t * opt)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt); /* honours TZ from the config */

    if(opt->fixed_year > 0) {
        lt.tm_year = opt->fixed_year - 1900;
        lt.tm_mon = opt->fixed_month - 1;
        lt.tm_mday = opt->fixed_day;
    }
    if(opt->fixed_minute >= 0) {
        lt.tm_hour = opt->fixed_minute / 60;
        lt.tm_min = opt->fixed_minute % 60;
        lt.tm_sec = 0;
    }
    if(opt->fixed_year > 0 || opt->fixed_minute >= 0) {
        lt.tm_isdst = -1;
        now = mktime(&lt);
    }

    clock_now_t c = {
        .t = now,
        .year = lt.tm_year + 1900,
        .month = lt.tm_mon + 1,
        .day = lt.tm_mday,
        .hour = lt.tm_hour,
        .minute = lt.tm_min,
    };
    /* Days since epoch of the *local* date, so the data rolls over at local midnight */
    struct tm midnight = {.tm_year = lt.tm_year, .tm_mon = lt.tm_mon, .tm_mday = lt.tm_mday};
    c.day_number = (long)(timegm(&midnight) / 86400);
    return c;
}

/* A saved file if given, else a live fetch. Caller frees. */
static char * load_or_fetch(const char * path, const char * url, const char ** source)
{
    *source = path ? path : "api.tidesandcurrents.noaa.gov";
    return path ? read_file(path) : fetch_url(url);
}

/* Predictions don't change, so NOAA is asked once a day. A failure shows no
 * data rather than anything invented. */
static void refresh_noaa(const options_t * opt, const clock_now_t * now)
{
    char url[512], err[96];
    const char * source;

    dial_set_tide(NULL);
    if(config_has_station(&config) &&
       (opt->tide_path || noaa_tide_url(url, sizeof(url), config.station_id, now->year, now->month, now->day))) {
        char * json = load_or_fetch(opt->tide_path, url, &source);
        if(json == NULL) fprintf(stderr, "tide: could not read %s\n", source);
        else if(!noaa_parse_tide(&tide_today, json, now->year, now->month, now->day, err, sizeof(err))) {
            fprintf(stderr, "tide: %s: %s\n", source, err);
        }
        else {
            dial_set_tide(&tide_today);
            printf("tide: %s, %d turns today\n", source, tide_today.event_count);
        }
        free(json);
    }

    dial_set_current(NULL);
    if(config_has_current_station(&config) &&
       (opt->current_path || noaa_current_url(url, sizeof(url), config.current_station_id, config.current_bin,
                                              now->year, now->month, now->day))) {
        char * json = load_or_fetch(opt->current_path, url, &source);
        if(json == NULL) fprintf(stderr, "current: could not read %s\n", source);
        else if(!noaa_parse_current(&current_today, json, now->year, now->month, now->day, err, sizeof(err))) {
            fprintf(stderr, "current: %s: %s\n", source, err);
        }
        else {
            dial_set_current(&current_today);
            printf("current: %s, %d events today\n", source, current_today.event_count);
        }
        free(json);
    }
}

/* Rebuild the once-a-day data when the local date changes */
static void refresh_day(const options_t * opt, const clock_now_t * now)
{
    if(now->day_number == data_day_number) return;
    data_day_number = now->day_number;

    refresh_noaa(opt, now);

    if(config_has_location(&config) &&
       sky_compute_day(&sky_today, config.latitude, config.longitude, now->year, now->month, now->day)) {
        dial_set_sky(&sky_today);
    }
    else {
        dial_set_sky(NULL);
    }
}

/* Moon phase drifts slowly; the board will do this every few minutes */
static void refresh_moon(const clock_now_t * now)
{
    int m = now->hour * 60 + now->minute;
    if(m == moon_minute) return;
    moon_minute = m;

    if(config_has_location(&config) && sky_compute_moon(&moon_now, now->t)) {
        dial_set_moon(&moon_now, config.latitude < 0.0);
    }
    else {
        dial_set_moon(NULL, false);
    }
}

/*********************
 *  WEATHER
 *********************/
static char * read_file(const char * path)
{
    FILE * f = fopen(path, "rb");
    if(f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char * buf = malloc((size_t)len + 1);
    size_t got = buf ? fread(buf, 1, (size_t)len, f) : 0;
    fclose(f);
    if(buf) buf[got] = '\0';
    return buf;
}

/* The board will use its HTTP client from a background task; curl stands in here */
static char * fetch_url(const char * url)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -sf --max-time 10 '%s'", url); /* our URL never contains a quote */
    FILE * p = popen(cmd, "r");
    if(p == NULL) return NULL;

    size_t cap = 16384, len = 0;
    char * buf = malloc(cap);
    size_t n;
    while(buf && (n = fread(buf + len, 1, cap - len - 1, p)) > 0) {
        len += n;
        if(len + 1 == cap) {
            char * bigger = realloc(buf, cap * 2);
            if(bigger == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = bigger;
            cap *= 2;
        }
    }
    int status = pclose(p);
    if(buf == NULL || status != 0) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static void refresh_weather(const options_t * opt, bool force)
{
    if(!config_has_location(&config)) {
        dial_set_weather(NULL);
        return;
    }
    time_t now = time(NULL);
    if(!force && weather_fetched != 0 && (opt->weather_path || now - weather_fetched < WEATHER_REFRESH_SECS)) return;
    weather_fetched = now;

    char * json = NULL;
    const char * source = opt->weather_path;
    char url[512];
    if(opt->weather_path) {
        json = read_file(opt->weather_path);
    }
    else if(weather_build_url(url, sizeof(url), &config) > 0) {
        source = "open-meteo.com";
        json = fetch_url(url);
    }
    if(json == NULL) {
        fprintf(stderr, "weather: could not read %s\n", source ? source : "(no source)");
        return; /* keep showing the last good forecast */
    }

    static weather_t parsed; /* parse aside so a bad response can't wipe good data */
    char err[96];
    if(weather_parse_open_meteo(&parsed, json, err, sizeof(err))) {
        weather = parsed;
        dial_set_weather(&weather);
        printf("weather: %s, %d hours from %s\n", source, weather.hour_count, weather.first_date);
    }
    else {
        fprintf(stderr, "weather: %s: %s\n", source, err);
    }
    free(json);
}

/**
 * The real panel is round, so pixels outside the circle simply don't exist.
 * Simulate that with a thick black ring over the corners. Sim-only: the UI
 * code itself never assumes it is masked.
 */
static void create_bezel_mask(lv_obj_t * parent)
{
    const int32_t thickness = 110; /* > corner distance (240*sqrt(2) - 240 ≈ 100) */
    lv_obj_t * ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, DIAL_SIZE + 2 * thickness, DIAL_SIZE + 2 * thickness);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, thickness, 0);
    lv_obj_set_style_border_color(ring, lv_color_black(), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static dial_layer_t layer_for_screen(screen_t screen)
{
    switch(screen) {
        case SCREEN_TIDE: return DIAL_LAYER_TIDE;
        case SCREEN_SUN: return DIAL_LAYER_SUN;
        case SCREEN_MOON: return DIAL_LAYER_MOON;
        case SCREEN_WEATHER: return DIAL_LAYER_WEATHER;
        case SCREEN_CURRENT: return DIAL_LAYER_CURRENT;
        default: return DIAL_LAYER_TIDE;
    }
}

/* Push the settings into the dial and make all data be fetched again */
static void apply_config(const options_t * opt)
{
    /* Run the simulator in the configured zone, the way the board will */
    setenv("TZ", config.tz_posix, 1);
    tzset();

    dial_set_top_hour(opt->top_hour >= 0 ? opt->top_hour : config.top_hour);
    dial_set_metric(config.units == UNITS_METRES);
    dial_set_station_name(config.station_name);
    dial_set_current_station_name(config.current_station_name);
    dial_set_place(config.place_name, config.latitude, config.longitude);
    dial_set_weather_units(config.temp_unit, config.wind_unit);

    dial_layer_t order[SCREEN_COUNT];
    for(int i = 0; i < config.screen_count; i++) order[i] = layer_for_screen(config.screens[i]);
    dial_set_layer_order(order, config.screen_count);

    data_day_number = -1;
    moon_minute = -1;
    weather_fetched = 0;
}

static time_t file_mtime(const char * path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

/* Reload when the watched file changes (or first appears) */
static bool check_config_changed(const options_t * opt)
{
    time_t m = file_mtime(watch_path);
    if(m == 0 || m == watch_mtime) return false;
    watch_mtime = m;

    clock_config_t previous = config;
    config_defaults(&config);
    if(load_config(watch_path) != 0) {
        config = previous;
        return false;
    }
    apply_config(opt);
    return true;
}

static void clock_timer_cb(lv_timer_t * t)
{
    const options_t * opt = lv_timer_get_user_data(t);
    check_config_changed(opt);
    clock_now_t now = current_time(opt);
    bool new_day = now.day_number != data_day_number;
    refresh_day(opt, &now);
    refresh_weather(opt, new_day); /* hours are relative to the forecast's first day */
    refresh_moon(&now);
    dial_set_time(now.hour, now.minute);
}

/* This Mac's address on the local network, so a phone can reach the dev server */
static bool lan_address(char * buf, size_t len)
{
    struct ifaddrs * list;
    if(getifaddrs(&list) != 0) return false;
    bool found = false;
    for(struct ifaddrs * i = list; i && !found; i = i->ifa_next) {
        if(!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        struct in_addr a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr;
        if((ntohl(a.s_addr) >> 16) == 0xA9FE) continue; /* 169.254: no DHCP */
        found = inet_ntop(AF_INET, &a, buf, (socklen_t)len) != NULL;
    }
    freeifaddrs(list);
    return found;
}

/* The board will show its own address; here it's the dev server on this Mac */
static void set_setup_card(void)
{
    static char url[64], line1[64];
    char ip[INET_ADDRSTRLEN];
    bool lan = lan_address(ip, sizeof(ip));
    snprintf(url, sizeof(url), "http://%s:8080/", lan ? ip : "localhost");
    snprintf(line1, sizeof(line1), "%s:8080", lan ? ip : "localhost");

    setup_card_info_t info = {
        .title = "SETTINGS",
        .hint = "Scan with your phone",
        .qr_text = url,
        .line1 = line1,
        .line2 = lan ? "tools/dev_server.py --lan" : "No network found",
    };
    dial_set_setup_info(&info);
}

static void build_ui(const options_t * opt)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    clock_now_t now = current_time(opt);

    dial_create(scr);
    apply_config(opt);
    refresh_day(opt, &now);
    refresh_moon(&now);
    refresh_weather(opt, true);
    dial_set_time(now.hour, now.minute);
    dial_set_layer(opt->layer >= 0 ? (dial_layer_t)opt->layer : layer_for_screen(config.screens[0]));
    if(opt->detail) dial_toggle_detail();
    set_setup_card();
    if(opt->setup) dial_show_setup(true);

    create_bezel_mask(scr);
}

/*********************
 *  HEADLESS SNAPSHOT
 *********************/
static uint32_t wallclock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void null_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static void put_le32(FILE * f, uint32_t v)
{
    uint8_t b[4] = {v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff};
    fwrite(b, 1, 4, f);
}

static void put_le16(FILE * f, uint16_t v)
{
    uint8_t b[2] = {v & 0xff, (v >> 8) & 0xff};
    fwrite(b, 1, 2, f);
}

/* Minimal 24-bit BMP writer: no dependencies, opens anywhere (and `sips` converts it) */
static int write_bmp(const char * path, const lv_draw_buf_t * buf)
{
    uint32_t w = buf->header.w;
    uint32_t h = buf->header.h;
    uint32_t row = (w * 3 + 3) & ~3u;

    FILE * f = fopen(path, "wb");
    if(f == NULL) return -1;

    fwrite("BM", 1, 2, f);
    put_le32(f, 54 + row * h);
    put_le32(f, 0);
    put_le32(f, 54);
    put_le32(f, 40);
    put_le32(f, w);
    put_le32(f, h);
    put_le16(f, 1);
    put_le16(f, 24);
    put_le32(f, 0);
    put_le32(f, row * h);
    put_le32(f, 2835);
    put_le32(f, 2835);
    put_le32(f, 0);
    put_le32(f, 0);

    uint8_t pad[3] = {0};
    for(int32_t y = (int32_t)h - 1; y >= 0; y--) { /* BMP rows are bottom-up */
        const uint8_t * src = buf->data + y * buf->header.stride;
        for(uint32_t x = 0; x < w; x++) {
            fwrite(src + x * 4, 1, 3, f); /* LVGL ARGB8888 is B,G,R,A in memory: same as BMP */
        }
        fwrite(pad, 1, row - w * 3, f);
    }

    fclose(f);
    return 0;
}

static int run_snapshot(const options_t * opt)
{
    lv_tick_set_cb(wallclock_ms);

    static uint8_t draw_buf[HOR_RES * VER_RES * 2];
    lv_display_t * disp = lv_display_create(HOR_RES, VER_RES);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, null_flush_cb);

    build_ui(opt);

    lv_draw_buf_t * shot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if(shot == NULL) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    int rc = write_bmp(opt->snapshot_path, shot);
    lv_draw_buf_destroy(shot);
    if(rc != 0) {
        perror(opt->snapshot_path);
        return 1;
    }
    printf("wrote %s\n", opt->snapshot_path);
    return 0;
}

/*********************
 *  INTERACTIVE
 *********************/
static int run_interactive(options_t * opt)
{
    lv_display_t * disp = lv_sdl_window_create(HOR_RES, VER_RES);
    lv_sdl_window_set_title(disp, "Even Flow - 480x480 round");

    lv_sdl_mouse_create(); /* stands in for the touch panel */
    lv_indev_t * wheel = lv_sdl_mousewheel_create();
    lv_indev_t * keys = lv_sdl_keyboard_create();

    build_ui(opt);

    /* Wheel and arrow keys both act as the bezel knob */
    lv_group_t * group = lv_group_create();
    lv_indev_set_group(wheel, group);
    lv_indev_set_group(keys, group);
    dial_attach_group(group);

    lv_timer_create(clock_timer_cb, 1000, opt);

    for(;;) {
        uint32_t idle_ms = lv_timer_handler();
        if(idle_ms > 50) idle_ms = 50;
        usleep(idle_ms * 1000);
    }
    return 0;
}

static void usage(const char * argv0)
{
    fprintf(stderr,
            "usage: %s [--date YYYY-MM-DD] [--time HH:MM] [--layer N] [--top H] [--detail] [--setup] [--config file] [--weather file] [--tide file] [--current file] [--snapshot out.bmp]\n",
            argv0);
}

int main(int argc, char ** argv)
{
    options_t opt = {.fixed_minute = -1, .top_hour = -1, .layer = -1};

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            int h, m;
            if(sscanf(argv[++i], "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
                usage(argv[0]);
                return 2;
            }
            opt.fixed_minute = h * 60 + m;
        }
        else if(strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
            int y, mo, d;
            if(sscanf(argv[++i], "%d-%d-%d", &y, &mo, &d) != 3 || y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) {
                usage(argv[0]);
                return 2;
            }
            opt.fixed_year = y;
            opt.fixed_month = mo;
            opt.fixed_day = d;
        }
        else if(strcmp(argv[i], "--layer") == 0 && i + 1 < argc) {
            opt.layer = atoi(argv[++i]);
            if(opt.layer < 0 || opt.layer >= DIAL_LAYER_COUNT) {
                usage(argv[0]);
                return 2;
            }
        }
        else if(strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
            opt.top_hour = atoi(argv[++i]);
            if(opt.top_hour < 0 || opt.top_hour > 23) {
                usage(argv[0]);
                return 2;
            }
        }
        else if(strcmp(argv[i], "--detail") == 0) {
            opt.detail = true;
        }
        else if(strcmp(argv[i], "--setup") == 0) {
            opt.setup = true;
        }
        else if(strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            opt.config_path = argv[++i];
        }
        else if(strcmp(argv[i], "--tide") == 0 && i + 1 < argc) {
            opt.tide_path = argv[++i];
        }
        else if(strcmp(argv[i], "--current") == 0 && i + 1 < argc) {
            opt.current_path = argv[++i];
        }
        else if(strcmp(argv[i], "--weather") == 0 && i + 1 < argc) {
            opt.weather_path = argv[++i];
        }
        else if(strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            opt.snapshot_path = argv[++i];
        }
        else {
            usage(argv[0]);
            return 2;
        }
    }

    config_defaults(&config);
    watch_path = opt.config_path ? opt.config_path : LOCAL_CONFIG;
    if(opt.config_path != NULL) {
        if(load_config(opt.config_path) != 0) return 1;
    }
    else if(access(LOCAL_CONFIG, R_OK) == 0) {
        load_config(LOCAL_CONFIG);
    }
    else {
        load_config(EXAMPLE_CONFIG); /* until even-flow.conf is created */
    }
    watch_mtime = file_mtime(watch_path);
    setenv("TZ", config.tz_posix, 1);
    tzset();

    lv_init();
    return opt.snapshot_path ? run_snapshot(&opt) : run_interactive(&opt);
}
