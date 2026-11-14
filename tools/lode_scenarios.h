/* Sample miner states used by the OLED previews and the renderer unit test. */
#pragma once
#include <string.h>
#include "lode_oled.h"

static void lode_sample(lode_view_t *v, int scenario)
{
    memset(v, 0, sizeof(*v));
    v->version = "1.2"; v->board = "602";
    strcpy(v->profile, "MANUAL");
    v->ghs = 1194.0f; v->mhz = 525; v->mv = 1150;
    v->chip_c = 58.4f; v->vr_c = 47.0f; v->power_w = 18.6f; v->fan_pct = 52;
    v->lim_chip = 68; v->lim_vr = 90; v->lim_power = 25;
    strcpy(v->best_session, "48.2M"); strcpy(v->best_all, "1.27G");
    v->acc = 4218; v->rej = 9;
    strcpy(v->ip, "192.168.1.42"); v->uptime_s = 2 * 86400 + 5 * 3600;
    v->npools = 2; v->split = true;
    strcpy(v->pool[0].host, "public-pool.io"); v->pool[0].weight = 70; v->pool[0].eff = 70; v->pool[0].up = true;
    strcpy(v->pool[1].host, "solo.ckpool.org"); v->pool[1].weight = 30; v->pool[1].eff = 30; v->pool[1].up = true;
    switch (scenario) {
    case 1: /* throttle: hot day, power limit reached on Turbo */
        strcpy(v->profile, "TURBO"); v->throttled = true;
        v->mhz = 525; v->mv = 1180; v->ghs = 1176.0f; v->chip_c = 67.6f; v->vr_c = 71.0f; v->power_w = 24.8f; break;
    case 2: /* failover: pool B down, its share moved to A */
        v->pool[1].up = false; v->pool[0].eff = 100; v->pool[1].eff = 0; v->failover = true; break;
    case 3: /* night schedule: Eco chosen by a schedule */
        strcpy(v->profile, "ECO"); v->scheduled = true; v->mhz = 425; v->mv = 1100; v->ghs = 968.0f;
        v->chip_c = 49.0f; v->vr_c = 41.0f; v->power_w = 13.1f; break;
    case 4: /* four pools incl. a 0 % backup */
        v->npools = 4;
        v->pool[0].weight = 50; v->pool[0].eff = 50; v->pool[1].weight = 25; v->pool[1].eff = 25;
        strcpy(v->pool[2].host, "stratum.braiins.com"); v->pool[2].weight = 25; v->pool[2].eff = 25; v->pool[2].up = true;
        strcpy(v->pool[3].host, "pool.example.net"); v->pool[3].weight = 0; v->pool[3].eff = 0; v->pool[3].up = true; break;
    case 5: /* single pool, auto-tune two steps up from stock */
        strcpy(v->profile, "STOCK"); v->tune_steps = 2; v->mhz = 575; v->ghs = 1310.0f; v->power_w = 21.4f;
        v->npools = 1; v->split = false; break;
    default: break;
    }
}
#define LODE_SCENARIOS 6
__attribute__((unused)) static const char *lode_scenario_name[LODE_SCENARIOS] = {"normal", "throttle", "failover", "schedule", "four-pools", "autotune"};
