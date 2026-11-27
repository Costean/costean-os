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


/* ---------- big digits ----------
 * 8x14 cells, 2-pixel strokes with open counters and chamfered corners, so the number stays
 * crisp on a 128x32 SSD1306 (no solid blocks that bleed). '.' is 2 px wide. */
static const char *const BIG[12][LODE_BIG_H] = {
    {" ###### ", "########", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "##    ##", "########", " ###### "},
    {"   ###  ", "  ####  ", " ## ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", "    ##  ", " ###### ", " ###### "},
    {" ###### ", "########", "##    ##", "      ##", "      ##", "     ###", "   #### ", " ####   ", "###     ", "##      ", "##      ", "##      ", "########", "########"},
    {" ###### ", "########", "##    ##", "      ##", "      ##", "      ##", "  ##### ", "  ##### ", "      ##", "      ##", "      ##", "##    ##", "########", " ###### "},
    {"    ### ", "   #### ", "  ## ## ", " ##  ## ", "##   ## ", "##   ## ", "########", "########", "     ## ", "     ## ", "     ## ", "     ## ", "     ## ", "     ## "},
    {"########", "########", "##      ", "##      ", "##      ", "##      ", "####### ", "########", "      ##", "      ##", "      ##", "##    ##", "########", " ###### "},
    {" ###### ", "########", "##      ", "##      ", "##      ", "##      ", "####### ", "########", "##    ##", "##    ##", "##    ##", "##    ##", "########", " ###### "},
    {"########", "########", "      ##", "      ##", "     ## ", "     ## ", "    ##  ", "    ##  ", "   ##   ", "   ##   ", "   ##   ", "   ##   ", "   ##   ", "   ##   "},
    {" ###### ", "########", "##    ##", "##    ##", "##    ##", "##    ##", " ###### ", " ###### ", "##    ##", "##    ##", "##    ##", "##    ##", "########", " ###### "},
    {" ###### ", "########", "##    ##", "##    ##", "##    ##", "##    ##", "########", " #######", "      ##", "      ##", "      ##", "      ##", "########", " ###### "},
    {"        ", "        ", "        ", "        ", "        ", "        ", "        ", "        ", "        ", "        ", "        ", "        ", "##      ", "##      "},
    {"        ", "        ", "        ", "        ", "        ", "        ", " ###### ", " ###### ", "        ", "        ", "        ", "        ", "        ", "        "},
};
static int big_idx(char c) { return c >= '0' && c <= '9' ? c - '0' : c == '.' ? 10 : c == '-' ? 11 : -1; }
static int big_adv(char c) { return c == '.' ? 4 : c == ' ' ? 5 : 10; }

int lode_big_width(const char *s)
{
    int w = 0;
    for (; s && *s; s++) w += big_adv(*s);
    return w > 0 ? w - 2 : 0;   /* no gap after the last glyph */
}

int lode_big(lode_fb_t *fb, int x, int y, const char *s)
{
    int x0 = x;
    for (; s && *s; s++) {
        int g = big_idx(*s);
        if (g >= 0)
            for (int r = 0; r < LODE_BIG_H; r++)
                for (int c = 0; c < 8; c++)
                    if (BIG[g][r][c] == '#') lode_px(fb, x + c, y + r, true);
        x += big_adv(*s);
    }
    return x - x0;
}

/* ---------- layout record (tests check that no two texts overlap) ---------- */
static lode_box_t s_boxes[LODE_MAX_BOXES];
static int s_nbox;
static void box(int x, int y, int w, int h) { if (s_nbox < LODE_MAX_BOXES && w > 0) s_boxes[s_nbox++] = (lode_box_t) {x, y, w, h}; }
int lode_boxes(const lode_box_t **out) { if (out) *out = s_boxes; return s_nbox; }

/* text in the 6x8 font: the glyphs use 5 columns, so the box is 1 px narrower than the advance */
static void txt(lode_fb_t *fb, int x, int y, const char *s) { int w = lode_text(fb, x, y, s, 1, true); box(x, y, w - 1, 7); }
static void rtxt(lode_fb_t *fb, int y, const char *s) { txt(fb, LODE_W - lode_text_width(s, 1) + 1, y, s); }
static void ctxt(lode_fb_t *fb, int y, const char *s) { txt(fb, (LODE_W - lode_text_width(s, 1) + 1) / 2, y, s); }
static void big(lode_fb_t *fb, int x, int y, const char *s) { int w = lode_big_width(s); lode_big(fb, x, y, s); box(x, y, w, LODE_BIG_H); }
static void big2(lode_fb_t *fb, int x, int y, const char *s) { int w = lode_text(fb, x, y, s, 2, true); box(x, y, w - 2, 14); }

