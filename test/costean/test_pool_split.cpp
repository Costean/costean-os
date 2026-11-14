#include "check.h"
#include "../../main/costean/poolsplit.h"
using namespace costean;

static PoolHealth up() { PoolHealth h; h.configured = true; h.hasWork = true; return h; }
static PoolHealth down() { PoolHealth h; h.configured = true; h.hasWork = false; return h; }

static void count(PoolSplit &s, int n, int *out) { for (int i = 0; i < kMaxPools; i++) out[i] = 0; for (int i = 0; i < n; i++) { int p = s.next(); if (p >= 0) out[p]++; } }

TEST(split_70_30_exact_over_100_jobs) {
    PoolSplit s; uint8_t w[2] = {70, 30}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 1000);
    int c[4]; count(s, 1000, c);
    CHECK_EQ(c[0], 700); CHECK_EQ(c[1], 300);
}

TEST(split_is_interleaved_not_bursty) {
    PoolSplit s; uint8_t w[2] = {50, 50}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 1);
    int last = -1, run = 0, maxRun = 0;
    for (int i = 0; i < 200; i++) { int p = s.next(); run = p == last ? run + 1 : 1; last = p; if (run > maxRun) maxRun = run; }
    CHECK_EQ(maxRun, 1);
}

TEST(weights_normalise_to_100) {
    PoolSplit s; uint8_t w[3] = {1, 1, 1}; s.setWeights(w, 3);
    CHECK_EQ(s.weight(0) + s.weight(1) + s.weight(2), 100);
    uint8_t z[2] = {0, 0}; s.setWeights(z, 2);
    CHECK_EQ(s.weight(0), 100); CHECK_EQ(s.weight(1), 0);
    uint8_t big[2] = {200, 100}; s.setWeights(big, 2);  // clamp to 100/100 -> 50/50
    CHECK_EQ(s.weight(0), 50); CHECK_EQ(s.weight(1), 50);
}

TEST(failover_moves_dead_share) {
    PoolSplit s; uint8_t w[2] = {70, 30}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 1000);
    h[1] = down(); CHECK(s.update(h, 2000));
    CHECK_EQ(s.effective(0), 100); CHECK_EQ(s.effective(1), 0); CHECK(s.failoverActive());
    int c[4]; count(s, 100, c); CHECK_EQ(c[0], 100); CHECK_EQ(c[1], 0);
    h[0] = down(); h[1] = up(); s.update(h, 3000);
    // pool 1 was seen down before, so it must be up for 30 s first
    CHECK_EQ(s.next(), -1);
    s.update(h, 3000 + PoolSplit::kRejoinMs);
    CHECK_EQ(s.effective(1), 100);
}

TEST(rejoin_needs_30s_of_health) {
    PoolSplit s; uint8_t w[2] = {50, 50}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 0);
    h[0] = down(); s.update(h, 1000);
    h[0] = up(); s.update(h, 2000);
    CHECK_EQ(s.effective(0), 0);         // flapping pool waits
    s.update(h, 2000 + PoolSplit::kRejoinMs - 1); CHECK_EQ(s.effective(0), 0);
    s.update(h, 2000 + PoolSplit::kRejoinMs); CHECK_EQ(s.effective(0), 50); CHECK_EQ(s.effective(1), 50);
    h[0] = down(); s.update(h, 40000); h[0] = up(); s.update(h, 45000); h[0] = down(); s.update(h, 50000); h[0] = up(); s.update(h, 60000);
    CHECK_EQ(s.effective(0), 0);          // timer restarted by the second drop
    s.update(h, 60000 + PoolSplit::kRejoinMs); CHECK_EQ(s.effective(0), 50);
}

TEST(four_pools_proportional_redistribution) {
    PoolSplit s; uint8_t w[4] = {40, 30, 20, 10}; s.setWeights(w, 4);
    PoolHealth h[4] = {up(), up(), up(), up()}; s.update(h, 1);
    int c[4]; count(s, 1000, c);
    CHECK_EQ(c[0], 400); CHECK_EQ(c[1], 300); CHECK_EQ(c[2], 200); CHECK_EQ(c[3], 100);
    h[0] = down(); s.update(h, 2);   // 40% moves to the others 30:20:10
    CHECK_EQ(s.effective(1), 50); CHECK_EQ(s.effective(2), 33); CHECK_EQ(s.effective(3), 17);
    CHECK_EQ(s.effective(0) + s.effective(1) + s.effective(2) + s.effective(3), 100);
}

TEST(zero_weight_pool_is_backup_only) {
    PoolSplit s; uint8_t w[2] = {100, 0}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 1);
    int c[4]; count(s, 50, c); CHECK_EQ(c[1], 0);
    h[0] = down(); s.update(h, 2);
    CHECK_EQ(s.effective(1), 100);
    count(s, 50, c); CHECK_EQ(c[1], 50);
}

TEST(blocked_or_unconfigured_pool_gets_nothing) {
    PoolSplit s; uint8_t w[2] = {50, 50}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; h[1].blocked = true; s.update(h, 1);
    CHECK_EQ(s.effective(0), 100);
    PoolHealth u[2] = {up(), PoolHealth{}}; s.update(u, 2); CHECK_EQ(s.effective(1), 0);
}

TEST(all_dead_returns_minus_one) {
    PoolSplit s; uint8_t w[2] = {60, 40}; s.setWeights(w, 2);
    PoolHealth h[2] = {down(), down()}; s.update(h, 5);
    CHECK_EQ(s.next(), -1); CHECK_EQ(s.effective(0), 0); CHECK_EQ(s.effective(1), 0);
}

TEST(credited_hashrate_from_pool_difficulty) {
    PoolSplit s; uint8_t w[2] = {50, 50}; s.setWeights(w, 2);
    // 1 share of diff 1000 every 10 s for 10 min = 60 shares -> 1000*2^32/10 s = 429.5 GH/s
    uint64_t t = 1000000;
    for (int i = 0; i < 60; i++) { s.onAccepted(0, 1000, t); t += 10000; }
    double g = s.creditedGhs(0, t);
    CHECK(g > 400 && g < 460);
    CHECK_EQ((int) s.creditedGhs(1, t), 0);
    // window slides: 20 minutes later nothing is left
    CHECK_EQ((int) s.creditedGhs(0, t + 20 * 60000), 0);
}

TEST(job_share_tracks_split) {
    PoolSplit s; uint8_t w[2] = {80, 20}; s.setWeights(w, 2);
    PoolHealth h[2] = {up(), up()}; s.update(h, 1);
    for (int i = 0; i < 5000; i++) s.next();
    CHECK(s.jobShare(0) > 0.75f && s.jobShare(0) < 0.85f);
    CHECK_EQ(s.jobs(0) + s.jobs(1), 5000);
}

int main() { return run_all("pool_split"); }
