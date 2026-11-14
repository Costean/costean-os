#include "board602.h"

namespace costean {

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void setupBoard602(int stockMhz, int stockMv, int userMhz, int userMv, Envelope &env, Presets &pre, Limits &lim)
{
    if (stockMhz <= 0) stockMhz = Board602::kStockMhz;
    if (stockMv <= 0) stockMv = Board602::kStockMv;
    env = Envelope();
    env.minMhz = Board602::kListMinMhz;   // throttle floor: lowest official preset
    env.maxMhz = Board602::kListMaxMhz;
    env.minMv = Board602::kListMinMv;
    env.maxMv = Board602::kListMaxMv;
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
    lim.powerW = 25;
    lim.hysteresisC = 4;
}

Limits effectiveLimits602(const Limits &u)
{
    Limits l = u;
    l.chipTempC = clampi(l.chipTempC, Board602::kChipMin, Board602::kChipMax);
    l.vrTempC = clampi(l.vrTempC, Board602::kVrMin, Board602::kVrMax);
    l.powerW = clampi(l.powerW, Board602::kPowerMin, Board602::kPowerMax);
    return l;
}

const char *checkLimits602(int chipC, int vrC, int powerW)
{
    if (chipC < Board602::kChipMin || chipC > Board602::kChipMax) return "chip limit must be 45-73 C";
    if (vrC < Board602::kVrMin || vrC > Board602::kVrMax) return "regulator limit must be 50-103 C";
    if (powerW < Board602::kPowerMin || powerW > Board602::kPowerMax) return "power limit must be 10-35 W";
    return nullptr;
}

} // namespace costean
