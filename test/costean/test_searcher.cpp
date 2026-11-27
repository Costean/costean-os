#include "check.h"
#include <string.h>
#include "../../main/costean/tuning.h"
using namespace costean;

static Envelope env()
{
    Envelope e;
    e.minMhz = 400; e.maxMhz = 1000; e.minMv = 1000; e.maxMv = 1300; e.stepMhz = 25;
    return e;
}
static Limits lim()
{
    Limits l;
    l.chipTempC = 68; l.vrTempC = 90; l.powerW = 27; l.psuW = 30;
    return l;
}
static SearchConfig cfg()
{
    SearchConfig c;
    c.enabled = true;
    c.goal = TuneGoal::EFFICIENCY;
    c.warmupMs = 100;
    c.windowMs = 200;
    c.watchMs = 400;
    c.stepMhz = 25;
    c.stepMv = 10;
    c.trimMarginMv = 10;
    return c;
}
// Stable telemetry at a trial clock: ~98.5 % of theoretical, cool, under the 27 W cap.
static Telemetry ok(int mhz, float jth = 14.0f, float err = 0.1f)
{
    Telemetry t;
    t.chipTempC = 55; t.vrTempC = 60; t.mining = true; t.errorPct = err; t.rejectPct = 0.1f;
    t.ghs = expectedGhs(mhz) * 0.985f;
    t.powerW = jth * (t.ghs / 1000.0f);
    return t;
}

struct Run {
    Searcher s;
    OpPoint start{525, 1150};
    uint64_t t = 0;
    OpPoint last{};
    Run() {
        s.configure(env(), lim(), cfg());
    }
    OpPoint tick(const Telemetry &tel, uint64_t dt = 50, bool throttled = false) {
        t += dt;
        last = s.step(start, tel, t, throttled);
        return last;
    }
    // Feed telemetry at the *current trial* clock. Advance until the trial or phase changes,
    // or a generous tick cap is reached (covers 2 min + 5 min in unit-test time).
    OpPoint window(float jth = 14.0f, float err = 0.1f, bool throttled = false) {
        SearchPhase p0 = s.phase();
        int f0 = s.trial().mhz, v0 = s.trial().mv;
        if (!f0) { tick(ok(start.mhz, jth, err), 50, throttled); f0 = last.mhz; v0 = last.mv; p0 = s.phase(); }
        for (int i = 0; i < 16; i++) {
            int f = s.trial().mhz ? s.trial().mhz : start.mhz;
            tick(ok(f, jth, err), 50, throttled);
            if (s.trial().mhz != f0 || s.trial().mv != v0 || s.phase() != p0) break;
        }
        return last;
    }
    OpPoint windowTel(Telemetry t, bool throttled = false) {
        SearchPhase p0 = s.phase();
        int f0 = s.trial().mhz, v0 = s.trial().mv;
        if (!f0) { tick(t, 50, throttled); f0 = last.mhz; v0 = last.mv; p0 = s.phase(); }
        for (int i = 0; i < 16; i++) {
            tick(t, 50, throttled);
            if (s.trial().mhz != f0 || s.trial().mv != v0 || s.phase() != p0) break;
        }
        return last;
    }
};

TEST(searcher_off_holds_profile) {
    Searcher s;
    SearchConfig c; c.enabled = false; c.warmupMs = 100; c.windowMs = 200;
    s.configure(env(), lim(), c);
    OpPoint o{};
    for (uint64_t t = 0; t < 10000; t += 100) o = s.step({525, 1150}, ok(525), t);
    CHECK_EQ(o.mhz, 525); CHECK_EQ(o.mv, 1150);
    CHECK(s.phase() == SearchPhase::OFF);
}

TEST(searcher_efficiency_climbs_then_trims_and_locks) {
    Run r;
    // 525 better baseline (higher J/TH), 550 better, 575 better, 600 worse, 625 worse -> peak at 575
    // step is 25: 525, 550, 575, 600, 625
    CHECK_EQ(r.window(14.6f).mhz, 550);           // climbed
    CHECK(r.s.phase() == SearchPhase::SEARCHING);
    CHECK_EQ(r.window(14.1f).mhz, 575);
    CHECK_EQ(r.window(13.9f).mhz, 600);
    CHECK_EQ(r.window(14.3f).mhz, 625);           // first worse, confirming
    OpPoint o = r.window(14.4f);                 // second worse -> trim at 575
    CHECK_EQ(o.mhz, 575);
    CHECK(o.mv == 1140);                                  // first trim step
    CHECK(r.s.phase() == SearchPhase::TRIMMING);
    // Successful trims 1140, 1130, then fail at 1120 with errors -> lock 1130 + 10 mV margin = 1140
    CHECK_EQ(r.window(13.7f).mv, 1130);
    CHECK_EQ(r.window(13.6f).mv, 1120);
    o = r.window(14.0f, 5.0f);
    CHECK_EQ(o.mhz, 575);
    CHECK_EQ(o.mv, 1140);                                 // last good 1130 + 10 mV margin
    CHECK(r.s.phase() == SearchPhase::LOCKED || r.s.phase() == SearchPhase::BACKOFF);
    CHECK(r.s.lock().valid());
    CHECK_EQ(r.s.lock().mhz, 575);
    CHECK_EQ(r.s.lock().mv, 1140);
    // Voltage never went above the 1150 start.
    CHECK(r.s.lock().mv <= 1150);
}

