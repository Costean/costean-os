#pragma once
// Costean OS multi-pool split: percentage split of ASIC work between up to
// kMaxPools pools, with failover that hands a dead pool's share to the healthy
// ones. Pure logic (no ESP-IDF), unit tested on the host (test/costean).
#include <stdint.h>

namespace costean {

constexpr int kMaxPools = 4;

struct PoolHealth {
    bool configured = false; // pool has a host set
    bool hasWork = false;    // connected and has a valid mining.notify
    bool blocked = false;    // e.g. coinbase verification blocked the pool
};

class PoolSplit {
  public:
    static constexpr uint32_t kRejoinMs = 30000;    // healthy this long before a pool gets its share back
    static constexpr uint32_t kWindowMs = 600000;   // 10 min window for pool-credited hashrate
    static constexpr int kBuckets = 60;             // 10 s buckets

    PoolSplit();

    // Configured split in percent. Values are clamped and normalised so they sum to 100.
    // If all are 0 the first pool gets 100.
    void setWeights(const uint8_t *pct, int n);
    int count() const { return m_n; }
    int weight(int i) const { return valid(i) ? m_weight[i] : 0; }

    // Feed the current health of each pool. Returns true when the effective split changed.
    bool update(const PoolHealth *h, uint64_t nowMs);

    // Effective share after failover (sums to 100 when at least one pool is healthy, else all 0).
    int effective(int i) const { return valid(i) ? m_eff[i] : 0; }
    bool healthy(int i) const { return valid(i) && m_healthy[i]; }
    bool failoverActive() const;   // some configured share is being carried by another pool

    // Pool for the next ASIC job (smooth weighted round robin). -1 when no pool has work.
    int next();
    uint64_t jobs(int i) const { return valid(i) ? m_jobs[i] : 0; }
    // Fraction of jobs (0..1) sent to pool i over the recent past (exponential average).
    float jobShare(int i) const { return valid(i) ? m_jobShare[i] : 0.0f; }

    // Pool-credited work, used for a per-pool hashrate estimate.
    void onAccepted(int i, double poolDiff, uint64_t nowMs);
    // GH/s credited by pool i over the window (difficulty * 2^32 / seconds).
    double creditedGhs(int i, uint64_t nowMs) const;

    void resetStats(int i);

  private:
    bool valid(int i) const { return i >= 0 && i < m_n; }
    void recompute();
    void roll(int i, uint64_t nowMs) const;

    int m_n = 2;
    uint8_t m_weight[kMaxPools]{};
    uint8_t m_eff[kMaxPools]{};
    bool m_healthy[kMaxPools]{};
    bool m_configured[kMaxPools]{};
    uint64_t m_healthySince[kMaxPools]{};
    bool m_seenHealth[kMaxPools]{};
    int32_t m_cur[kMaxPools]{};
    uint64_t m_jobs[kMaxPools]{};
    float m_jobShare[kMaxPools]{};

    // credited difficulty ring per pool
    mutable double m_bucket[kMaxPools][kBuckets]{};
    mutable uint64_t m_bucketEpoch[kMaxPools]{};   // index of the newest bucket (nowMs / bucket length)
    mutable uint64_t m_firstMs[kMaxPools]{};
};

} // namespace costean
