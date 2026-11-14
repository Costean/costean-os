#include "check.h"
#include <string.h>
#include "../../main/costean/tuning.h"
#include "../../main/costean/rollup.h"
using namespace costean;

static Telemetry cool() { Telemetry t; t.chipTempC = 55; t.vrTempC = 60; t.powerW = 150; return t; }

TEST(defaults_are_safe) {
    AutoTune a; Limits l; Presets p;
    CHECK(!a.enabled);
    CHECK(l.chipTempC < 70);       // below the stock 70 C overheat shutdown
    CHECK(p.stock.mhz == 700 && p.stock.mv == 1150);
    CHECK(p.turbo.mv <= 1200 && p.eco.mhz < p.stock.mhz);
}

TEST(holds_target_when_cool_and_autotune_off) {
    Governor g; g.configure(Envelope{}, Limits{}, AutoTune{});
    OpPoint o{};
    for (uint64_t t = 0; t < 3600000; t += 2000) o = g.step({700, 1150}, cool(), t);
    CHECK_EQ(o.mhz, 700); CHECK_EQ(o.mv, 1150); CHECK(g.state() == GovState::HOLD);
}

TEST(throttles_on_temperature_then_recovers) {
    Governor g; g.configure(Envelope{}, Limits{}, AutoTune{});
    Telemetry hot = cool(); hot.chipTempC = 69;
    OpPoint o = g.step({700, 1150}, hot, 1000);
    CHECK_EQ(o.mhz, 675); CHECK(g.state() == GovState::THROTTLED);
    o = g.step({700, 1150}, hot, 5000); CHECK_EQ(o.mhz, 675);     // rate limited (20 s)
    o = g.step({700, 1150}, hot, 21000); CHECK_EQ(o.mhz, 650);
    CHECK(strcmp(g.reason(), "chip temperature") == 0);
    o = g.step({700, 1150}, cool(), 60000); CHECK_EQ(o.mhz, 650);  // waits 2 min after last step down
    o = g.step({700, 1150}, cool(), 21000 + 120000); CHECK_EQ(o.mhz, 675);
    o = g.step({700, 1150}, cool(), 21000 + 200000); CHECK_EQ(o.mhz, 675);
    o = g.step({700, 1150}, cool(), 21000 + 240000); CHECK_EQ(o.mhz, 700);
    CHECK(g.state() == GovState::HOLD);
}

TEST(hysteresis_blocks_recovery_near_limit) {
    Governor g; g.configure(Envelope{}, Limits{}, AutoTune{});
    Telemetry hot = cool(); hot.chipTempC = 69; g.step({700, 1150}, hot, 0);
    Telemetry warm = cool(); warm.chipTempC = 66;   // limit 68, hysteresis 4 -> needs < 64
    OpPoint o = g.step({700, 1150}, warm, 500000);
    CHECK_EQ(o.mhz, 675);
}

TEST(throttle_on_vr_and_power_never_below_floor) {
    Governor g; Envelope e; e.minMhz = 600; g.configure(e, Limits{}, AutoTune{});
    Telemetry t = cool(); t.powerW = 250;
    OpPoint o{};
    for (int i = 0; i < 50; i++) o = g.step({700, 1150}, t, (uint64_t) i * 30000);
    CHECK_EQ(o.mhz, 600);
    CHECK(strcmp(g.reason(), "power limit") == 0);
    Governor g2; g2.configure(Envelope{}, Limits{}, AutoTune{});
    Telemetry v = cool(); v.vrTempC = 90; g2.step({700, 1150}, v, 1);
    CHECK(strcmp(g2.reason(), "regulator temperature") == 0);
}

TEST(not_mining_no_throttle_change) {
    Governor g; g.configure(Envelope{}, Limits{}, AutoTune{});
    Telemetry t = cool(); t.chipTempC = 80; t.mining = false;
    CHECK_EQ(g.step({700, 1150}, t, 1).mhz, 700);
}

TEST(clamps_to_envelope) {
    Governor g; Envelope e; e.maxMhz = 800; e.maxMv = 1260; g.configure(e, Limits{}, AutoTune{});
    OpPoint o = g.step({900, 1400}, cool(), 1);
    CHECK_EQ(o.mhz, 800); CHECK_EQ(o.mv, 1260);
}

static AutoTune on() { AutoTune a; a.enabled = true; a.holdMs = 60000; return a; }

TEST(autotune_steps_up_carefully_and_caps) {
    Governor g; g.configure(Envelope{}, Limits{}, on());
    OpPoint o = g.step({700, 1150}, cool(), 0);  CHECK_EQ(o.mhz, 700);
    o = g.step({700, 1150}, cool(), 30000);       CHECK_EQ(o.mhz, 700);   // dwell
    o = g.step({700, 1150}, cool(), 60000);       CHECK_EQ(o.mhz, 725);
    o = g.step({700, 1150}, cool(), 120000);      CHECK_EQ(o.mhz, 750);
    o = g.step({700, 1150}, cool(), 180000);      CHECK_EQ(o.mhz, 750);   // max 2 steps
    CHECK_EQ(o.mv, 1150);                                                // voltage never raised
}

