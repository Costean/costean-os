#include "check.h"
#include <string.h>
#include "../../main/costean/scheduler.h"
using namespace costean;

static int64_t utcOf(int y, int mo, int d, int h, int mi) { return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60; }

TEST(civil_roundtrip_and_weekday) {
    CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
    CHECK_EQ(weekday(0), 4);                          // Thursday
    CHECK_EQ(weekday(daysFromCivil(2026, 11, 13)), 5);  // Friday
    for (int64_t d = -1000; d < 30000; d += 37) { int y; unsigned m, dd; civilFromDays(d, y, m, dd); CHECK_EQ(daysFromCivil(y, m, dd), d); }
}

TEST(north_america_dst_edges_2026) {
    const Zone &ny = *zoneById("America/New_York");
    // 2026: DST starts Sun Mar 8 07:00 UTC, ends Sun Nov 1 06:00 UTC
    CHECK(!isDst(utcOf(2026, 3, 8, 6, 59), ny)); CHECK(isDst(utcOf(2026, 3, 8, 7, 0), ny));
    CHECK(isDst(utcOf(2026, 11, 1, 5, 59), ny)); CHECK(!isDst(utcOf(2026, 11, 1, 6, 0), ny));
    CHECK_EQ(toLocal(utcOf(2026, 11, 14, 0, 4), ny), utcOf(2026, 11, 13, 19, 4));   // EST = UTC-5
    CHECK_EQ(toLocal(utcOf(2026, 7, 1, 12, 0), ny), utcOf(2026, 7, 1, 8, 0));       // EDT = UTC-4
}

TEST(europe_dst_edges_2026) {
    const Zone &be = *zoneById("Europe/Berlin");
    // 2026: last Sunday March = Mar 29, last Sunday October = Oct 25, both 01:00 UTC
    CHECK(!isDst(utcOf(2026, 3, 29, 0, 59), be)); CHECK(isDst(utcOf(2026, 3, 29, 1, 0), be));
    CHECK(isDst(utcOf(2026, 10, 25, 0, 59), be)); CHECK(!isDst(utcOf(2026, 10, 25, 1, 0), be));
    CHECK(zoneById("Mars/Base") == nullptr);
    CHECK(!isDst(utcOf(2026, 7, 1, 0, 0), *zoneById("America/Phoenix")));
}

static Window win(uint8_t days, int s, int e, Profile p, LcdMode l = LcdMode::NORMAL) {
    Window w; w.enabled = true; w.days = days; w.start = s; w.end = e; w.profile = p; w.lcd = l; return w;
}

TEST(overnight_window_wraps_midnight_and_week) {
    Window w = win(1 << 6, 22 * 60, 7 * 60, Profile::ECO);   // Saturday 22:00 -> Sunday 07:00
    CHECK(Schedule::activeAt(w, 6 * 1440 + 22 * 60));         // Sat 22:00 inclusive
    CHECK(Schedule::activeAt(w, 0 * 1440 + 6 * 60 + 59));     // Sun 06:59 (wrapped past end of week)
    CHECK(!Schedule::activeAt(w, 0 * 1440 + 7 * 60));         // Sun 07:00 exclusive
    CHECK(!Schedule::activeAt(w, 5 * 1440 + 23 * 60));        // Friday night not selected
    Window all = win(0x7f, 0, 0, Profile::ECO);               // start == end: whole day
    for (int m = 0; m < 7 * 1440; m += 97) CHECK(Schedule::activeAt(all, m));
    w.enabled = false; CHECK(!Schedule::activeAt(w, 6 * 1440 + 23 * 60));
}

