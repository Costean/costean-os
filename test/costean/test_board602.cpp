// Bitaxe Gamma 602 values: profiles, limits and the governor at the 602's own operating points.
#include "check.h"
#include <string.h>
#include "../../main/costean/board602.h"
using namespace costean;

static Telemetry cool602() { Telemetry t; t.chipTempC = 58; t.vrTempC = 55; t.powerW = 16; return t; }

TEST(stock_eco_turbo_values) {
    Envelope e; Presets p; Limits l;
    setupBoard602(525, 1150, 525, 1150, e, p, l);
    CHECK_EQ(p.stock.mhz, 525); CHECK_EQ(p.stock.mv, 1150);
    CHECK_EQ(p.eco.mhz, 425);   CHECK_EQ(p.eco.mv, 1100);
    CHECK_EQ(p.turbo.mhz, 575); CHECK_EQ(p.turbo.mv, 1180);
    CHECK_EQ(p.custom.mhz, 525); CHECK_EQ(p.custom.mv, 1150);
    CHECK_EQ(e.minMhz, 400); CHECK_EQ(e.maxMhz, 690); CHECK_EQ(e.minMv, 1000); CHECK_EQ(e.maxMv, 1250);
    CHECK_EQ(l.chipTempC, 68); CHECK_EQ(l.vrTempC, 90); CHECK_EQ(l.powerW, 25);
    AutoTune a; CHECK(!a.enabled); CHECK_EQ(a.maxStepsUp, 2);
}

TEST(manual_is_never_clipped) {
    Envelope e; Presets p; Limits l;
    setupBoard602(525, 1150, 750, 1300, e, p, l);   // user already overclocked with ?oc
    CHECK_EQ(e.maxMhz, 750); CHECK_EQ(e.maxMv, 1300);
    Governor g; g.configure(e, l, AutoTune{});
    OpPoint o = g.step(p.custom, cool602(), 1000);
    CHECK_EQ(o.mhz, 750); CHECK_EQ(o.mv, 1300);
    setupBoard602(525, 1150, 350, 950, e, p, l);
    g.configure(e, l, AutoTune{});
    o = g.step(p.custom, cool602(), 1000);
    CHECK_EQ(o.mhz, 350); CHECK_EQ(o.mv, 950);
}

TEST(limits_stay_below_stock_cutoffs) {
    Limits u; u.chipTempC = 80; u.vrTempC = 120; u.powerW = 60;
    Limits l = effectiveLimits602(u);
    CHECK_EQ(l.chipTempC, 73); CHECK_EQ(l.vrTempC, 103); CHECK_EQ(l.powerW, 35);
    CHECK(checkLimits602(68, 90, 25) == nullptr);
    CHECK(checkLimits602(74, 90, 25) != nullptr);
    CHECK(checkLimits602(68, 104, 25) != nullptr);
    CHECK(checkLimits602(68, 90, 36) != nullptr);
    CHECK(checkLimits602(44, 90, 25) != nullptr);
}

TEST(stock_holds_525_when_cool) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    Governor g; g.configure(e, l, AutoTune{});
    OpPoint o{};
    for (uint64_t t = 0; t < 7200000; t += 5000) o = g.step(p.stock, cool602(), t);
    CHECK_EQ(o.mhz, 525); CHECK_EQ(o.mv, 1150); CHECK(g.state() == GovState::HOLD);
}

TEST(power_limit_throttles_turbo_down_to_floor_only) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    Governor g; g.configure(e, l, AutoTune{});
    Telemetry t = cool602(); t.powerW = 26;   // a weak 5 V supply
    OpPoint o{};
    for (int i = 0; i < 40; i++) o = g.step(p.turbo, t, (uint64_t) i * 21000);
    CHECK_EQ(o.mhz, 400);                     // 575 -> 400 in 25 MHz steps, never lower
    CHECK_EQ(o.mv, 1180);                     // the governor never changes voltage
    CHECK(strcmp(g.reason(), "power limit") == 0);
}

TEST(chip_limit_throttle_and_recover_at_602_points) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    Governor g; g.configure(e, effectiveLimits602(l), AutoTune{});
    Telemetry hot = cool602(); hot.chipTempC = 68.5f;
    OpPoint o = g.step(p.stock, hot, 1000); CHECK_EQ(o.mhz, 500);
    o = g.step(p.stock, hot, 22000); CHECK_EQ(o.mhz, 475);
    Telemetry warm = cool602(); warm.chipTempC = 66;   // inside the 4 C hysteresis: stay down
    o = g.step(p.stock, warm, 22000 + 300000); CHECK_EQ(o.mhz, 475);
    o = g.step(p.stock, cool602(), 22000 + 420000); CHECK_EQ(o.mhz, 500);
    o = g.step(p.stock, cool602(), 22000 + 540000); CHECK_EQ(o.mhz, 525);
    CHECK(g.state() == GovState::HOLD);
}

TEST(autotune_from_stock_is_capped_and_frequency_only) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    AutoTune a; a.enabled = true; a.holdMs = 60000;
    Governor g; g.configure(e, l, a);
    OpPoint o{};
    for (uint64_t t = 0; t <= 600000; t += 5000) o = g.step(p.stock, cool602(), t);
    CHECK_EQ(o.mhz, 575); CHECK_EQ(o.mv, 1150);   // +2 steps max by default, voltage untouched
    Telemetry err = cool602(); err.errorPct = 2.5f;
    o = g.step(p.stock, err, 700000); CHECK_EQ(o.mhz, 550); CHECK(g.state() == GovState::BACKED_OFF);
    for (uint64_t t = 705000; t <= 2000000; t += 5000) o = g.step(p.stock, cool602(), t);
    CHECK_EQ(o.mhz, 550);                          // never retries the failed step
}

TEST(autotune_needs_headroom) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    AutoTune a; a.enabled = true; a.holdMs = 60000;
    Governor g; g.configure(e, l, a);
    Telemetry t = cool602(); t.chipTempC = 63;    // only 5 C under the 68 C limit
    OpPoint o{};
    for (uint64_t ms = 0; ms <= 900000; ms += 5000) o = g.step(p.stock, t, ms);
    CHECK_EQ(o.mhz, 525);
}

int main() { return run_all("board602"); }
