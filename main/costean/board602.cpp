#include "board602.h"

#include <stdio.h>
#include <string.h>

namespace costean {

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void setupBoard602(int stockMhz, int stockMv, int userMhz, int userMv, Envelope &env, Presets &pre, Limits &lim)
{
    if (stockMhz <= 0) stockMhz = Board602::kStockMhz;
    if (stockMv <= 0) stockMv = Board602::kStockMv;
    env = Envelope();
    env.minMhz = Board602::kListMinMhz;   // throttle floor: lowest official preset
    env.maxMhz = Board602::kTuneMaxMhz;    // auto-tune ceiling (it stays off by default)
    env.minMv = Board602::kListMinMv;
    env.maxMv = Board602::kTuneMaxMv;
    env.stepMhz = 25;
    if (userMhz > env.maxMhz) env.maxMhz = userMhz;   // Manual is never clipped
    if (userMv > env.maxMv) env.maxMv = userMv;
    if (userMhz > 0 && userMhz < env.minMhz) env.minMhz = userMhz;
    if (userMv > 0 && userMv < env.minMv) env.minMv = userMv;

    pre.stock = {stockMhz, stockMv};
    pre.eco = {clampi(stockMhz - 100, Board602::kListMinMhz, stockMhz), clampi(stockMv - 50, Board602::kListMinMv, stockMv)};
    pre.turbo = {clampi(stockMhz + 50, stockMhz, Board602::kListMaxMhz), clampi(stockMv + 30, stockMv, Board602::kListMaxMv)};
    pre.custom = {userMhz > 0 ? userMhz : stockMhz, userMv > 0 ? userMv : stockMv};

    lim = Limits();
    lim.chipTempC = 68;
    lim.vrTempC = 90;
    lim.psuW = Board602::kPsuDefault;
    lim.powerW = Board602::defaultPowerFor(lim.psuW);   // 27 W on the stock 30 W supply
    lim.hysteresisC = 4;
}

int psu602(const Limits &u)
{
    return u.psuW > 0 ? clampi(u.psuW, Board602::kPsuMin, Board602::kPsuMax) : Board602::kPsuDefault;
}

Limits effectiveLimits602(const Limits &u)
{
    Limits l = u;
    l.psuW = psu602(u);
    l.chipTempC = clampi(l.chipTempC, Board602::kChipMin, Board602::kChipMax);
    l.vrTempC = clampi(l.vrTempC, Board602::kVrMin, Board602::kVrMax);
    l.powerW = clampi(l.powerW, Board602::kPowerMin, l.psuW);
    return l;
}

Limits sanitizeLimits602(const Limits &saved) { return effectiveLimits602(saved); }

const char *checkLimits602(int chipC, int vrC, int powerW, int psuW)
{
    if (chipC < Board602::kChipMin || chipC > Board602::kChipMax) return "chip limit must be 45-73 C";
    if (vrC < Board602::kVrMin || vrC > Board602::kVrMax) return "regulator limit must be 50-103 C";
    if (psuW < Board602::kPsuMin || psuW > Board602::kPsuMax) return "power supply rating must be 10-150 W";
    if (powerW < Board602::kPowerMin || powerW > psuW) return "power limit must be 10 W up to the power supply rating";
    return nullptr;
}

int editIndex(Profile p)
{
    switch (p) {
    case Profile::ECO: return 0;
    case Profile::STOCK: return 1;
    case Profile::TURBO: return 2;
    case Profile::USER1: return 3;
    case Profile::USER2: return 4;
    default: return -1;
    }
}

Profile editProfile(int i)
{
    static const Profile k[kEditProfiles] = {Profile::ECO, Profile::STOCK, Profile::TURBO, Profile::USER1, Profile::USER2};
    return i >= 0 && i < kEditProfiles ? k[i] : Profile::NONE;
}

const char *defaultLabel(Profile p)
{
    switch (p) {
    case Profile::ECO: return "Eco";
    case Profile::STOCK: return "Stock";
    case Profile::TURBO: return "Turbo";
    case Profile::USER1: return "Custom 1";
    case Profile::USER2: return "Custom 2";
    default: return "Manual";
    }
}

void applyProfileEdits602(Presets &pre, const ProfileEdit *e)
{
    pre.user1 = pre.stock;
    pre.user2 = pre.stock;
    if (!e) return;
    OpPoint *slot[kEditProfiles] = {&pre.eco, &pre.stock, &pre.turbo, &pre.user1, &pre.user2};
    // Custom 1 / 2 follow the built-in stock point, not an edited Stock
    OpPoint stock = pre.stock;
    for (int i = 0; i < kEditProfiles; i++) {
        if (i >= 3) *slot[i] = stock;
        if (e[i].set && !checkProfilePoint602(e[i].mhz, e[i].mv)) *slot[i] = {e[i].mhz, e[i].mv};
    }
}

const char *checkProfilePoint602(int mhz, int mv)
{
    if (mhz < Board602::kManualMinMhz || mhz > Board602::kManualMaxMhz) return "profile frequency must be 400-1000 MHz";
    if (mv < Board602::kManualMinMv || mv > Board602::kManualMaxMv) return "profile core voltage must be 1000-1300 mV";
    return nullptr;
}

const char *checkProfileName(const char *n)
{
    if (!n) return "profile name missing";
    size_t len = strlen(n);
    if (len < 1 || len > 12) return "profile name must be 1-12 characters";
    for (const char *c = n; *c; c++)
        if (*c < 0x20 || *c > 0x7e || *c == '"' || *c == '\\') return "profile name: letters, digits and simple punctuation only";
    return nullptr;
}

ProfileEdit sanitizeProfileEdit602(const ProfileEdit &in)
{
    ProfileEdit e = in;
    if (e.set) {
        e.mhz = clampi(e.mhz, Board602::kManualMinMhz, Board602::kManualMaxMhz);
        e.mv = clampi(e.mv, Board602::kManualMinMv, Board602::kManualMaxMv);
    }
    if (e.name[0] && checkProfileName(e.name)) e.name[0] = 0;
    return e;
}

bool profileAvailable(Profile p, const ProfileEdit *e)
{
    if (p == Profile::USER1 || p == Profile::USER2) return e && e[editIndex(p)].enabled;
    return p != Profile::NONE;
}

} // namespace costean
