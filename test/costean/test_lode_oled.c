/* Host tests for the Costean OS 128x32 OLED renderer (main/costean/lode_oled.c). */
#include <stdio.h>
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

static void test_every_screen_draws(void)
{
    lode_fb_t fb; lode_view_t v;
    for (int sc = 0; sc < LODE_SCENARIOS; sc++)
        for (int s = 0; s < LODE_SCREEN_COUNT; s++)
            for (int b = 0; b < 2; b++) {
                lode_sample(&v, sc);
                lode_render(&fb, (lode_screen_t) s, &v, b);
                int n = lode_count(&fb);
                CHECK(n > 60 && n < 128 * 32);
            }
}

static void test_header_layout(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 0);
    for (int p = LODE_HASH; p <= LODE_POOLS; p++) {
        lode_render(&fb, (lode_screen_t) p, &v, false);
        CHECK(lode_get(&fb, 3, 0) && lode_get(&fb, 0, 3));         /* diamond logo */
        CHECK(lode_get(&fb, 127, 0));                              /* inverted profile tag */
        int idx = lode_page_index((lode_screen_t) p);
        CHECK(lit_in(&fb, 57 + idx * 5, 2, 3, 3) == 9);            /* active page dot */
        CHECK(lit_in(&fb, 0, 8, 128, 1) == 0 || p == LODE_POOLS);  /* gap under the header */
    }
    /* worst-case tags never reach the page dots (x <= 75) */
    const char *worst[] = {"THROTTLE", "FAILOVER", "MANUAL"};
    for (int i = 0; i < 3; i++) CHECK(128 - (lode_text_width(worst[i], 1) + 3) > 75);
    lode_sample(&v, 5); strcpy(v.profile, "MANUAL"); v.tune_steps = 12;
    lode_render(&fb, LODE_HASH, &v, false);
    CHECK(lit_in(&fb, 75, 0, 2, 8) == 0);           /* one clear column between dots and tag */
}

static void test_throttle_and_failover_tags(void)
{
    lode_fb_t a, b; lode_view_t v;
    lode_sample(&v, 1);                         /* throttled */
    lode_render(&a, LODE_HASH, &v, false);
    lode_render(&b, LODE_HASH, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) != 0);       /* THROTTLE blinks against the profile */
    lode_sample(&v, 0);
    lode_render(&a, LODE_HASH, &v, false);
    lode_render(&b, LODE_HASH, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) == 0);       /* calm screens don't flicker */
    lode_sample(&v, 2);                         /* failover */
    lode_render(&a, LODE_POOLS, &v, false);
    lode_render(&b, LODE_POOLS, &v, true);
    CHECK(memcmp(&a, &b, sizeof a) != 0);
}

static void test_pool_bar(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 0);                         /* 70/30 */
    lode_render(&fb, LODE_POOLS, &v, false);
    CHECK(lit_in(&fb, 1, 10, 80, 5) == 80 * 5);     /* pool A solid over ~70 % of the bar */
    CHECK(lit_in(&fb, 100, 10, 26, 5) < 26 * 5);    /* pool B patterned */
    lode_sample(&v, 2);                         /* B down -> 100 % to A */
    lode_render(&fb, LODE_POOLS, &v, false);
    CHECK(lit_in(&fb, 1, 10, 125, 5) == 125 * 5);
    lode_sample(&v, 5);                         /* single pool: no bar */
    lode_render(&fb, LODE_POOLS, &v, false);
    CHECK(!(lode_get(&fb, 0, 9) && lode_get(&fb, 127, 9) && lode_get(&fb, 64, 9)));
}

static void test_heat_bars(void)
{
    lode_fb_t fb; lode_view_t v;
    lode_sample(&v, 0);                         /* 58/68 C chip */
    lode_render(&fb, LODE_HEAT, &v, true);
    int chip = lit_in(&fb, 76, 11, 50, 2);
    v.chip_c = 30; lode_render(&fb, LODE_HEAT, &v, true);
    CHECK(lit_in(&fb, 76, 11, 50, 2) < chip);
    v.chip_c = 99; lode_render(&fb, LODE_HEAT, &v, false);
    CHECK(lit_in(&fb, 76, 11, 50, 2) == 100);   /* clamps, never overflows its frame */
}

int main(void)
{
    test_primitives(); test_helpers(); test_every_screen_draws(); test_header_layout();
    test_throttle_and_failover_tags(); test_pool_bar(); test_heat_bars();
    printf("lode_oled: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
