// Bitaxe Gamma 601/602 values: profiles, limits and the governor at the Gamma stock operating points.
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
    CHECK_EQ(e.minMhz, 400); CHECK_EQ(e.maxMhz, 1000); CHECK_EQ(e.minMv, 1000); CHECK_EQ(e.maxMv, 1300);
    CHECK_EQ(l.chipTempC, 68); CHECK_EQ(l.vrTempC, 90); CHECK_EQ(l.psuW, 30); CHECK_EQ(l.powerW, 27);
    AutoTune a; CHECK(!a.enabled); CHECK_EQ(a.maxStepsUp, 2);
}

TEST(edited_profiles_replace_the_builtin_points) {
    Envelope e; Presets p; Limits l;
    setupBoard602(525, 1150, 525, 1150, e, p, l);
    ProfileEdit ed[kEditProfiles];
    applyProfileEdits602(p, ed);                       // nothing edited: defaults, Custom 1/2 = stock
    CHECK_EQ(p.eco.mhz, 425); CHECK_EQ(p.turbo.mhz, 575);
    CHECK_EQ(p.user1.mhz, 525); CHECK_EQ(p.user2.mv, 1150);
    ed[0].set = true; ed[0].mhz = 600; ed[0].mv = 1180;       // "Eco is too low"
    ed[2].set = true; ed[2].mhz = 950; ed[2].mv = 1250;       // Turbo at the user's overclock
    ed[3].set = true; ed[3].mhz = 1000; ed[3].mv = 1300; ed[3].enabled = true;
    applyProfileEdits602(p, ed);
    CHECK_EQ(p.eco.mhz, 600); CHECK_EQ(p.eco.mv, 1180);
    CHECK_EQ(p.turbo.mhz, 950); CHECK_EQ(p.turbo.mv, 1250);
    CHECK_EQ(p.user1.mhz, 1000); CHECK_EQ(p.user1.mv, 1300);
    CHECK_EQ(p.of(Profile::USER1).mhz, 1000); CHECK_EQ(p.of(Profile::TURBO).mhz, 950);
    CHECK_EQ(p.stock.mhz, 525);                                // untouched profiles keep their defaults
    // the governor runs an edited Turbo at the user's values when it is cool and inside the limits
    Limits big = l; big.psuW = 60; big.powerW = 54;
    Governor g; g.configure(e, effectiveLimits602(big), AutoTune{});
    Telemetry t = cool602(); t.powerW = 35;
    OpPoint o = g.step(p.turbo, t, 1000); CHECK_EQ(o.mhz, 950); CHECK_EQ(o.mv, 1250);
}

TEST(profile_edit_checks_and_sanitising) {
    CHECK(checkProfilePoint602(400, 1000) == nullptr); CHECK(checkProfilePoint602(1000, 1300) == nullptr);
    CHECK(checkProfilePoint602(399, 1150) != nullptr); CHECK(checkProfilePoint602(1001, 1150) != nullptr);
    CHECK(checkProfilePoint602(525, 999) != nullptr); CHECK(checkProfilePoint602(525, 1301) != nullptr);
    CHECK(checkProfileName("Night") == nullptr); CHECK(checkProfileName("") != nullptr);
    CHECK(checkProfileName("thirteen-char") != nullptr); CHECK(checkProfileName("bad\"name") != nullptr);
    ProfileEdit e; e.set = true; e.mhz = 2000; e.mv = 900; strcpy(e.name, "ok");
    ProfileEdit s = sanitizeProfileEdit602(e);
    CHECK_EQ(s.mhz, 1000); CHECK_EQ(s.mv, 1000); CHECK(!strcmp(s.name, "ok"));
    ProfileEdit ed[kEditProfiles];
    CHECK(profileAvailable(Profile::ECO, ed)); CHECK(!profileAvailable(Profile::USER1, ed));
    ed[3].enabled = true; CHECK(profileAvailable(Profile::USER1, ed)); CHECK(!profileAvailable(Profile::USER2, ed));
    CHECK_EQ(editIndex(Profile::CUSTOM), -1); CHECK(editProfile(4) == Profile::USER2);
    CHECK(!strcmp(defaultLabel(Profile::USER1), "Custom 1")); CHECK(!strcmp(defaultLabel(Profile::CUSTOM), "Manual"));
    // an out-of-range saved point is never used as-is
    Envelope en; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, en, p, l);
    ProfileEdit bad[kEditProfiles]; bad[1].set = true; bad[1].mhz = 1500; bad[1].mv = 1150;
    applyProfileEdits602(p, bad); CHECK_EQ(p.stock.mhz, 525);
}

