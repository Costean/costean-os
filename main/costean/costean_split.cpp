#include "costean_split.h"
#include "poolsplit.h"

static costean::PoolSplit s_split;

extern "C" {

void costean_split_setup(const uint8_t *weights, int n)
{
    // A pool configured at 0 % is a backup: it only gets work when every weighted pool is down.
    s_split.setWeights(weights, n);
}

bool costean_split_update(const bool *healthy, uint64_t now_ms, uint8_t *eff_out)
{
    costean::PoolHealth h[costean::kMaxPools];
    for (int i = 0; i < s_split.count(); i++) {
        h[i].configured = true;
        h[i].hasWork = healthy[i];
    }
    bool changed = s_split.update(h, now_ms);
    if (eff_out) {
        for (int i = 0; i < s_split.count(); i++) eff_out[i] = (uint8_t) s_split.effective(i);
    }
    return changed;
}

int costean_split_next(void) { return s_split.next(); }
void costean_split_on_accepted(int slot, double diff, uint64_t now_ms) { s_split.onAccepted(slot, diff, now_ms); }
double costean_split_credited_ghs(int slot, uint64_t now_ms) { return s_split.creditedGhs(slot, now_ms); }
float costean_split_job_share(int slot) { return s_split.jobShare(slot); }
bool costean_split_healthy(int slot) { return s_split.healthy(slot); }
bool costean_split_failover(void) { return s_split.failoverActive(); }
int costean_split_weight(int slot) { return s_split.weight(slot); }

}
