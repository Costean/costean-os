#include "poolsplit.h"

namespace costean {

static constexpr uint32_t kBucketMs = PoolSplit::kWindowMs / PoolSplit::kBuckets;

PoolSplit::PoolSplit()
{
    uint8_t w[2] = {100, 0};
    setWeights(w, 2);
}

void PoolSplit::setWeights(const uint8_t *pct, int n)
{
    if (n < 1) n = 1;
    if (n > kMaxPools) n = kMaxPools;
    m_n = n;
    uint32_t sum = 0;
    for (int i = 0; i < kMaxPools; i++) {
        m_weight[i] = (i < n && pct) ? (pct[i] > 100 ? 100 : pct[i]) : 0;
        sum += m_weight[i];
    }
    if (sum == 0) {
        m_weight[0] = 100;
    } else if (sum != 100) {
        // largest-remainder normalisation to exactly 100
        uint32_t acc = 0, rem[kMaxPools]{};
        for (int i = 0; i < n; i++) {
            uint32_t scaled = (uint32_t) m_weight[i] * 100u;
            rem[i] = scaled % sum;
            m_weight[i] = (uint8_t) (scaled / sum);
            acc += m_weight[i];
        }
        while (acc < 100) {
            int best = 0;
            for (int i = 1; i < n; i++) if (rem[i] > rem[best]) best = i;
            m_weight[best]++; rem[best] = 0; acc++;
        }
    }
    for (int i = 0; i < kMaxPools; i++) m_cur[i] = 0;
    recompute();
}

bool PoolSplit::failoverActive() const
{
    for (int i = 0; i < m_n; i++) {
        if (m_weight[i] > 0 && !m_healthy[i]) return true;
    }
    return false;
}

bool PoolSplit::update(const PoolHealth *h, uint64_t nowMs)
{
    bool changed = false;
    for (int i = 0; i < m_n; i++) {
        bool up = h[i].configured && h[i].hasWork && !h[i].blocked;
        m_configured[i] = h[i].configured;
        bool wasHealthy = m_healthy[i];
        if (!up) {
            m_healthySince[i] = 0;
            m_seenHealth[i] = true;
            m_healthy[i] = false;
        } else {
            if (m_healthySince[i] == 0) m_healthySince[i] = nowMs ? nowMs : 1;
            // First time we see a pool up (boot) it is used at once; after an outage it must
            // stay up for kRejoinMs so a flapping pool does not steal work every few seconds.
            bool firstUp = !m_seenHealth[i];
            if (firstUp || nowMs - m_healthySince[i] >= kRejoinMs) m_healthy[i] = true;
        }
        if (wasHealthy != m_healthy[i]) changed = true;
    }
    if (changed) recompute();
    return changed;
}

void PoolSplit::recompute()
{
    uint32_t live = 0, liveCount = 0;
    for (int i = 0; i < m_n; i++) {
        if (m_healthy[i]) { live += m_weight[i]; liveCount++; }
    }
    for (int i = 0; i < kMaxPools; i++) m_eff[i] = 0;
    if (liveCount == 0) return;
    if (live == 0) {
        // only zero-weight (backup) pools are healthy: split evenly between them
        uint32_t base = 100 / liveCount, extra = 100 % liveCount;
        for (int i = 0; i < m_n; i++) {
            if (!m_healthy[i]) continue;
            m_eff[i] = (uint8_t) (base + (extra ? 1 : 0));
            if (extra) extra--;
        }
        return;
    }
    // dead pools' share goes to the healthy pools in proportion to their own share
    uint32_t acc = 0, rem[kMaxPools]{};
    for (int i = 0; i < m_n; i++) {
        if (!m_healthy[i]) continue;
        uint32_t scaled = (uint32_t) m_weight[i] * 100u;
        m_eff[i] = (uint8_t) (scaled / live);
        rem[i] = scaled % live;
        acc += m_eff[i];
    }
    while (acc < 100) {
        int best = -1;
        for (int i = 0; i < m_n; i++) {
            if (!m_healthy[i] || m_weight[i] == 0) continue;
            if (best < 0 || rem[i] > rem[best]) best = i;
        }
        m_eff[best]++; rem[best] = 0; acc++;
    }
    for (int i = 0; i < kMaxPools; i++) m_cur[i] = 0;
}

int PoolSplit::next()
{
    int best = -1;
    for (int i = 0; i < m_n; i++) {
        if (m_eff[i] == 0) continue;
        m_cur[i] += m_eff[i];
        if (best < 0 || m_cur[i] > m_cur[best]) best = i;
    }
    if (best < 0) return -1;
    m_cur[best] -= 100;
    m_jobs[best]++;
    const float a = 1.0f / 256.0f;
    for (int i = 0; i < m_n; i++) m_jobShare[i] += a * ((i == best ? 1.0f : 0.0f) - m_jobShare[i]);
    return best;
}

void PoolSplit::roll(int i, uint64_t nowMs) const
{
    uint64_t ep = nowMs / kBucketMs;
    if (m_bucketEpoch[i] == 0 && m_firstMs[i] == 0) { m_bucketEpoch[i] = ep; return; }
    if (ep <= m_bucketEpoch[i]) return;
    uint64_t steps = ep - m_bucketEpoch[i];
    if (steps > (uint64_t) kBuckets) steps = kBuckets;
    for (uint64_t s = 1; s <= steps; s++) m_bucket[i][(m_bucketEpoch[i] + s) % kBuckets] = 0;
    m_bucketEpoch[i] = ep;
}

void PoolSplit::onAccepted(int i, double poolDiff, uint64_t nowMs)
{
    if (!valid(i) || poolDiff <= 0) return;
    roll(i, nowMs);
    if (m_firstMs[i] == 0) m_firstMs[i] = nowMs ? nowMs : 1;
    m_bucket[i][m_bucketEpoch[i] % kBuckets] += poolDiff;
}

double PoolSplit::creditedGhs(int i, uint64_t nowMs) const
{
    if (!valid(i) || m_firstMs[i] == 0) return 0.0;
    roll(i, nowMs);
    double sum = 0;
    for (int b = 0; b < kBuckets; b++) sum += m_bucket[i][b];
    uint64_t span = nowMs - m_firstMs[i];
    if (span > kWindowMs) span = kWindowMs;
    if (span < 60000) span = 60000; // avoid huge numbers from the first share
    return sum * 4294967296.0 / ((double) span / 1000.0) / 1e9;
}

void PoolSplit::resetStats(int i)
{
    if (!valid(i)) return;
    m_jobs[i] = 0;
    m_jobShare[i] = 0;
    m_firstMs[i] = 0;
    m_bucketEpoch[i] = 0;
    for (int b = 0; b < kBuckets; b++) m_bucket[i][b] = 0;
}

} // namespace costean