/* ---------- shared top line: page name left, profile right (plain text, like the stock pages) ---------- */
static void clock_icon(lode_fb_t *fb, int x, int y)
{
    static const uint8_t ic[7] = {0x38, 0x44, 0x92, 0x9a, 0x82, 0x44, 0x38};
    for (int r = 0; r < 7; r++) for (int c = 0; c < 7; c++) if (ic[r] & (0x80 >> c)) lode_px(fb, x + c, y + r, true);
    box(x, y, 7, 7);
}

static void header(lode_fb_t *fb, const lode_view_t *v, const char *title, const char *tagOverride)
{
    txt(fb, 0, 0, title);
    char tag[20];
    if (tagOverride) snprintf(tag, sizeof tag, "%s", tagOverride);
    else if (v->tune_steps > 0) snprintf(tag, sizeof tag, "%.9s+%d", v->profile, v->tune_steps > 9 ? 9 : v->tune_steps);
    else snprintf(tag, sizeof tag, "%s", v->profile[0] ? v->profile : "--");
    int x0 = LODE_W - lode_text_width(tag, 1) + 1;
    rtxt(fb, 0, tag);
    if (v->scheduled && !tagOverride && x0 - 10 > lode_text_width(title, 1) + 2) clock_icon(fb, x0 - 10, 0);
}

/* throttling: the profile name and the word THROTTLE take turns (text only, nothing inverted) */
static const char *tag_for(const lode_view_t *v, bool blink) { return (v->throttled && blink) ? "THROTTLE" : NULL; }

/* ---------- screens ---------- */
void lode_short_version(const char *ver, char *out, int n)
{
    if (!ver || !ver[0]) { snprintf(out, n, "?"); return; }
    const char *c = strstr(ver, "costean.");
    if (c) snprintf(out, n, "c%.4s", c + 8);         /* "1.2.0-costean.5" -> "c5" */
    else snprintf(out, n, "%.6s", ver);
}

/* boot screen: the full firmware version, so an update can be checked on the miner itself */
static void splash(lode_fb_t *fb, const lode_view_t *v)
{
    lode_diamond(fb, 14, 15, 13, 1); box(1, 2, 27, 27);
    big2(fb, 36, 0, "COSTEAN");
    char a[40], b[16];
    snprintf(b, sizeof b, "GAMMA %s", v->board ? v->board : "602");
    txt(fb, 36, 16, b);
    snprintf(a, sizeof a, "%s", v->version && v->version[0] ? v->version : "?");
    if (36 + lode_text_width(a, 1) > LODE_W) {      /* too long: "OS c5" */
        char sv[8]; lode_short_version(v->version, sv, sizeof sv);
        snprintf(a, sizeof a, "OS %s", sv);
    }
    txt(fb, 36, 24, a);
}

static void page_hash(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    header(fb, v, "HASH", tag_for(v, blink));
    char num[12]; const char *unit;
    lode_fmt_hashrate(v->ghs, num, sizeof num, &unit);
    big(fb, 0, 9, num);
    txt(fb, lode_big_width(num) + 4, 16, unit);
    char l[24], t[8];
    if (v->tuner_line[0]) snprintf(l, sizeof l, "%s", v->tuner_line);
    else snprintf(l, sizeof l, "%dMHz %dmV", v->mhz, v->mv);
    snprintf(t, sizeof t, "%dC", (int) (v->chip_c + 0.5f));
    txt(fb, 0, 25, l);
    if (lode_text_width(l, 1) + lode_text_width(t, 1) + 6 <= LODE_W) rtxt(fb, 25, t);
}

static void heat_line(lode_fb_t *fb, int y, const char *label, float val, int lim, const char *unit, int dec)
{
    char a[20], b[16];
    if (dec) snprintf(a, sizeof a, "%-5s %.1f%s", label, val, unit); else snprintf(a, sizeof a, "%-5s %d%s", label, (int) (val + 0.5f), unit);
    bool near = lim > 0 && val >= lim - 3;
    snprintf(b, sizeof b, "%s %d%s", near ? "NEAR" : "max", lim, unit);
    txt(fb, 0, y, a);
    if (lode_text_width(a, 1) + lode_text_width(b, 1) + 6 <= LODE_W) rtxt(fb, y, b);
}

static void page_heat(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    const char *htag = tag_for(v, blink);
    if (!htag && v->tuner_line[0]) htag = v->tuner_line;
    header(fb, v, "HEAT", htag);
    heat_line(fb, 8, "CHIP", v->chip_c, v->lim_chip, "C", 0);
    heat_line(fb, 16, "VR", v->vr_c, v->lim_vr, "C", 0);
    heat_line(fb, 24, "POWER", v->power_w, v->lim_power, "W", 1);
}

