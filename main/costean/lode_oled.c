#include "lode_oled.h"

#include <stdio.h>
#include <string.h>

/* ---------- primitives ---------- */
void lode_clear(lode_fb_t *fb) { memset(fb, 0, sizeof(*fb)); }

void lode_px(lode_fb_t *fb, int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= LODE_W || y >= LODE_H) return;
    uint8_t m = (uint8_t) (0x80 >> (x & 7));
    if (on) fb->bits[y][x >> 3] |= m; else fb->bits[y][x >> 3] &= (uint8_t) ~m;
}

bool lode_get(const lode_fb_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= LODE_W || y >= LODE_H) return false;
    return (fb->bits[y][x >> 3] & (0x80 >> (x & 7))) != 0;
}

void lode_fill(lode_fb_t *fb, int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) lode_px(fb, i, j, on);
}

void lode_frame(lode_fb_t *fb, int x, int y, int w, int h)
{
    for (int i = x; i < x + w; i++) { lode_px(fb, i, y, true); lode_px(fb, i, y + h - 1, true); }
    for (int j = y; j < y + h; j++) { lode_px(fb, x, j, true); lode_px(fb, x + w - 1, j, true); }
}

int lode_count(const lode_fb_t *fb)
{
    int n = 0;
    for (int y = 0; y < LODE_H; y++)
        for (int x = 0; x < LODE_W; x++) n += lode_get(fb, x, y);
    return n;
}

static int adv(char c, int scale) { return (c == '.' && scale > 1) ? 3 * scale : 6 * scale; }

int lode_text_width(const char *s, int scale)
{
    int w = 0;
    for (; s && *s; s++) w += adv(*s, scale);
    return w;
}

int lode_text(lode_fb_t *fb, int x, int y, const char *s, int scale, bool on)
{
    int x0 = x;
    for (; s && *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c < 0x20 || c > 0x7e) c = '?';
        const uint8_t *g = lode_font6x8[c - 0x20];
        int shift = (*s == '.' && scale > 1) ? 2 : 0;   /* narrow '.' in big digits */
        for (int r = 0; r < 8; r++)
            for (int col = 0; col < 6; col++)
                if (g[r] & (0x20 >> col))
                    lode_fill(fb, x + (col - shift) * scale, y + r * scale, scale, scale, on);
        x += adv(*s, scale);
    }
    return x - x0;
}

void lode_diamond(lode_fb_t *fb, int cx, int cy, int r, int thick)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            bool ring = d <= r && d > r - thick;
            /* the vein: a short diagonal cut through the stone, bottom-left to top-right */
            int s = dx + dy, a = dx < 0 ? -dx : dx;
            bool vein = (s >= 0 && s < thick) && a <= r / 2 && d < r - thick;
            if (ring || vein) lode_px(fb, cx + dx, cy + dy, true);
        }
}

/* ---------- helpers ---------- */
void lode_fmt_hashrate(float ghs, char *num, int numlen, const char **unit)
{
    if (ghs >= 1000.0f) { snprintf(num, numlen, "%.3f", ghs / 1000.0f); *unit = "TH/s"; }
    else if (ghs > 0.0f) { snprintf(num, numlen, "%.0f", ghs); *unit = "GH/s"; }
    else { snprintf(num, numlen, "---"); *unit = "GH/s"; }
}

void lode_short_host(const char *url, char *out, int outlen)
{
    if (!out || outlen < 2) return;
    out[0] = 0;
    if (!url || !url[0]) return;
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s", h);
    char *c = strpbrk(tmp, ":/");
    if (c) *c = 0;
    /* drop a leading "www."/"stratum."/"pool." style label when there are 3+ labels */
    char *labels[8]; int n = 0; char *p = tmp;
    while (p && *p && n < 8) { labels[n++] = p; char *d = strchr(p, '.'); if (!d) break; *d = 0; p = d + 1; }
    const char *pick = n >= 3 ? labels[n - 2] : n >= 1 ? labels[0] : tmp;
    snprintf(out, outlen, "%s", pick);
}

