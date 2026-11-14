#pragma once
// Costean OS 1.2 board envelope for the Bitaxe Gamma 602 (1x BM1370, TPS546 regulator, 5 V input).
// Pure logic so the values are host tested (test/costean/test_board602.cpp).
#include "tuning.h"

namespace costean {

struct Board602 {
    // Official ESP-Miner BM1370 presets for this board.
    static constexpr int kStockMhz = 525, kStockMv = 1150;
    // Official preset lists (device_config.h): 400..690 MHz, 1000..1250 mV.
    static constexpr int kListMinMhz = 400, kListMaxMhz = 690;
    static constexpr int kListMinMv = 1000, kListMaxMv = 1250;
    // ESP-Miner's own overheat cut-off (power_management_task.c). Costean limits stay below them.
    static constexpr int kStockChipCutC = 75;   // THROTTLE_TEMP
    static constexpr int kStockVrCutC = 105;    // TPS546_THROTTLE_TEMP
    // Allowed ranges for the user-editable limits.
    static constexpr int kChipMin = 45, kChipMax = kStockChipCutC - 2;
    static constexpr int kVrMin = 50, kVrMax = kStockVrCutC - 2;
    static constexpr int kPowerMin = 10, kPowerMax = 35;
};

// Envelope, presets and default limits for the 602.
//  stock  = the board defaults reported by the firmware (525 / 1150 on a 602)
//  eco    = 100 MHz and 50 mV under stock (same rule as every Costean OS 1.2 board)
//  turbo  = 50 MHz and 30 mV over stock (same rule), never above the official lists
//  custom = the user's own saved frequency / voltage (Manual)
// The envelope never cuts a user's own saved values: if Manual is above the official
// list, the ceiling follows it so Manual behaves exactly like stock ESP-Miner.
void setupBoard602(int stockMhz, int stockMv, int userMhz, int userMv, Envelope &env, Presets &pre, Limits &lim);

// Limits actually enforced: user limits, capped below the stock cut-offs.
Limits effectiveLimits602(const Limits &user);

// Range checks for PATCH /api/costean (nullptr = ok).
const char *checkLimits602(int chipC, int vrC, int powerW);

} // namespace costean
