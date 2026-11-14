#pragma once
/* Costean OS 1.2 "Lode" screens for the Bitaxe Gamma 128x32 monochrome OLED.
 * Pure C (no LVGL / ESP-IDF): screen.c turns the frame into an LVGL image, and the
 * host tool tools/lode_preview renders the very same frames to PNG for previews/tests. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LODE_W 128
#define LODE_H 32
#define LODE_MAX_POOLS 4

typedef struct {
    uint8_t bits[LODE_H][LODE_W / 8];   /* 1 = lit pixel, MSB = leftmost */
} lode_fb_t;

typedef enum {
    LODE_SPLASH = 0,
    LODE_HASH,      /* page 1: hashrate, clock, voltage, quick vitals */
    LODE_HEAT,      /* page 2: chip / regulator / power against the Costean limits */
    LODE_SHARES,    /* page 3: best difficulty, shares, efficiency, IP */
    LODE_POOLS,     /* page 4: pool split bar, per-pool share and health */
    LODE_BLOCK,     /* overlay: block found */
    LODE_OVERHEAT,  /* overlay: stock overheat safe mode */
    LODE_SCREEN_COUNT
} lode_screen_t;

typedef struct {
    char host[24];
    uint8_t weight;     /* configured % */
    uint8_t eff;        /* effective % after failover */
    bool up;
    uint32_t acc, rej;
} lode_pool_t;

typedef struct {
    const char *version;     /* "1.2" */
    const char *board;       /* "602" */
    char profile[8];         /* "ECO" "STOCK" "TURBO" "MANUAL" */
    bool throttled;
    bool scheduled;          /* profile chosen by a schedule window */
    int tune_steps;          /* auto-tune steps above the profile */
    float ghs;
    int mhz, mv;
    float chip_c, vr_c, power_w, fan_pct;
    int lim_chip, lim_vr, lim_power;
    char best_session[12], best_all[12];
    uint32_t acc, rej;
    char ip[16];
    uint32_t uptime_s;
    int npools;
    bool split;              /* multi-pool split running */
    bool failover;           /* a weighted pool is down and its share moved */
    lode_pool_t pool[LODE_MAX_POOLS];
} lode_view_t;

extern const uint8_t lode_font6x8[95][8];

void lode_clear(lode_fb_t *fb);
void lode_px(lode_fb_t *fb, int x, int y, bool on);
bool lode_get(const lode_fb_t *fb, int x, int y);
void lode_fill(lode_fb_t *fb, int x, int y, int w, int h, bool on);
void lode_frame(lode_fb_t *fb, int x, int y, int w, int h);
/* text: returns the advance in pixels; scale 1 or 2; inverted draws dark text */
int lode_text(lode_fb_t *fb, int x, int y, const char *s, int scale, bool on);
int lode_text_width(const char *s, int scale);
void lode_diamond(lode_fb_t *fb, int cx, int cy, int r, int thick);
int lode_count(const lode_fb_t *fb);

/* Draw one screen. blink toggles every ~1 s and alternates the throttle tag. */
void lode_render(lode_fb_t *fb, lode_screen_t screen, const lode_view_t *v, bool blink);
/* Number of carousel pages (HASH..POOLS) and the page index of a screen (-1 if not a page). */
int lode_page_index(lode_screen_t s);

/* helpers shared with the tests */
void lode_fmt_hashrate(float ghs, char *num, int numlen, const char **unit);
void lode_short_host(const char *url, char *out, int outlen);

#ifdef __cplusplus
}
#endif