static void fmt_uptime(uint32_t s, char *b, int n)
{
    uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d) snprintf(b, n, "%lud%luh", (unsigned long) d, (unsigned long) h);
    else if (h) snprintf(b, n, "%luh%lum", (unsigned long) h, (unsigned long) m);
    else snprintf(b, n, "%lum", (unsigned long) m);
}

static void right(lode_fb_t *fb, int y, const char *s) { lode_text(fb, LODE_W - lode_text_width(s, 1), y, s, 1, true); }

/* ---------- shared header: diamond, wordmark, page dots, profile tag ---------- */
#define HDR_H 8

static void clock_icon(lode_fb_t *fb, int x, int y)
{
    static const uint8_t ic[7] = {0x38, 0x44, 0x92, 0x9a, 0x82, 0x44, 0x38};
    for (int r = 0; r < 7; r++) for (int c = 0; c < 7; c++) if (ic[r] & (0x80 >> c)) lode_px(fb, x + c, y + r, true);
}

static void header(lode_fb_t *fb, const lode_view_t *v, int page, const char *tagOverride)
{
    lode_diamond(fb, 3, 3, 3, 1);
    lode_text(fb, 10, 0, "COSTEAN", 1, true);
    if (page >= 0) {
        for (int i = 0; i < 4; i++) {
            int x = 58 + i * 5;
            if (i == page) lode_fill(fb, x - 1, 2, 3, 3, true); else lode_px(fb, x, 3, true);
        }
    }
    char tag[12];
    if (tagOverride) snprintf(tag, sizeof tag, "%s", tagOverride);
    else if (v->tune_steps > 0) snprintf(tag, sizeof tag, "%.6s+%d", v->profile, v->tune_steps > 9 ? 9 : v->tune_steps);
    else snprintf(tag, sizeof tag, "%s", v->profile[0] ? v->profile : "--");
    int w = lode_text_width(tag, 1) + 3, x0 = LODE_W - w;
    lode_fill(fb, x0, 0, w, HDR_H, true);
    lode_text(fb, x0 + 2, 0, tag, 1, false);
    if (v->scheduled && !tagOverride && x0 - 9 > 76) clock_icon(fb, x0 - 9, 0);
}

static const char *tag_for(const lode_view_t *v, bool blink) { return (v->throttled && blink) ? "THROTTLE" : NULL; }

/* ---------- screens ---------- */
static void splash(lode_fb_t *fb, const lode_view_t *v)
{
    lode_diamond(fb, 15, 15, 14, 2);
    lode_text(fb, 36, 1, "COSTEAN", 2, true);
    for (int x = 36; x < LODE_W; x += 2) lode_px(fb, x, 17, true);   /* the lode line */
    char a[16], b[16];
    snprintf(a, sizeof a, "OS %s", v->version ? v->version : "1.2");
    snprintf(b, sizeof b, "GAMMA %s", v->board ? v->board : "602");
    lode_text(fb, 36, 22, a, 1, true);
    right(fb, 22, b);
}

static void page_hash(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    header(fb, v, 0, tag_for(v, blink));
    char num[12]; const char *unit;
    lode_fmt_hashrate(v->ghs, num, sizeof num, &unit);
    int w = lode_text(fb, 0, 9, num, 2, true);
    lode_text(fb, w + 3, 16, unit, 1, true);
    char a[12], b[12];
    snprintf(a, sizeof a, "%dMHz", v->mhz);
    snprintf(b, sizeof b, "%dmV", v->mv);
    right(fb, 9, a);
    right(fb, 17, b);
    if (v->throttled) lode_fill(fb, LODE_W - lode_text_width(a, 1) - 4, 9, 2, 7, blink);   /* clock held down */
    char line[40];
    float j = (v->ghs > 0 && v->power_w > 0) ? v->power_w / (v->ghs / 1000.0f) : 0;
    snprintf(line, sizeof line, "%dC VR%dC %.1fW", (int) (v->chip_c + 0.5f), (int) (v->vr_c + 0.5f), v->power_w);
    lode_text(fb, 0, 25, line, 1, true);
    if (j > 0) { char e[12]; snprintf(e, sizeof e, "%.1fJ", j); if (lode_text_width(line, 1) + lode_text_width(e, 1) + 6 <= LODE_W) right(fb, 25, e); }
}

