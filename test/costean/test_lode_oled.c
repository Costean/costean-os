/* Host tests for the Costean OS 128x32 OLED renderer (main/costean/lode_oled.c). */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "lode_oled.h"
#include "../../tools/lode_scenarios.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* does any lit pixel exist inside a rectangle */
static int lit_in(const lode_fb_t *fb, int x, int y, int w, int h)
{
    int n = 0;
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) n += lode_get(fb, i, j);
    return n;
}

static void test_primitives(void)
{
    lode_fb_t fb; lode_clear(&fb);
    CHECK(lode_count(&fb) == 0);
    lode_px(&fb, -1, 0, true); lode_px(&fb, 128, 31, true); lode_px(&fb, 0, 32, true);
    CHECK(lode_count(&fb) == 0);          /* off-screen writes are ignored */
    lode_px(&fb, 127, 31, true);
    CHECK(lode_get(&fb, 127, 31) && lode_count(&fb) == 1);
    CHECK(lode_text_width("COSTEAN", 1) == 42);
    {   /* firmware version on the OLED: full on the splash, short tag on SHARES */
        char sv[8];
        lode_short_version("1.2.0-costean.5", sv, sizeof sv); CHECK(strcmp(sv, "c5") == 0);
        lode_short_version("2.15.4", sv, sizeof sv); CHECK(strcmp(sv, "2.15.4") == 0);
        lode_short_version(NULL, sv, sizeof sv); CHECK(strcmp(sv, "?") == 0);
        CHECK(36 + lode_text_width("1.2.0-costean.5", 1) <= LODE_W);   /* fits beside the logo */
        lode_view_t sv_v; memset(&sv_v, 0, sizeof sv_v); sv_v.version = "1.2.0-costean.5"; sv_v.board = "602";
        lode_fb_t a, b; lode_render(&a, LODE_SPLASH, &sv_v, false);
        sv_v.version = "1.2.0-costean.4"; lode_render(&b, LODE_SPLASH, &sv_v, false);
        CHECK(memcmp(&a, &b, sizeof a) != 0);                            /* the version really is drawn */
    }
    CHECK(lode_text_width("1.194", 2) == 54);   /* narrow dot in big digits */
    lode_clear(&fb);
    lode_diamond(&fb, 15, 15, 14, 2);
    for (int y = 0; y < 31; y++) for (int x = 0; x < 31; x++) CHECK(lode_get(&fb, x, y) == lode_get(&fb, y, x)); /* symmetric about the vein */
    CHECK(lode_get(&fb, 15, 1) && lode_get(&fb, 1, 15) && !lode_get(&fb, 15, 0));
}

static void test_helpers(void)
{
    char n[16], h[24]; const char *u;
    lode_fmt_hashrate(1194.0f, n, sizeof n, &u); CHECK(!strcmp(n, "1.194") && !strcmp(u, "TH/s"));
    lode_fmt_hashrate(968.4f, n, sizeof n, &u); CHECK(!strcmp(n, "968") && !strcmp(u, "GH/s"));
    lode_fmt_hashrate(0, n, sizeof n, &u); CHECK(!strcmp(n, "---"));
    lode_short_host("stratum+tcp://public-pool.io:21496", h, sizeof h); CHECK(!strcmp(h, "public-pool"));
    lode_short_host("solo.ckpool.org", h, sizeof h); CHECK(!strcmp(h, "ckpool"));
    lode_short_host("stratum.braiins.com:3333/x", h, sizeof h); CHECK(!strcmp(h, "braiins"));
    lode_short_host("10.0.0.5", h, sizeof h); CHECK(h[0] != 0);
    lode_short_host("", h, sizeof h); CHECK(h[0] == 0);
    lode_short_host(NULL, h, sizeof h); CHECK(h[0] == 0);
}

/* largest lit square anywhere on the frame */
static int max_solid(const lode_fb_t *fb)
{
    int best = 0;
    for (int k = 2; k <= 32; k++) {
        bool found = false;
        for (int y = 0; y + k <= 32 && !found; y++)
            for (int x = 0; x + k <= 128 && !found; x++)
                if (lit_in(fb, x, y, k, k) == k * k) found = true;
        if (!found) break;
        best = k;
    }
    return best;
}

static int overlaps(void)
{
    const lode_box_t *b; int n = lode_boxes(&b), bad = 0;
    for (int i = 0; i < n; i++) {
        if (b[i].x < 0 || b[i].y < 0 || b[i].x + b[i].w > 128 || b[i].y + b[i].h > 32) bad++;
        for (int j = i + 1; j < n; j++)
            if (b[i].x < b[j].x + b[j].w && b[j].x < b[i].x + b[i].w && b[i].y < b[j].y + b[j].h && b[j].y < b[i].y + b[i].h) bad++;
    }
    return bad;
}

