#include "rollup.h"
#include <string.h>

namespace costean {

uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint8_t q2(float c)
{
    if (c <= 0) return 0;
    float v = c * 2.0f + 0.5f;
    return v > 255 ? 255 : (uint8_t) v;
}

Rollup::Rollup() { memset(m_ring, 0, sizeof m_ring); }

const RollupPoint &Rollup::at(int i) const
{
    int idx = (m_head - m_count + i + kSlots * 2) % kSlots;
    return m_ring[idx];
}

int64_t Rollup::slotTime(int i) const { return (m_lastSlot - (m_count - 1 - i)) * (int64_t) kSlotSec; }

void Rollup::commit(int64_t slot)
{
    // fill gaps (miner was off) with "no data" so the chart shows a break
    if (m_count > 0 && slot > m_lastSlot + 1) {
        int64_t gap = slot - m_lastSlot - 1;
        if (gap > kSlots) gap = kSlots;
        for (int64_t g = 0; g < gap; g++) {
            RollupPoint e{0xFFFF, 0, 0, 0, 0, 255};
            m_ring[m_head] = e; m_head = (m_head + 1) % kSlots; if (m_count < kSlots) m_count++;
        }
    } else if (m_count > 0 && slot <= m_lastSlot) {
        return;  // clock went backwards: drop
    }
    RollupPoint p{};
    float ghs = (float) (m_sGhs / m_n), w = (float) (m_sW / m_n);
    p.ghs = ghs >= 65534 ? 65534 : (uint16_t) (ghs + 0.5f);
    float w10 = w * 10.0f;
    p.watts10 = w10 >= 65535 ? 65535 : (uint16_t) (w10 + 0.5f);
    p.chip2 = q2((float) (m_sChip / m_n));
    p.vr2 = q2((float) (m_sVr / m_n));
    p.flags = m_flags;
    float jth = ghs > 1 ? w / (ghs / 1000.0f) : 0;
    p.eff = (jth <= 0 || jth >= 255) ? 255 : (uint8_t) (jth + 0.5f);
    m_ring[m_head] = p; m_head = (m_head + 1) % kSlots; if (m_count < kSlots) m_count++;
    m_lastSlot = slot;
    m_lastCommit = slot * (int64_t) kSlotSec;
}

void Rollup::add(int64_t utc, float ghs, float watts, float chipC, float vrC, uint8_t flags)
{
    if (utc < 1700000000) return;
    int64_t slot = utc / kSlotSec;
    if (m_n && slot != m_accSlot) {
        commit(m_accSlot);
        m_sGhs = m_sW = m_sChip = m_sVr = 0; m_n = 0; m_flags = 0;
    }
    m_accSlot = slot;
    m_sGhs += ghs < 0 ? 0 : ghs; m_sW += watts < 0 ? 0 : watts; m_sChip += chipC; m_sVr += vrC;
    m_flags |= flags;
    m_n++;
}

static void put32(uint8_t *&p, uint32_t v) { memcpy(p, &v, 4); p += 4; }
static uint32_t get32(const uint8_t *&p) { uint32_t v; memcpy(&v, p, 4); p += 4; return v; }

size_t Rollup::save(uint8_t *buf, size_t len) const
{
    if (len < kBlobSize) return 0;
    uint8_t *p = buf;
    put32(p, kMagic);
    put32(p, (uint32_t) m_lastSlot);
    uint8_t *crcAt = p; p += 4;
    uint16_t n = (uint16_t) m_count; memcpy(p, &n, 2); p += 2;
    for (int i = 0; i < kSlots; i++) {
        RollupPoint e = i < m_count ? at(i) : RollupPoint{0xFFFF, 0, 0, 0, 0, 255};
        memcpy(p, &e, sizeof e); p += sizeof e;
    }
    uint32_t c = crc32(crcAt + 4, kBlobSize - 12);
    memcpy(crcAt, &c, 4);
    return kBlobSize;
}

bool Rollup::restore(const uint8_t *buf, size_t len)
{
    if (!buf || len != kBlobSize) return false;
    const uint8_t *p = buf;
    if (get32(p) != kMagic) return false;
    uint32_t last = get32(p);
    uint32_t c = get32(p);
    if (crc32(p, kBlobSize - 12) != c) return false;
    uint16_t n; memcpy(&n, p, 2); p += 2;
    if (n > kSlots) return false;
    memset(m_ring, 0, sizeof m_ring);
    for (int i = 0; i < n; i++) memcpy(&m_ring[i], p + i * sizeof(RollupPoint), sizeof(RollupPoint));
    m_count = n; m_head = n % kSlots; m_lastSlot = last; m_lastCommit = (int64_t) last * kSlotSec;
    m_n = 0;
    return true;
}

} // namespace costean