static void meter(lode_fb_t *fb, int y, const char *label, float val, int lim, char unit, bool blink)
{
    char t[24];
    snprintf(t, sizeof t, "%-4s%3d/%d%c", label, (int) (val + 0.5f), lim, unit);
    lode_text(fb, 0, y, t, 1, true);
    int x = 74, w = LODE_W - x, h = 6;
    lode_frame(fb, x, y, w, h);
    float f = lim > 0 ? val / (float) lim : 0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    int fill = (int) ((w - 4) * f + 0.5f);
    lode_fill(fb, x + 2, y + 2, fill, h - 4, true);
    if (val >= lim - 3 && lim > 0 && blink) lode_fill(fb, x + 1, y + 1, w - 2, h - 2, true);   /* near the limit: flash */
}

static void page_heat(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    header(fb, v, 1, tag_for(v, blink));
    meter(fb, 9, "CHIP", v->chip_c, v->lim_chip, 'C', blink);
    meter(fb, 17, "VR", v->vr_c, v->lim_vr, 'C', blink);
    meter(fb, 25, "PWR", v->power_w, v->lim_power, 'W', blink);
}

static void page_shares(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    header(fb, v, 2, tag_for(v, blink));
    char l[40];
    snprintf(l, sizeof l, "BEST %s/%s", v->best_session[0] ? v->best_session : "--", v->best_all[0] ? v->best_all : "--");
    if (lode_text_width(l, 1) > LODE_W) snprintf(l, sizeof l, "BEST %s", v->best_all[0] ? v->best_all : "--");
    lode_text(fb, 0, 9, l, 1, true);
    unsigned long acc = v->acc > 999999 ? 999999 : v->acc, rej = v->rej > 9999 ? 9999 : v->rej;
    float rp = (v->acc + v->rej) ? 100.0f * v->rej / (float) (v->acc + v->rej) : 0;
    snprintf(l, sizeof l, "A/R %lu/%lu", acc, rej);
    lode_text(fb, 0, 17, l, 1, true);
    snprintf(l, sizeof l, "%.2f%%", rp);
    right(fb, 17, l);
    char up[12]; fmt_uptime(v->uptime_s, up, sizeof up);
    lode_text(fb, 0, 25, v->ip[0] ? v->ip : "no IP", 1, true);
    if (lode_text_width(v->ip, 1) + lode_text_width(up, 1) + 6 <= LODE_W) right(fb, 25, up);
}

static void seg_pattern(lode_fb_t *fb, int x, int y, int w, int h, int i)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            bool on = i == 0 ? true : i == 1 ? ((xx + yy) & 1) == 0 : i == 2 ? (xx % 3) == 0 : (yy == y || yy == y + h - 1 || xx == x || xx == x + w - 1);
            lode_px(fb, xx, yy, on);
        }
}