TEST(manual_is_never_clipped) {
    Envelope e; Presets p; Limits l;
    setupBoard602(525, 1150, 1100, 1350, e, p, l);   // user saved values above the manual range
    CHECK_EQ(e.maxMhz, 1100); CHECK_EQ(e.maxMv, 1350);
    Governor g; g.configure(e, l, AutoTune{});
    OpPoint o = g.step(p.custom, cool602(), 1000);
    CHECK_EQ(o.mhz, 1100); CHECK_EQ(o.mv, 1350);
    setupBoard602(525, 1150, 350, 950, e, p, l);
    g.configure(e, l, AutoTune{});
    o = g.step(p.custom, cool602(), 1000);
    CHECK_EQ(o.mhz, 350); CHECK_EQ(o.mv, 950);
}

TEST(limits_stay_below_stock_cutoffs_and_the_psu) {
    Limits u; u.chipTempC = 80; u.vrTempC = 120; u.powerW = 60; u.psuW = 0;   // no PSU saved: stock 30 W brick
    Limits l = effectiveLimits602(u);
    CHECK_EQ(l.chipTempC, 73); CHECK_EQ(l.vrTempC, 103); CHECK_EQ(l.powerW, 30); CHECK_EQ(l.psuW, 30);
    u.psuW = 50; l = effectiveLimits602(u); CHECK_EQ(l.powerW, 50);
    u.powerW = 45; l = effectiveLimits602(u); CHECK_EQ(l.powerW, 45);
    CHECK(checkLimits602(68, 90, 25, 30) == nullptr);
    CHECK(checkLimits602(68, 90, 45, 50) == nullptr);        // 50 W PSU: 45 W limit is fine (no 35 W cap)
    CHECK(checkLimits602(68, 90, 50, 50) == nullptr);        // up to the full rating
    CHECK(checkLimits602(68, 90, 51, 50) != nullptr);        // never above the PSU
    CHECK(checkLimits602(68, 90, 36, 30) != nullptr);
    CHECK(checkLimits602(74, 90, 25, 30) != nullptr);
    CHECK(checkLimits602(68, 104, 25, 30) != nullptr);
    CHECK(checkLimits602(44, 90, 25, 30) != nullptr);
    CHECK(checkLimits602(68, 90, 25, 9) != nullptr);
    CHECK(checkLimits602(68, 90, 100, 151) != nullptr);
}

TEST(psu_presets_give_90_percent_limits) {
    CHECK_EQ(Board602::defaultPowerFor(25), 22); CHECK_EQ(Board602::defaultPowerFor(30), 27);
    CHECK_EQ(Board602::defaultPowerFor(40), 36); CHECK_EQ(Board602::defaultPowerFor(50), 45);
    CHECK_EQ(Board602::defaultPowerFor(60), 54); CHECK_EQ(Board602::defaultPowerFor(5), 10);
}

TEST(saved_bad_limits_are_pulled_into_range_not_dropped) {
    Limits bad; bad.chipTempC = 99; bad.vrTempC = 10; bad.powerW = 35; bad.psuW = 0;   // e.g. an older build's values
    Limits s = sanitizeLimits602(bad);
    CHECK_EQ(s.chipTempC, 73); CHECK_EQ(s.vrTempC, 50); CHECK_EQ(s.powerW, 30); CHECK_EQ(s.psuW, 30);
    CHECK(checkLimits602(s.chipTempC, s.vrTempC, s.powerW, s.psuW) == nullptr);
}

TEST(high_overclock_950_1250_is_not_clipped_or_throttled_when_inside_limits) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 950, 1250, e, p, l);
    l.psuW = 50; l.powerW = 45; Limits el = effectiveLimits602(l);
    Governor g; g.configure(e, el, AutoTune{});
    Telemetry t = cool602(); t.powerW = 38; t.chipTempC = 62;
    OpPoint o{};
    for (uint64_t ms = 0; ms < 600000; ms += 5000) o = g.step(p.custom, t, ms);
    CHECK_EQ(o.mhz, 950); CHECK_EQ(o.mv, 1250); CHECK(g.state() == GovState::HOLD);
    t.powerW = 46;                                     // over the 45 W limit: steps down, voltage kept
    o = g.step(p.custom, t, 700000); CHECK_EQ(o.mhz, 925); CHECK_EQ(o.mv, 1250);
}

TEST(power_limit_default_still_throttles_on_a_30w_brick) {
    Envelope e; Presets p; Limits l; setupBoard602(525, 1150, 525, 1150, e, p, l);
    Governor g; g.configure(e, effectiveLimits602(l), AutoTune{});
    Telemetry t = cool602(); t.powerW = 28;
    OpPoint o = g.step(p.turbo, t, 1000); CHECK_EQ(o.mhz, 550);
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
    l.powerW = 25;   // a weak 5 V supply, limit set to 25 W
    Governor g; g.configure(e, l, AutoTune{});
    Telemetry t = cool602(); t.powerW = 26;
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
