#pragma once
// Costean OS tuning profiles and safety governor (pure logic, host tested).
#include <stdint.h>

namespace costean {

enum class Profile : uint8_t { NONE = 0, ECO = 1, STOCK = 2, TURBO = 3, CUSTOM = 4 };

const char *profileName(Profile p);
Profile profileFromName(const char *s);   // "eco"/"stock"/"turbo"/"custom", NONE otherwise

struct OpPoint {
    int mhz = 0;
    int mv = 0;
};

// Board envelope the governor never leaves.
struct Envelope {
    int minMhz = 400;      // throttle floor
    int maxMhz = 800;      // highest frequency in the board's list
    int minMv = 1100;
    int maxMv = 1260;
    int stepMhz = 25;      // board list spacing
};

struct Presets {
    OpPoint eco{600, 1100};
    OpPoint stock{700, 1150};
    OpPoint turbo{750, 1180};
    OpPoint custom{700, 1150};  // the user's own saved MHz / mV
    OpPoint of(Profile p) const;
};

// Conservative defaults. All limits throttle the clock before the stock firmware's
// overheat shutdown (which stays in place as the last line of defence).
struct Limits {
    int chipTempC = 68;   // hottest ASIC
    int vrTempC = 85;     // voltage regulator
    int powerW = 200;     // input power
    int hysteresisC = 4;  // must cool this far below a limit before stepping back up
};

struct AutoTune {
    bool enabled = false;          // OFF by default
    int maxStepsUp = 2;            // at most +2 steps (+50 MHz) above the profile
    float maxErrorPct = 1.0f;      // ASIC hardware error rate ceiling
    float maxRejectPct = 1.0f;     // pool reject rate ceiling (over the last interval)
    uint32_t holdMs = 15 * 60000;  // dwell at each step before trying the next
    int headroomC = 6;             // only step up with this much temperature headroom
};

struct Telemetry {
    float chipTempC = 0;   // 0 = unknown
    float vrTempC = 0;
    float powerW = 0;
    float errorPct = 0;    // ASIC hardware error %
    float rejectPct = 0;   // recent pool reject %
    bool mining = true;    // false while shut down / faulted / not initialised
};

enum class GovState : uint8_t { HOLD = 0, THROTTLED = 1, TUNING_UP = 2, BACKED_OFF = 3 };
const char *govStateName(GovState s);

// Combines the selected profile, the optional auto-tune offset and the throttle cap
// into the operating point the hardware should run at.
class Governor {
  public:
    static constexpr uint32_t kThrottleEveryMs = 20000;   // step down at most every 20 s
    static constexpr uint32_t kRecoverEveryMs = 120000;   // step back up at most every 2 min

    void configure(const Envelope &e, const Limits &l, const AutoTune &a);
    // target = profile point (already resolved from presets / schedule)
    OpPoint step(const OpPoint &target, const Telemetry &t, uint64_t nowMs);

    GovState state() const { return m_state; }
    int throttleSteps() const { return m_throttle; }
    int tuneSteps() const { return m_tune; }
    int bestStableSteps() const { return m_bestStable; }
    const char *reason() const { return m_reason; }
    void resetTune();

    const Envelope &envelope() const { return m_env; }
    const Limits &limits() const { return m_lim; }
    const AutoTune &autotune() const { return m_at; }

    OpPoint clamp(const OpPoint &p) const;

  private:
    Envelope m_env;
    Limits m_lim;
    AutoTune m_at;
    GovState m_state = GovState::HOLD;
    int m_throttle = 0;       // steps below target
    int m_tune = 0;           // steps above target (auto-tune)
    int m_bestStable = 0;
    int m_failCeil = 1000;    // auto-tune never returns to a step that failed
    OpPoint m_lastTarget{};
    uint64_t m_lastDown = 0, m_lastUp = 0, m_lastTune = 0;
    bool m_tuneStarted = false;
    const char *m_reason = "";
};

} // namespace costean
