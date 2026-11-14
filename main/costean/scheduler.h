#pragma once
// Costean OS weekly schedules: time windows that select a tuning profile and/or
// dim or switch off the LCD. Pure logic (no ESP-IDF), unit tested on the host.
#include <stdint.h>
#include "tuning.h"

namespace costean {

// Time zones with their daylight-saving rule, so the schedule follows local time
// without relying on the firmware's global TZ.
enum class DstRule : uint8_t { NONE = 0, NORTH_AMERICA = 1, EUROPE = 2 };

struct Zone {
    const char *id;
    int16_t stdOffsetMin;   // standard time offset from UTC
    DstRule rule;
};

const Zone *zoneById(const char *id);       // nullptr if unknown
const Zone *zoneAt(int i);                  // iterate supported zones, nullptr at the end
const Zone &defaultZone();                  // America/New_York

// Local time for a UTC epoch in a zone (handles DST).
int64_t toLocal(int64_t utc, const Zone &z);
bool isDst(int64_t utc, const Zone &z);
// Civil helpers (proleptic Gregorian, UTC based)
int64_t daysFromCivil(int y, unsigned m, unsigned d);
void civilFromDays(int64_t z, int &y, unsigned &m, unsigned &d);
int weekday(int64_t days);                  // 0 = Sunday

enum class LcdMode : uint8_t { NORMAL = 0, DIM = 1, OFF = 2 };

struct Window {
    bool enabled = false;
    uint8_t days = 0x7f;      // bit0 = Sunday ... bit6 = Saturday (day the window starts)
    uint16_t start = 0;       // minutes after local midnight, 0..1439 (inclusive)
    uint16_t end = 0;         // minutes, exclusive; end <= start means it runs past midnight
    Profile profile = Profile::NONE;   // NONE = leave tuning alone
    LcdMode lcd = LcdMode::NORMAL;
    char name[20] = {0};
};

struct ScheduleResult {
    bool clockValid = false;
    int profileWindow = -1;       // index of the window that chose the profile
    Profile profile = Profile::NONE;
    int lcdWindow = -1;
    LcdMode lcd = LcdMode::NORMAL;
    int64_t nextChangeLocal = 0;  // local epoch seconds of the next boundary (0 = none in 8 days)
};

class Schedule {
  public:
    static constexpr int kMaxWindows = 8;

    bool enabled = false;
    Window windows[kMaxWindows];
    int count = 0;

    // Is window w active at this local minute-of-week (0 = Sunday 00:00)?
    static bool activeAt(const Window &w, int minuteOfWeek);
    // Validate one window (ranges); returns nullptr if fine, else a short error string.
    static const char *validate(const Window &w);

    // Evaluate at a UTC time. Overlapping windows: the lowest-power profile wins
    // (Eco < Stock < Custom < Turbo) and the darkest LCD mode wins.
    ScheduleResult evaluate(int64_t utc, const Zone &z) const;
};

int profileRank(Profile p);   // lower = less power

} // namespace costean