static void page_pools(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    const char *tag = tag_for(v, blink);
    if (v->failover && !blink) tag = "FAILOVER";
    header(fb, v, 3, tag);
    int n = v->npools > LODE_MAX_POOLS ? LODE_MAX_POOLS : v->npools;
    char l[48], h[24];
    if (!v->split || n < 1) {
        lode_short_host(n > 0 ? v->pool[0].host : "", h, sizeof h);
        snprintf(l, sizeof l, "POOL %.15s", h[0] ? h : "--");
        lode_text(fb, 0, 9, l, 1, true);
        snprintf(l, sizeof l, "SHARES %lu/%lu", (unsigned long) v->acc, (unsigned long) v->rej);
        lode_text(fb, 0, 17, l, 1, true);
        lode_text(fb, 0, 25, "SPLIT OFF", 1, true);
        right(fb, 25, n > 0 && v->pool[0].up ? "UP" : "DOWN");
        return;
    }
    /* split bar: effective share per pool, one pattern per pool */
    int x = 0, total = 0;
    for (int i = 0; i < n; i++) total += v->pool[i].eff;
    lode_frame(fb, 0, 9, LODE_W, 7);
    for (int i = 0; i < n; i++) {
        int pct = total ? v->pool[i].eff : v->pool[i].weight;
        int w = (int) ((LODE_W - 2) * pct / 100.0f + 0.5f);
        if (i == n - 1 || x + w > LODE_W - 2) w = (pct ? LODE_W - 2 - x : 0);
        if (w > 1) seg_pattern(fb, 1 + x, 10, w - 1, 5, i);
        x += w;
    }
    for (int row = 0; row < 2; row++) {
        int y = 17 + row * 8;
        if (n <= 2) {
            int i = row;
            if (i >= n) break;
            const lode_pool_t *p = &v->pool[i];
            lode_short_host(p->host, h, sizeof h);
            const char *st = !p->up ? "DOWN" : p->weight == 0 ? "SPARE" : p->eff != p->weight ? "+FO" : "OK";
            snprintf(l, sizeof l, "%c %3d%% %.10s", 'A' + i, p->up ? p->eff : 0, h);
            seg_pattern(fb, 0, y + 2, 4, 4, i);
            lode_text(fb, 6, y, l + 2, 1, true);
            right(fb, y, st);
        } else {
            for (int k = 0; k < 2; k++) {
                int i = row * 2 + k;
                if (i >= n) break;
                const lode_pool_t *p = &v->pool[i];
                lode_short_host(p->host, h, sizeof h);
                if (p->up) snprintf(l, sizeof l, "%3d%% %.4s", p->eff, h); else snprintf(l, sizeof l, "DOWN %.4s", h);
                int x0 = k * 65;
                seg_pattern(fb, x0, y + 2, 4, 4, i);
                lode_text(fb, x0 + 6, y, l, 1, true);
            }
        }
    }
}

static void block(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    lode_fill(fb, 0, 0, LODE_W, LODE_H, true);
    lode_fb_t d; lode_clear(&d);
    lode_diamond(&d, 14, 15, 13, 2);
    for (int y = 0; y < LODE_H; y++) for (int x = 0; x < 30; x++) if (lode_get(&d, x, y)) lode_px(fb, x, y, false);
    lode_text(fb, 34, 1, "BLOCK!", 2, false);
    char l[32];
    snprintf(l, sizeof l, "FOUND %s", v->best_session[0] ? v->best_session : "");
    lode_text(fb, 34, 22, l, 1, false);
    if (blink) lode_frame(fb, 0, 0, LODE_W, LODE_H);
}

static void overheat(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    if (blink) lode_fill(fb, 0, 0, LODE_W, 15, true);
    lode_text(fb, (LODE_W - lode_text_width("OVERHEAT", 2)) / 2, 0, "OVERHEAT", 2, !blink);
    const char *a = "SAFE MODE  FAN 100%";
    lode_text(fb, (LODE_W - lode_text_width(a, 1)) / 2, 16, a, 1, true);
    const char *ip = v->ip[0] ? v->ip : "--";
    lode_text(fb, (LODE_W - lode_text_width(ip, 1)) / 2, 25, ip, 1, true);
}

int lode_page_index(lode_screen_t s) { return (s >= LODE_HASH && s <= LODE_POOLS) ? (int) s - LODE_HASH : -1; }

void lode_render(lode_fb_t *fb, lode_screen_t s, const lode_view_t *v, bool blink)
{
    lode_clear(fb);
    switch (s) {
    case LODE_SPLASH: splash(fb, v); break;
    case LODE_HASH: page_hash(fb, v, blink); break;
    case LODE_HEAT: page_heat(fb, v, blink); break;
    case LODE_SHARES: page_shares(fb, v, blink); break;
    case LODE_POOLS: page_pools(fb, v, blink); break;
    case LODE_BLOCK: block(fb, v, blink); break;
    case LODE_OVERHEAT: overheat(fb, v, blink); break;
    default: break;
    }
}
