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
    // Manual entry range (Mining & Tuning) and the ceiling auto-tune may climb to.
    // The TPS546 on the 602 is configured for 1.0-2.0 V, so 1300 mV is inside its range.
    static constexpr int kManualMinMhz = 400, kManualMaxMhz = 1000;
    static constexpr int kManualMinMv = 1000, kManualMaxMv = 1300;
    static constexpr int kTuneMaxMhz = kManualMaxMhz, kTuneMaxMv = kManualMaxMv;
    // Allowed ranges for the user-editable limits.
    static constexpr int kChipMin = 45, kChipMax = kStockChipCutC - 2;
    static constexpr int kVrMin = 50, kVrMax = kStockVrCutC - 2;
    // Power supply rating the user picks (Profiles & Limits). The power limit goes up to it.
    static constexpr int kPsuMin = 10, kPsuMax = 150, kPsuDefault = 30;   // 30 W = the stock 5 V / 6 A brick
    static constexpr int kPowerMin = 10;
    // Default power limit for a supply: 90 % of its rating, rounded down.
    static constexpr int defaultPowerFor(int psuW) { return psuW * 9 / 10 < kPowerMin ? kPowerMin : psuW * 9 / 10; }
};

// Envelope, presets and default limits for the 602.
//  stock  = the board defaults reported by the firmware (525 / 1150 on a 602)
//  eco    = 100 MHz and 50 mV under stock (same rule as every Costean OS 1.2 board)
//  turbo  = 50 MHz and 30 mV over stock (same rule), never above the official lists
//  custom = the user's own saved frequency / voltage (Manual)
// The envelope never cuts a user's own saved values: if Manual is above the official
// list, the ceiling follows it so Manual behaves exactly like stock ESP-Miner.
void setupBoard602(int stockMhz, int stockMv, int userMhz, int userMv, Envelope &env, Presets &pre, Limits &lim);

// Limits actually enforced: user limits, capped below the stock cut-offs and at the PSU rating.
Limits effectiveLimits602(const Limits &user);

// PSU rating in use (the default when none is saved).
int psu602(const Limits &user);

// Range checks for PATCH /api/costean (nullptr = ok). psuW is the supply rating (power <= psuW).
const char *checkLimits602(int chipC, int vrC, int powerW, int psuW);

// Bring saved limits back into range instead of throwing the whole saved config away.
Limits sanitizeLimits602(const Limits &saved);

// ---- user-editable profiles ----
// Eco, Stock, Turbo and two extra profiles ("Custom 1", "Custom 2") can each be given the
// user's own frequency, voltage and name. Manual stays the saved Mining & Tuning values.
struct ProfileEdit {
    bool set = false;       // false = use the built-in default point
    int mhz = 0, mv = 0;
    char name[13] = "";     // "" = the default label
    bool enabled = false;   // only used by Custom 1 / 2 (Eco/Stock/Turbo are always available)
};
constexpr int kEditProfiles = 5;   // eco, stock, turbo, user1, user2
int editIndex(Profile p);          // 0..4, or -1 for Manual / none
Profile editProfile(int idx);
const char *defaultLabel(Profile p);   // "Eco" "Stock" "Turbo" "Manual" "Custom 1" "Custom 2"
// Override the built-in presets with the user's values. Custom 1 / 2 start at the stock point.
void applyProfileEdits602(Presets &pre, const ProfileEdit *edits);
// Range checks (nullptr = ok): 400-1000 MHz, 1000-1300 mV, names 1-12 printable characters.
const char *checkProfilePoint602(int mhz, int mv);
const char *checkProfileName(const char *name);
// Saved edits pulled back into range (bad names dropped) instead of being discarded.
ProfileEdit sanitizeProfileEdit602(const ProfileEdit &e);
bool profileAvailable(Profile p, const ProfileEdit *edits);   // Custom 1 / 2 only when turned on

} // namespace costean