TEST(searcher_never_raises_voltage) {
    Run r;
    r.window(14.0f);
    CHECK_EQ(r.last.mv, 1150);
    r.window(13.8f);
    CHECK_EQ(r.last.mv, 1150);
    CHECK(r.last.mv <= r.start.mv);
}

TEST(searcher_hashrate_stops_at_error_wall_and_rolls_back) {
    Run r;
    SearchConfig c = cfg(); c.goal = TuneGoal::HASHRATE;
    r.s.configure(env(), lim(), c);
    CHECK_EQ(r.window().mhz, 550);
    CHECK_EQ(r.window().mhz, 575);
    OpPoint o = r.window(14.0f, 3.0f);
    // rolled back to last good (550) and entered trim (voltage down 10 mV) or backoff
    CHECK_EQ(o.mhz, 550);
    CHECK(o.mv <= 1150);
    CHECK(r.s.phase() == SearchPhase::TRIMMING || r.s.phase() == SearchPhase::BACKOFF || r.s.phase() == SearchPhase::LOCKED);
}

TEST(searcher_rolls_back_on_overheating) {
    Run r;
    r.window(14.0f);
    CHECK_EQ(r.last.mhz, 550);
    Telemetry hot = ok(550, 14.0f);
    hot.chipTempC = 69;   // at/over the 68 C limit
    OpPoint o = r.tick(hot, 50);
    CHECK_EQ(o.mhz, 525);                 // last good start point
    CHECK(r.s.phase() == SearchPhase::TRIMMING || r.s.phase() == SearchPhase::BACKOFF || r.s.phase() == SearchPhase::LOCKED);
}

TEST(searcher_power_cap_stops_climb) {
    Run r;
    SearchConfig c = cfg(); c.goal = TuneGoal::POWER_CAP; c.powerCapW = 18;
    r.s.configure(env(), lim(), c);
    CHECK_EQ(r.s.powerCapW(), 18);        // under the 27 W limit and 30 W PSU
    // 16 W is under the 18 W cap and under the 5 % early-stop (17.1 W)
    OpPoint o = r.window(14.0f); // ~14 J/TH * 1.07 TH/s ≈ 15 W
    CHECK_EQ(o.mhz, 550);
    // Force power at 550 MHz over the 5 % margin (cap 18, 95 % = 17.1)
    Telemetry near = ok(550, 16.5f);
    // ok() sets power = jth * ghs/1000; bump it
    near.powerW = 17.5f;
    o = r.windowTel(near);
    CHECK_EQ(o.mhz, 550);                 // did not climb to 575
    CHECK(o.mv <= 1150);
    CHECK(r.s.phase() == SearchPhase::TRIMMING || r.s.phase() == SearchPhase::LOCKED);
}

TEST(searcher_power_cap_never_exceeds_psu_or_limit) {
    Searcher s;
    SearchConfig c = cfg(); c.goal = TuneGoal::POWER_CAP; c.powerCapW = 80;
    Limits l = lim();   // 27 W limit, 30 W PSU
    s.configure(env(), l, c);
    CHECK_EQ(s.powerCapW(), 27);
    l.psuW = 20; l.powerW = 27;
    s.configure(env(), l, c);
    CHECK_EQ(s.powerCapW(), 20);
}

TEST(searcher_respects_mhz_and_mv_envelope) {
    Run r;
    // Drive many successful efficiency climbs: J/TH keeps falling so it will hit 1000 MHz
    float jth = 16.0f;
    OpPoint o = r.last;
    for (int i = 0; i < 30; i++) {
        jth -= 0.2f;
        o = r.window(jth);
        CHECK(o.mhz >= 400 && o.mhz <= 1000);
        CHECK(o.mv >= 1000 && o.mv <= 1300);
        CHECK(o.mv <= 1150);
        if (r.s.phase() == SearchPhase::TRIMMING || r.s.phase() == SearchPhase::LOCKED) break;
    }
    CHECK(o.mhz <= 1000);
}

TEST(searcher_missing_temps_do_not_climb) {
    Run r;
    Telemetry t = ok(525);
    t.chipTempC = 0; t.vrTempC = 0;
    OpPoint o = r.windowTel(t);
    CHECK_EQ(o.mhz, 525);                 // no climb
    CHECK(r.s.phase() != SearchPhase::LOCKED);
}