static void test_every_screen_draws(void)
{
    lode_fb_t fb; lode_view_t v;
    for (int sc = 0; sc < LODE_SCENARIOS; sc++)
        for (int s = 0; s < LODE_SCREEN_COUNT; s++)
            for (int b = 0; b < 2; b++) {
                lode_sample(&v, sc);
                lode_render(&fb, (lode_screen_t) s, &v, b);
                int n = lode_count(&fb);
                CHECK(n > 60);
                CHECK(n < 128 * 32 / 4);            /* white text on black: under a quarter of the pixels lit */
                CHECK(max_solid(&fb) <= 2);         /* no filled or inverted areas, only 1-2 px strokes */
                CHECK(overlaps() == 0);             /* no text runs into other text or off the screen */
                CHECK(lode_boxes(NULL) <= 12);      /* few items per screen */
            }
}

static void test_big_digits(void)
{
    lode_fb_t fb; lode_clear(&fb);
    CHECK(lode_big_width("1.940") == 4 * 10 + 4 - 2);
    lode_big(&fb, 0, 0, "0123456789.-");
    CHECK(max_solid(&fb) == 2);                     /* 2-pixel strokes, never blocks */
    for (int d = 0; d < 10; d++) {                  /* every digit has an open inside */
        int x = d * 10;
        CHECK(lit_in(&fb, x, 0, 8, LODE_BIG_H) > 20);
        CHECK(lit_in(&fb, x, 0, 8, LODE_BIG_H) < 8 * LODE_BIG_H * 2 / 3);   /* under 2/3 of the cell lit */
        CHECK(lit_in(&fb, x + 8, 0, 2, LODE_BIG_H) == 0);   /* 2 px gap between digits */
    }
}

static void test_header_is_plain_text(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 0);
    const char *titles[] = {"HASH", "HEAT", "SHARES", "POOLS"};
    for (int p = LODE_HASH; p <= LODE_POOLS; p++) {
        lode_render(&fb, (lode_screen_t) p, &v, false);
        lode_fb_t want; lode_clear(&want);
        int w = lode_text(&want, 0, 0, titles[p - LODE_HASH], 1, true);
        CHECK(memcmp(fb.bits[0], want.bits[0], 4) == 0 || w > 32);   /* page name, lit on black */
        lode_clear(&want);
        lode_text(&want, 128 - lode_text_width("MANUAL", 1) + 1, 0, "MANUAL", 1, true);
        for (int y = 0; y < 8; y++) for (int x = 80; x < 128; x++) CHECK(lode_get(&fb, x, y) == lode_get(&want, x, y));
        CHECK(lit_in(&fb, 0, 7, 128, 1) == 0);    /* clear row under the top line */
    }
    lode_sample(&v, 7);                           /* clock icon + MANUAL+3 still fits */
    lode_render(&fb, LODE_SHARES, &v, false);
    CHECK(overlaps() == 0);
}

static void test_hash_page(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 6);                           /* 950 MHz / 1250 mV, 1.937 TH/s */
    lode_render(&fb, LODE_HASH, &v, false);
    lode_fb_t want; lode_clear(&want);
    lode_big(&want, 0, 9, "1.937");
    for (int y = 9; y < 9 + LODE_BIG_H; y++) for (int x = 0; x < lode_big_width("1.937") + 2; x++) CHECK(lode_get(&fb, x, y) == lode_get(&want, x, y));
    CHECK(lit_in(&fb, 0, 23, 128, 2) == 0);       /* gap between the big number and the bottom line */
    CHECK(lit_in(&fb, 0, 25, 60, 7) > 20);        /* 950MHz 1250mV */
}

static void test_throttle_and_failover_tags(void)
{
    lode_fb_t a, b; lode_view_t v;
    lode_sample(&v, 1);                         /* throttled */
    lode_render(&a, LODE_HASH, &v, false);
    lode_render(&b, LODE_HASH, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) != 0);       /* THROTTLE takes turns with the profile name */
    CHECK(max_solid(&b) <= 2);
    lode_sample(&v, 0);
    lode_render(&a, LODE_HASH, &v, false);
    lode_render(&b, LODE_HASH, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) == 0);       /* calm screens don't flicker */
    lode_sample(&v, 2);                         /* failover */
    lode_render(&a, LODE_POOLS, &v, false);
    lode_render(&b, LODE_POOLS, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) != 0);
}

static void test_heat_near_limit(void)
{
    lode_fb_t a, b; lode_view_t v;
    lode_sample(&v, 0);
    lode_render(&a, LODE_HEAT, &v, false);
    v.chip_c = 66; lode_render(&b, LODE_HEAT, &v, false);
    CHECK(memcmp(a.bits[8], b.bits[8], 16 * 7) != 0);   /* chip line says NEAR */
    CHECK(max_solid(&b) <= 2);
}

static void test_render_clears(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 0);
    memset(&fb, 0xff, sizeof fb);               /* garbage from a previous frame */
    lode_render(&fb, LODE_HEAT, &v, false);
    lode_fb_t ref; lode_render(&ref, LODE_HEAT, &v, false);
    CHECK(memcmp(&fb, &ref, sizeof fb) == 0);
}

int main(void)
{
    test_primitives(); test_helpers(); test_every_screen_draws(); test_big_digits(); test_header_is_plain_text();
    test_hash_page(); test_throttle_and_failover_tags(); test_heat_near_limit(); test_render_clears();
    printf("lode_oled: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
