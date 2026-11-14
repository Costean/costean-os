/* Host tool: render every Costean OS OLED screen with the firmware renderer to PBM files.
 * build: gcc -Imain/costean tools/lode_preview.c main/costean/lode_oled.c main/costean/lode_font.c
 * usage: lode_preview OUTDIR   (then tools/lode_preview.py turns them into scaled PNGs) */
#include <stdio.h>
#include "lode_oled.h"
#include "lode_scenarios.h"

static const char *scr_name[LODE_SCREEN_COUNT] = {"splash", "hash", "heat", "shares", "pools", "block", "overheat"};

static void dump(const char *dir, const char *name, const lode_fb_t *fb)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "P1\n%d %d\n", LODE_W, LODE_H);
    for (int y = 0; y < LODE_H; y++) {
        for (int x = 0; x < LODE_W; x++) fputc(lode_get(fb, x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    lode_fb_t fb; lode_view_t v; char name[96];
    for (int sc = 0; sc < LODE_SCENARIOS; sc++) {
        lode_sample(&v, sc);
        for (int s = 0; s < LODE_SCREEN_COUNT; s++) {
            if (sc && (s == LODE_SPLASH || s == LODE_BLOCK || s == LODE_OVERHEAT)) continue;
            for (int b = 0; b < 2; b++) {
                if (b && !(v.throttled || v.failover || s == LODE_BLOCK || s == LODE_OVERHEAT)) continue;
                lode_render(&fb, (lode_screen_t) s, &v, b);
                snprintf(name, sizeof name, "%s-%s%s", lode_scenario_name[sc], scr_name[s], b ? "-blink" : "");
                dump(dir, name, &fb);
            }
        }
    }
    return 0;
}
