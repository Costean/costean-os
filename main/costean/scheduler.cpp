#include "scheduler.h"
#include <string.h>

namespace costean {

static const Zone kZones[] = {
    {"UTC", 0, DstRule::NONE},
    {"America/St_Johns", -210, DstRule::NORTH_AMERICA},
    {"America/Halifax", -240, DstRule::NORTH_AMERICA},
    {"America/New_York", -300, DstRule::NORTH_AMERICA},
    {"America/Toronto", -300, DstRule::NORTH_AMERICA},
    {"America/Chicago", -360, DstRule::NORTH_AMERICA},
    {"America/Denver", -420, DstRule::NORTH_AMERICA},
    {"America/Phoenix", -420, DstRule::NONE},
    {"America/Los_Angeles", -480, DstRule::NORTH_AMERICA},
    {"America/Vancouver", -480, DstRule::NORTH_AMERICA},
    {"Europe/London", 0, DstRule::EUROPE},
    {"Europe/Berlin", 60, DstRule::EUROPE},
    {"Europe/Helsinki", 120, DstRule::EUROPE},
};
static constexpr int kNumZones = sizeof(kZones) / sizeof(kZones[0]);

const Zone *zoneById(const char *id)
{
    if (!id) return nullptr;
    for (int i = 0; i < kNumZones; i++)
        if (!strcmp(kZones[i].id, id)) return &kZones[i];
    return nullptr;
}
const Zone *zoneAt(int i) { return (i >= 0 && i < kNumZones) ? &kZones[i] : nullptr; }
const Zone &defaultZone() { return kZones[3]; }

// days since 1970-01-01 for a civil date (algorithm by era / day-of-era decomposition)
int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t) doe - 719468;
}

void civilFromDays(int64_t z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned) (z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = (int) (yoe + era * 400) + (m <= 2);
}

int weekday(int64_t days) { return (int) (days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6); }

static int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// day number of the n-th (1-based) Sunday of a month, or the last Sunday when n == 0
static int64_t sundayOf(int y, unsigned m, int n)
{
    if (n == 0) {
        unsigned nm = m == 12 ? 1 : m + 1; int ny = m == 12 ? y + 1 : y;
        int64_t last = daysFromCivil(ny, nm, 1) - 1;
        return last - weekday(last);
    }
    int64_t first = daysFromCivil(y, m, 1);
    int64_t firstSun = first + (7 - weekday(first)) % 7;
    return firstSun + 7 * (n - 1);
}

bool isDst(int64_t utc, const Zone &z)
{
    if (z.rule == DstRule::NONE) return false;
    int64_t day = floorDiv(utc + z.stdOffsetMin * 60, 86400);
    int y; unsigned m, d;
    civilFromDays(day, y, m, d);
    int64_t startUtc, endUtc;
    if (z.rule == DstRule::NORTH_AMERICA) {
        // 02:00 local standard time on the 2nd Sunday of March to 02:00 local daylight time on the 1st Sunday of November
        startUtc = sundayOf(y, 3, 2) * 86400 + 2 * 3600 - z.stdOffsetMin * 60;
        endUtc = sundayOf(y, 11, 1) * 86400 + 2 * 3600 - (z.stdOffsetMin + 60) * 60;
    } else {
        // 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of October
        startUtc = sundayOf(y, 3, 0) * 86400 + 3600;
        endUtc = sundayOf(y, 10, 0) * 86400 + 3600;
    }
    return utc >= startUtc && utc < endUtc;
}

int64_t toLocal(int64_t utc, const Zone &z)
{
    return utc + (int64_t) (z.stdOffsetMin + (isDst(utc, z) ? 60 : 0)) * 60;
}

bool Schedule::activeAt(const Window &w, int mow)
{
    if (!w.enabled || w.start > 1439 || w.end > 1440) return false;
    const int week = 7 * 1440;
    int len = (int) w.end - (int) w.start;
    if (len <= 0) len += 1440;   // past midnight (equal start and end = 24 h)
    for (int day = 0; day < 7; day++) {
        if (!(w.days & (1 << day))) continue;
        int s = day * 1440 + w.start;
        int off = ((mow - s) % week + week) % week;
        if (off < len) return true;
    }
    return false;
}

const char *Schedule::validate(const Window &w)
{
    if (w.start > 1439) return "start must be 00:00-23:59";
    if (w.end > 1439) return "end must be 00:00-23:59";
    if ((w.days & 0x7f) == 0) return "pick at least one day";
    if ((uint8_t) w.profile > (uint8_t) Profile::CUSTOM) return "unknown profile";
    if ((uint8_t) w.lcd > (uint8_t) LcdMode::OFF) return "unknown screen mode";
    if (w.profile == Profile::NONE && w.lcd == LcdMode::NORMAL) return "window does nothing";
    return nullptr;
}

int profileRank(Profile p)
{
    switch (p) {
    case Profile::ECO: return 1;
    case Profile::STOCK: return 2;
    case Profile::CUSTOM: return 3;
    case Profile::TURBO: return 4;
    default: return 99;
    }
}

ScheduleResult Schedule::evaluate(int64_t utc, const Zone &z) const
{
    ScheduleResult r;
    if (utc < 1700000000) return r;   // no valid network time yet: never act
    r.clockValid = true;
    if (!enabled) return r;
    int64_t local = toLocal(utc, z);
    int64_t day = floorDiv(local, 86400);
    int mow = weekday(day) * 1440 + (int) ((local - day * 86400) / 60);

    auto at = [&](int m, int &pw, Profile &p, int &lw, LcdMode &l) {
        pw = -1; p = Profile::NONE; lw = -1; l = LcdMode::NORMAL;
        for (int i = 0; i < count && i < kMaxWindows; i++) {
            const Window &w = windows[i];
            if (!activeAt(w, m)) continue;
            if (w.profile != Profile::NONE && profileRank(w.profile) < profileRank(p)) { p = w.profile; pw = i; }
            if ((uint8_t) w.lcd > (uint8_t) l) { l = w.lcd; lw = i; }
        }
    };
    at(mow, r.profileWindow, r.profile, r.lcdWindow, r.lcd);

    // next boundary: scan forward minute by minute (bounded: 8 days)
    for (int dm = 1; dm <= 8 * 1440; dm++) {
        int pw, lw; Profile p; LcdMode l;
        at((mow + dm) % (7 * 1440), pw, p, lw, l);
        if (p != r.profile || l != r.lcd) {
            r.nextChangeLocal = (local / 60 + dm) * 60;
            break;
        }
    }
    return r;
}

} // namespace costean