static void page_shares(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    header(fb, v, "SHARES", tag_for(v, blink));
    char l[40], r[24];
    snprintf(l, sizeof l, "BEST %s", v->best_session[0] ? v->best_session : "--");
    snprintf(r, sizeof r, "ALL %s", v->best_all[0] ? v->best_all : "--");
    txt(fb, 0, 8, l);
    if (lode_text_width(l, 1) + lode_text_width(r, 1) + 6 <= LODE_W) rtxt(fb, 8, r);
    unsigned long acc = v->acc > 999999 ? 999999 : v->acc, rej = v->rej > 99999 ? 99999 : v->rej;
    float rp = (v->acc + v->rej) ? 100.0f * v->rej / (float) (v->acc + v->rej) : 0;
    snprintf(l, sizeof l, "OK %lu BAD %lu", acc, rej);
    snprintf(r, sizeof r, "%.1f%%", rp);
    txt(fb, 0, 16, l);
    if (lode_text_width(l, 1) + lode_text_width(r, 1) + 6 <= LODE_W) rtxt(fb, 16, r);
    /* bottom right: uptime and the firmware version ("OS c5") take turns */
    char up[12], sv[8];
    if (blink) { lode_short_version(v->version, sv, sizeof sv); snprintf(up, sizeof up, "OS %s", sv); }
    else fmt_uptime(v->uptime_s, up, sizeof up);
    const char *ip = v->ip[0] ? v->ip : "no IP";
    txt(fb, 0, 24, ip);
    if (lode_text_width(ip, 1) + lode_text_width(up, 1) + 6 <= LODE_W) rtxt(fb, 24, up);
}

static void page_pools(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    const char *tag = tag_for(v, blink);
    if (v->failover && !blink) tag = "FAILOVER";
    header(fb, v, "POOLS", tag);
    int n = v->npools > LODE_MAX_POOLS ? LODE_MAX_POOLS : v->npools;
    char l[48], h[24];
    if (!v->split || n < 1) {
        lode_short_host(n > 0 ? v->pool[0].host : "", h, sizeof h);
        snprintf(l, sizeof l, "%.14s", h[0] ? h : "no pool");
        txt(fb, 0, 8, l);
        rtxt(fb, 8, n > 0 && v->pool[0].up ? "UP" : "DOWN");
        snprintf(l, sizeof l, "OK %lu BAD %lu", (unsigned long) v->acc, (unsigned long) v->rej);
        txt(fb, 0, 16, l);
        txt(fb, 0, 24, "SPLIT OFF");
        return;
    }
    if (n <= 3) {
        for (int i = 0; i < n; i++) {
            const lode_pool_t *p = &v->pool[i];
            int y = 8 + i * 8;
            lode_short_host(p->host, h, sizeof h);
            const char *st = !p->up ? "DOWN" : p->weight == 0 ? "SPARE" : p->eff != p->weight ? "+FO" : "OK";
            snprintf(l, sizeof l, "%3d%% %.10s", p->up ? p->eff : 0, h);
            txt(fb, 0, y, l);
            rtxt(fb, y, st);
        }
        if (n <= 2) {
            snprintf(l, sizeof l, "OK %lu BAD %lu", (unsigned long) v->acc, (unsigned long) v->rej);
            txt(fb, 0, 24, l);
        }
        return;
    }
    for (int i = 0; i < 4; i++) {
        const lode_pool_t *p = &v->pool[i];
        lode_short_host(p->host, h, sizeof h);
        if (p->up) snprintf(l, sizeof l, "%3d%% %.4s", p->eff, h); else snprintf(l, sizeof l, "DOWN %.4s", h);
        txt(fb, (i & 1) * 66, 8 + (i / 2) * 8, l);
    }
    snprintf(l, sizeof l, "OK %lu BAD %lu", (unsigned long) v->acc, (unsigned long) v->rej);
    txt(fb, 0, 24, l);
}

static void block(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    lode_diamond(fb, 14, 15, 13, 1); box(1, 2, 27, 27);
    big2(fb, 36, 1, "BLOCK!");
    char l[32];
    snprintf(l, sizeof l, "FOUND %s", v->best_session[0] ? v->best_session : "");
    txt(fb, 36, 22, l);
    if (blink) lode_diamond(fb, 14, 15, 9, 1);   /* a second thin ring pulses inside the stone */
}

static void overheat(lode_fb_t *fb, const lode_view_t *v, bool blink)
{
    big2(fb, (LODE_W - lode_text_width("OVERHEAT", 2) + 2) / 2, 0, "OVERHEAT");
    ctxt(fb, 16, blink ? "SAFE MODE" : "FAN 100%");
    ctxt(fb, 24, v->ip[0] ? v->ip : "--");
}

int lode_page_index(lode_screen_t s) { return (s >= LODE_HASH && s <= LODE_POOLS) ? (int) s - LODE_HASH : -1; }

void lode_render(lode_fb_t *fb, lode_screen_t s, const lode_view_t *v, bool blink)
{
    lode_clear(fb);   /* every frame starts from black: no leftovers from the previous page */
    s_nbox = 0;
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