TEST(autotune_backs_off_on_errors_and_never_retries) {
    Governor g; g.configure(Envelope{}, Limits{}, on());
    g.step({700, 1150}, cool(), 0); g.step({700, 1150}, cool(), 60000);
    OpPoint o = g.step({700, 1150}, cool(), 120000); CHECK_EQ(o.mhz, 750);
    Telemetry bad = cool(); bad.errorPct = 2.5f;
    o = g.step({700, 1150}, bad, 130000); CHECK_EQ(o.mhz, 725); CHECK(g.state() == GovState::BACKED_OFF);
    for (uint64_t t = 200000; t < 2000000; t += 60000) o = g.step({700, 1150}, cool(), t);
    CHECK_EQ(o.mhz, 725);   // the failed step is not tried again
}

TEST(autotune_needs_headroom) {
    Governor g; g.configure(Envelope{}, Limits{}, on());
    Telemetry warm = cool(); warm.chipTempC = 64;    // limit 68 - headroom 6 = 62
    OpPoint o{};
    for (uint64_t t = 0; t < 600000; t += 60000) o = g.step({700, 1150}, warm, t);
    CHECK_EQ(o.mhz, 700);
}

TEST(autotune_gives_back_first_when_hot) {
    Governor g; g.configure(Envelope{}, Limits{}, on());
    g.step({700, 1150}, cool(), 0); OpPoint o = g.step({700, 1150}, cool(), 60000); CHECK_EQ(o.mhz, 725);
    Telemetry hot = cool(); hot.chipTempC = 70;
    o = g.step({700, 1150}, hot, 61000); CHECK_EQ(o.mhz, 700); CHECK(g.state() == GovState::BACKED_OFF);
    o = g.step({700, 1150}, hot, 90000); CHECK_EQ(o.mhz, 675);
}

TEST(new_profile_restarts_autotune) {
    Governor g; g.configure(Envelope{}, Limits{}, on());
    g.step({700, 1150}, cool(), 0); CHECK_EQ(g.step({700, 1150}, cool(), 60000).mhz, 725);
    CHECK_EQ(g.step({600, 1100}, cool(), 61000).mhz, 600);   // schedule switched to Eco
    CHECK_EQ(g.tuneSteps(), 0);
}

TEST(profile_names_roundtrip) {
    for (Profile p : {Profile::ECO, Profile::STOCK, Profile::TURBO, Profile::CUSTOM}) CHECK(profileFromName(profileName(p)) == p);
    CHECK(profileFromName("warp") == Profile::NONE);
}

TEST(rollup_averages_gaps_and_persists) {
    Rollup r; int64_t t0 = 1763000000 - (1763000000 % 900);
    for (int i = 0; i < 90; i++) r.add(t0 + i * 10, 11000 + (i % 2) * 200, 180, 60, 65, 0);   // slot 0
    r.add(t0 + 900, 12000, 190, 61, 66, 1);                 // starts slot 1 -> commits slot 0
    CHECK_EQ(r.size(), 1); CHECK_EQ(r.at(0).ghs, 11100); CHECK_EQ(r.at(0).watts10, 1800); CHECK_EQ(r.at(0).chip2, 120);
    CHECK_EQ(r.at(0).eff, 16);
    r.add(t0 + 900 * 4, 12000, 190, 61, 66, 0);             // commits slot 1 (miner off for slots 2-3)
    r.add(t0 + 900 * 5, 12000, 190, 61, 66, 0);             // commits slot 4 after a 2-slot gap
    CHECK_EQ(r.size(), 5); CHECK_EQ(r.at(2).ghs, 0xFFFF); CHECK_EQ(r.at(3).ghs, 0xFFFF); CHECK_EQ(r.at(1).flags, 1);
    CHECK_EQ(r.slotTime(0), t0); CHECK_EQ(r.slotTime(4), t0 + 4 * 900);
    uint8_t buf[Rollup::kBlobSize]; CHECK_EQ(r.save(buf, sizeof buf), Rollup::kBlobSize);
    Rollup q; CHECK(q.restore(buf, sizeof buf)); CHECK_EQ(q.size(), 5); CHECK_EQ(q.at(0).ghs, 11100); CHECK_EQ(q.slotTime(4), t0 + 4 * 900);
    buf[40] ^= 0x55; Rollup bad; CHECK(!bad.restore(buf, sizeof buf)); CHECK_EQ(bad.size(), 0);
    CHECK(!bad.restore(buf, 10));
    for (int i = 0; i < 400; i++) q.add(t0 + 900 * (6 + i), 1, 1, 1, 1, 0);
    CHECK_EQ(q.size(), Rollup::kSlots);
    r.add(100, 1, 1, 1, 1, 0); CHECK_EQ(r.size(), 5);       // invalid clock ignored
}

int main() { return run_all("tuning"); }