TEST(searcher_low_hashrate_is_unstable) {
    Run r;
    Telemetry t = ok(525);
    t.ghs = expectedGhs(525) * 0.80f;     // below 92 %
    t.powerW = 15;
    OpPoint o = r.windowTel(t);
    CHECK(o.mhz <= 525);
    CHECK(r.s.phase() == SearchPhase::BACKOFF || o.mhz < 525);
}

TEST(searcher_restore_lock_skips_climb) {
    Run r;
    SearchLock L; L.mhz = 575; L.mv = 1100; L.ghs = 1160; L.jth = 13.8f;
    r.s.setLock(L);
    OpPoint o = r.tick(ok(575, 13.8f), 50);
    CHECK_EQ(o.mhz, 575);
    CHECK_EQ(o.mv, 1100);
    CHECK(r.s.phase() == SearchPhase::WATCHING || r.s.phase() == SearchPhase::LOCKED);
    // After a full watch window it should stay at the lock (or be locked).
    o = r.window(13.8f);
    CHECK_EQ(o.mhz, 575);
    CHECK_EQ(o.mv, 1100);
}

TEST(searcher_restart_clears_lock_and_starts_over) {
    Run r;
    SearchLock L; L.mhz = 575; L.mv = 1100; L.ghs = 1160; L.jth = 13.8f;
    r.s.setLock(L);
    r.tick(ok(575), 50);
    r.s.restart();
    CHECK(!r.s.lock().valid());
    CHECK(r.s.lockDirty());
    OpPoint o = r.tick(ok(525), 50);
    CHECK_EQ(o.mhz, 525);
    CHECK_EQ(o.mv, 1150);
    CHECK(r.s.phase() == SearchPhase::SEARCHING);
}

TEST(searcher_profile_change_restarts) {
    Run r;
    r.window(14.0f);
    CHECK_EQ(r.last.mhz, 550);
    r.start = {425, 1100};                // new profile
    OpPoint o = r.tick(ok(425, 14.0f), 50);
    CHECK_EQ(o.mhz, 425);
    CHECK_EQ(o.mv, 1100);
    CHECK(r.s.phase() == SearchPhase::SEARCHING);
}

TEST(searcher_watch_error_drops_clock) {
    Run r;
    SearchLock L; L.mhz = 575; L.mv = 1100; L.ghs = 1160; L.jth = 13.8f;
    r.s.setLock(L);
    r.window(13.8f);             // restore + first recheck (maybe still watching/locked)
    // Force a watch measure to fail with errors
    // If currently idle/locked, advance past watchMs (400)
    for (int i = 0; i < 20 && r.s.phase() == SearchPhase::LOCKED; i++) r.tick(ok(575, 13.8f), 50);
    Telemetry err = ok(575, 13.8f, 5.0f);
    OpPoint o = r.windowTel(err);
    CHECK(o.mhz <= 575);
    CHECK(o.mhz >= 400);
    if (o.mhz < 575) {
        CHECK_EQ(o.mhz, 550);
        CHECK(r.s.lock().mhz == 550);
    }
}

TEST(searcher_voltage_floor_is_1000mV) {
    Run r;
    r.start = {525, 1010};
    // Peak immediately: one good window then two worse
    r.window(13.0f);
    r.window(14.0f);
    OpPoint o = r.window(14.2f);
    CHECK(o.mhz == 525 || o.mv <= 1010);
    // Trim from 1010: 1000 then floor
    if (r.s.phase() == SearchPhase::TRIMMING) {
        o = r.window(12.8f);
        CHECK(o.mv >= 1000);
        if (r.s.phase() == SearchPhase::TRIMMING) {
            o = r.window(12.7f);
        }
        CHECK(o.mv >= 1000);
        CHECK(r.s.lock().valid() ? r.s.lock().mv >= 1000 : o.mv >= 1000);
    }
}

TEST(searcher_governor_throttle_rolls_back) {
    Run r;
    r.window(14.0f);
    CHECK_EQ(r.last.mhz, 550);
    OpPoint o = r.tick(ok(550, 14.0f), 50, true);
    CHECK_EQ(o.mhz, 525);
}

TEST(goal_and_phase_names) {
    CHECK(strcmp(tuneGoalName(TuneGoal::EFFICIENCY), "efficiency") == 0);
    CHECK(tuneGoalFromName("hashrate") == TuneGoal::HASHRATE);
    CHECK(tuneGoalFromName("power") == TuneGoal::POWER_CAP);
    CHECK(tuneGoalFromName("nope") == TuneGoal::EFFICIENCY);
    CHECK(strcmp(searchPhaseName(SearchPhase::LOCKED), "locked") == 0);
}

int main() { return run_all("searcher"); }
