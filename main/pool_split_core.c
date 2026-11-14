#include "pool_split_core.h"

#include <string.h>

pool_split_error_t pool_split_validate(const pool_split_config_t *cfg, bool enabled)
{
    if (!cfg) {
        return POOL_SPLIT_ERR_EMPTY;
    }
    if (!enabled) {
        return POOL_SPLIT_ERR_DISABLED;
    }

    int participants = 0;
    unsigned sum = 0;
    int n = cfg->count;
    if (n > COSTEAN_MAX_SPLIT_POOLS) {
        n = COSTEAN_MAX_SPLIT_POOLS;
    }

    for (int i = 0; i < n; i++) {
        if (cfg->percent[i] == 0) {
            continue;
        }
        if (!cfg->has_url[i]) {
            return POOL_SPLIT_ERR_EMPTY;
        }
        if (cfg->is_sv2[i]) {
            return POOL_SPLIT_ERR_SV2;
        }
        if (cfg->percent[i] > 100) {
            return POOL_SPLIT_ERR_SUM;
        }
        participants++;
        sum += cfg->percent[i];
    }

    if (participants < 2) {
        return POOL_SPLIT_ERR_TOO_FEW;
    }
    if (participants > COSTEAN_MAX_SPLIT_POOLS) {
        return POOL_SPLIT_ERR_TOO_MANY;
    }
    if (sum != 100) {
        return POOL_SPLIT_ERR_SUM;
    }
    return POOL_SPLIT_OK;
}

const char *pool_split_error_string(pool_split_error_t err)
{
    switch (err) {
        case POOL_SPLIT_OK:
            return "ok";
        case POOL_SPLIT_ERR_DISABLED:
            return "split disabled";
        case POOL_SPLIT_ERR_TOO_FEW:
            return "split needs 2 to 4 pools with a percentage";
        case POOL_SPLIT_ERR_TOO_MANY:
            return "split supports at most 4 pools";
        case POOL_SPLIT_ERR_SUM:
            return "split percentages must add up to 100";
        case POOL_SPLIT_ERR_SV2:
            return "multi-pool split is not available with Stratum V2; use V1 pools or disable the split";
        case POOL_SPLIT_ERR_EMPTY:
            return "a participating pool is missing a URL";
        default:
            return "invalid split";
    }
}

void pool_split_redistribute(const uint16_t *configured, const bool *connected,
                             uint16_t *effective, int n)
{
    if (!configured || !connected || !effective || n <= 0) {
        return;
    }

    unsigned live_cfg = 0;
    int last = -1;
    for (int i = 0; i < n; i++) {
        if (connected[i] && configured[i] > 0) {
            live_cfg += configured[i];
            last = i;
        }
    }

    if (live_cfg == 0) {
        for (int i = 0; i < n; i++) {
            effective[i] = 0;
        }
        return;
    }

    unsigned assigned = 0;
    for (int i = 0; i < n; i++) {
        if (connected[i] && configured[i] > 0) {
            effective[i] = (uint16_t)((configured[i] * 100u) / live_cfg);
            assigned += effective[i];
        } else {
            effective[i] = 0;
        }
    }
    /* Give leftover percent (from integer division) to the last live pool. */
    if (last >= 0 && assigned < 100) {
        effective[last] = (uint16_t)(effective[last] + (100 - assigned));
    }
}

int pool_split_pick(const uint16_t *effective_pct, const bool *ready,
                    const uint32_t *jobs_sent, int n, uint32_t total_sent)
{
    if (!effective_pct || !ready || !jobs_sent || n <= 0) {
        return -1;
    }

    int best = -1;
    double best_score = -1e300;
    for (int i = 0; i < n; i++) {
        if (!ready[i] || effective_pct[i] == 0) {
            continue;
        }
        double target = (double)effective_pct[i] / 100.0;
        double actual = (total_sent > 0) ? ((double)jobs_sent[i] / (double)total_sent) : 0.0;
        double score = target - actual;
        /* Stable tie-break: prefer a pool that has received fewer jobs. */
        score += (1.0 / (double)(jobs_sent[i] + 1u)) * 1e-9;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}
