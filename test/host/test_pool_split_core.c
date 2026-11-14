#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../main/pool_split_core.c"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    }
}

int main(void)
{
    pool_split_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.count = 4;
    cfg.has_url[0] = cfg.has_url[1] = true;
    cfg.percent[0] = 50;
    cfg.percent[1] = 50;
    expect(pool_split_validate(&cfg, false) == POOL_SPLIT_ERR_DISABLED, "disabled");
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_OK, "50/50 ok");

    cfg.percent[1] = 40;
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_ERR_SUM, "sum 90 rejected");
    cfg.percent[1] = 50;

    cfg.is_sv2[1] = true;
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_ERR_SV2, "sv2 rejected");
    cfg.is_sv2[1] = false;

    cfg.percent[1] = 0;
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_ERR_TOO_FEW, "single participant rejected");
    cfg.percent[1] = 50;

    cfg.has_url[1] = false;
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_ERR_EMPTY, "missing url rejected");
    cfg.has_url[1] = true;

    cfg.percent[0] = 25;
    cfg.percent[1] = 25;
    cfg.percent[2] = 25;
    cfg.percent[3] = 25;
    cfg.has_url[2] = cfg.has_url[3] = true;
    expect(pool_split_validate(&cfg, true) == POOL_SPLIT_OK, "25x4 ok");

    /* redistribute: 40/60 with first pool down -> second gets 100 */
    uint16_t configured[4] = {40, 60, 0, 0};
    bool connected[4] = {false, true, false, false};
    uint16_t effective[4] = {0};
    pool_split_redistribute(configured, connected, effective, 4);
    expect(effective[0] == 0 && effective[1] == 100, "failover 40/60 -> 0/100");

    connected[0] = true;
    pool_split_redistribute(configured, connected, effective, 4);
    expect(effective[0] == 40 && effective[1] == 60, "both live keep 40/60");

    /* 1/3 + 1/3 + 1/3 leftover goes to last live */
    uint16_t c3[3] = {33, 33, 34};
    bool live3[3] = {true, true, true};
    uint16_t e3[3] = {0};
    pool_split_redistribute(c3, live3, e3, 3);
    expect(e3[0] + e3[1] + e3[2] == 100, "33/33/34 sums to 100");

    /* scheduler converges toward 70/30 */
    uint16_t pct[2] = {70, 30};
    bool ready[2] = {true, true};
    uint32_t sent[2] = {0, 0};
    uint32_t total = 0;
    for (int i = 0; i < 1000; i++) {
        int p = pool_split_pick(pct, ready, sent, 2, total);
        expect(p == 0 || p == 1, "pick returns a live pool");
        if (p < 0) break;
        sent[p]++;
        total++;
    }
    /* allow a little slack from the discrete scheduler */
    expect(sent[0] >= 680 && sent[0] <= 720, "1000 jobs ~70% on pool 0");
    expect(sent[1] >= 280 && sent[1] <= 320, "1000 jobs ~30% on pool 1");

    /* downed pool is never picked */
    ready[0] = false;
    uint32_t sent2[2] = {0, 0};
    for (int i = 0; i < 50; i++) {
        int p = pool_split_pick(pct, ready, sent2, 2, sent2[0] + sent2[1]);
        expect(p == 1, "only ready pool is picked");
        if (p >= 0) sent2[p]++;
    }

    if (g_fail) {
        fprintf(stderr, "%d failure(s)\n", g_fail);
        return 1;
    }
    printf("all pool_split_core tests passed\n");
    return 0;
}
