#pragma once
// Costean OS long history: 15-minute averages for 48 hours, small enough to keep in
// NVS so the charts survive a reboot or a firmware update. Pure logic, host tested.
#include <stddef.h>
#include <stdint.h>

namespace costean {

#pragma pack(push, 1)
struct RollupPoint {
    uint16_t ghs;      // average GH/s (0xFFFF = no data)
    uint16_t watts10;  // average W * 10
    uint8_t chip2;     // hottest chip, degC * 2
    uint8_t vr2;       // regulator, degC * 2
    uint8_t flags;     // bit0 throttled, bit1 failover, bit2 schedule active, bit3 auto-tune above profile
    uint8_t eff;       // J/TH (rounded, 255 = n/a)
};
#pragma pack(pop)

class Rollup {
  public:
    static constexpr int kSlots = 192;          // 48 h
    static constexpr uint32_t kSlotSec = 900;   // 15 min
    static constexpr uint32_t kMagic = 0x43524c31; // "CRL1"
    static constexpr size_t kBlobSize = 4 + 4 + 4 + 2 + kSlots * sizeof(RollupPoint);

    Rollup();
    // Add a live sample (any rate). utc must be a valid epoch (ignored otherwise).
    void add(int64_t utc, float ghs, float watts, float chipC, float vrC, uint8_t flags);
    // Number of committed slots and access, oldest first.
    int size() const { return m_count; }
    const RollupPoint &at(int i) const;      // i = 0 oldest
    int64_t slotTime(int i) const;           // UTC start of slot i
    bool dirtySince(int64_t utc) const { return m_lastCommit > utc; }

    // Serialise / restore. restore() returns false on a bad blob (history stays empty).
    size_t save(uint8_t *buf, size_t len) const;
    bool restore(const uint8_t *buf, size_t len);

  private:
    void commit(int64_t slot);
    RollupPoint m_ring[kSlots];
    int m_head = 0;      // next write
    int m_count = 0;
    int64_t m_lastSlot = 0;   // slot index (utc / kSlotSec) of the newest committed point
    int64_t m_lastCommit = 0;
    // accumulator for the slot being filled
    int64_t m_accSlot = 0;
    double m_sGhs = 0, m_sW = 0, m_sChip = 0, m_sVr = 0;
    uint32_t m_n = 0;
    uint8_t m_flags = 0;
};

uint32_t crc32(const uint8_t *p, size_t n);

} // namespace costean