TEST(quiet_overnight_in_new_york_local_time) {
    Schedule s; s.enabled = true; s.count = 1;
    s.windows[0] = win(0x7f, 22 * 60, 7 * 60, Profile::ECO, LcdMode::DIM);
    const Zone &ny = defaultZone();
    ScheduleResult r = s.evaluate(utcOf(2026, 11, 14, 3, 30), ny);  // 22:30 EST Friday
    CHECK(r.clockValid); CHECK(r.profile == Profile::ECO); CHECK(r.lcd == LcdMode::DIM); CHECK_EQ(r.profileWindow, 0);
    CHECK_EQ(r.nextChangeLocal, utcOf(2026, 11, 14, 7, 0));          // next boundary 07:00 local
    r = s.evaluate(utcOf(2026, 11, 13, 17, 0), ny);                  // 12:00 local
    CHECK(r.profile == Profile::NONE); CHECK(r.lcd == LcdMode::NORMAL);
    CHECK_EQ(r.nextChangeLocal, utcOf(2026, 11, 13, 22, 0));
}

TEST(no_clock_or_disabled_means_no_action) {
    Schedule s; s.enabled = true; s.count = 1; s.windows[0] = win(0x7f, 0, 0, Profile::ECO);
    ScheduleResult r = s.evaluate(0, defaultZone());
    CHECK(!r.clockValid); CHECK(r.profile == Profile::NONE);
    s.enabled = false; r = s.evaluate(utcOf(2026, 1, 1, 0, 0), defaultZone());
    CHECK(r.clockValid); CHECK(r.profile == Profile::NONE);
}

TEST(overlap_lowest_power_and_darkest_screen_win) {
    Schedule s; s.enabled = true; s.count = 3;
    s.windows[0] = win(0x7f, 8 * 60, 20 * 60, Profile::TURBO);
    s.windows[1] = win(0x7f, 16 * 60, 21 * 60, Profile::ECO, LcdMode::DIM);
    s.windows[2] = win(0x7f, 17 * 60, 18 * 60, Profile::NONE, LcdMode::OFF);
    ScheduleResult r = s.evaluate(utcOf(2026, 6, 1, 17, 30), *zoneById("UTC"));
    CHECK(r.profile == Profile::ECO); CHECK_EQ(r.profileWindow, 1); CHECK(r.lcd == LcdMode::OFF); CHECK_EQ(r.lcdWindow, 2);
    r = s.evaluate(utcOf(2026, 6, 1, 9, 0), *zoneById("UTC"));
    CHECK(r.profile == Profile::TURBO);
}

TEST(weekday_only_window) {
    Schedule s; s.enabled = true; s.count = 1;
    s.windows[0] = win(0x3e, 9 * 60, 17 * 60, Profile::ECO);   // Mon-Fri
    const Zone &utc = *zoneById("UTC");
    CHECK(s.evaluate(utcOf(2026, 11, 13, 10, 0), utc).profile == Profile::ECO);    // Friday
    CHECK(s.evaluate(utcOf(2026, 11, 14, 10, 0), utc).profile == Profile::NONE);   // Saturday
}

TEST(validate_rejects_bad_windows) {
    CHECK(Schedule::validate(win(0x7f, 60, 120, Profile::ECO)) == nullptr);
    CHECK(Schedule::validate(win(0, 60, 120, Profile::ECO)) != nullptr);
    CHECK(Schedule::validate(win(0x7f, 1440, 120, Profile::ECO)) != nullptr);
    CHECK(Schedule::validate(win(0x7f, 60, 1500, Profile::ECO)) != nullptr);
    CHECK(Schedule::validate(win(0x7f, 60, 120, Profile::NONE)) != nullptr);
    CHECK(Schedule::validate(win(0x7f, 60, 120, Profile::NONE, LcdMode::OFF)) == nullptr);
}

TEST(dst_change_keeps_local_schedule) {
    // 22:00 local on both sides of the November switch
    Schedule s; s.enabled = true; s.count = 1; s.windows[0] = win(0x7f, 22 * 60, 23 * 60, Profile::ECO);
    const Zone &ny = defaultZone();
    CHECK(s.evaluate(utcOf(2026, 10, 31, 2, 30), ny).profile == Profile::ECO);   // Oct 30 22:30 EDT
    CHECK(s.evaluate(utcOf(2026, 11, 2, 3, 30), ny).profile == Profile::ECO);    // Nov 1 22:30 EST
    CHECK(s.evaluate(utcOf(2026, 11, 2, 2, 30), ny).profile == Profile::NONE);   // Nov 1 21:30 EST
}

int main() { return run_all("scheduler"); }
