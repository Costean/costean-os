#pragma once
// Costean OS tuning profiles and safety governor (pure logic, host tested).
#include <stdint.h>

namespace costean {

// CUSTOM is "Manual" (the saved Mining & Tuning values); USER1 / USER2 are the two extra,
// user-named profiles (off until the user turns them on).
enum class Profile : uint8_t { NONE = 0, ECO = 1, STOCK = 2, TURBO = 3, CUSTOM = 4, USER1 = 5, USER2 = 6 };

const char *profileName(Profile p);
Profile profileFromName(const char *s);   // "eco"/"stock"/"turbo"/"custom"/"user1"/"user2", NONE otherwise

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
    OpPoint user1{700, 1150};
    OpPoint user2{700, 1150};
    OpPoint of(Profile p) const;
};

// Conservative defaults. All limits throttle the clock before the stock firmware's
// overheat shutdown (which stays in place as the last line of defence).
struct Limits {
    int chipTempC = 68;   // hottest ASIC
    int vrTempC = 85;     // voltage regulator
    int powerW = 200;     // input power
    int psuW = 0;         // power supply rating in W (0 = not set); the power limit can't exceed it
    int hysteresisC = 4;  // must cool this far below a limit before stepping back up
};

// BM1370 (Gamma 601/602): 2040 small cores -> expected GH/s = MHz * 2.04.
inline float expectedGhs(int mhz) { return mhz * 2.04f; }

enum class TuneGoal : uint8_t { EFFICIENCY = 0, HASHRATE = 1, POWER_CAP = 2 };
const char *tuneGoalName(TuneGoal g);          // "efficiency" / "hashrate" / "power"
TuneGoal tuneGoalFromName(const char *s);

// User-visible searcher phases (the Lode UI strip).
enum class SearchPhase : uint8_t { OFF = 0, SEARCHING = 1, TRIMMING = 2, LOCKED = 3, WATCHING = 4, BACKOFF = 5 };
const char *searchPhaseName(SearchPhase p);

struct AutoTune {
    bool enabled = false;          // OFF by default
    int maxStepsUp = 2;            // at most +2 steps (+50 MHz) above the profile
    float maxErrorPct = 1.0f;      // ASIC hardware error rate ceiling
    float maxRejectPct = 1.0f;     // pool reject rate ceiling (over the last interval)
    uint32_t holdMs = 15 * 60000;  // kept for config compatibility; the searcher uses fixed 2+5 min windows
    int headroomC = 6;             // only used by the legacy Governor climb
    TuneGoal goal = TuneGoal::EFFICIENCY;
    int powerCapW = 0;             // 0 = use the protective power limit
    int lockedMhz = 0, lockedMv = 0;
    float lockedGhs = 0, lockedJth = 0;
};

struct Telemetry {
    float chipTempC = 0;   // 0 = unknown
    float vrTempC = 0;
    float powerW = 0;
    float errorPct = 0;    // ASIC hardware error %
    float rejectPct = 0;   // recent pool reject %
    bool mining = true;    // false while shut down / faulted / not initialised
    float ghs = 0;         // current hashrate in GH/s (searcher; ignored by Governor)
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


struct SearchConfig {
    bool enabled = false;                 // OFF by default
    TuneGoal goal = TuneGoal::EFFICIENCY; // Best efficiency never raises voltage
    int powerCapW = 0;                    // 0 = use the protective power limit; never above the PSU rating
    float maxErrorPct = 1.0f;
    float maxRejectPct = 1.0f;
    uint32_t warmupMs = 2 * 60000u;       // settle after a change
    uint32_t windowMs = 5 * 60000u;       // measurement window
    uint32_t watchMs = 10 * 60000u;       // recheck the lock
    int stepMhz = 25;
    int stepMv = 10;
    int trimMarginMv = 10;                // added back only if a trim succeeded
};

struct SearchLock {
    int mhz = 0, mv = 0;
    float ghs = 0, jth = 0;
    bool valid() const { return mhz > 0 && mv > 0; }
};

struct SearchStep {
    int mhz = 0, mv = 0;
    float jth = 0;
    char result[48] = "";
};

// Host-tested auto-tune searcher. Sits beside the Governor: this class proposes a
// point, the Governor still enforces the protective limits. Voltage is never raised
// above the profile (start) voltage. No flash or NVS I/O lives here.
class Searcher {
  public:
    static constexpr int kLog = 8;
    static constexpr float kMinHashRatio = 0.92f;
    static constexpr float kTrimHashKeep = 0.98f;
    static constexpr int kThermalMarginC = 2;
    static constexpr float kPowerMargin = 0.05f;
    static constexpr int kWorseBeforePeak = 2;

    void configure(const Envelope &e, const Limits &l, const SearchConfig &c);
    void setLock(const SearchLock &lock);   // restore a previously saved lock (NVS, RAM only here)
    void restart();                         // forget the lock and search again from the profile

    // start = active profile point. throttled = the Governor already cut the clock.
    OpPoint step(const OpPoint &start, const Telemetry &t, uint64_t nowMs, bool throttled = false);

    SearchPhase phase() const { return m_cfg.enabled ? m_phase : SearchPhase::OFF; }
    OpPoint trial() const { return m_trial; }
    SearchLock best() const { return m_best; }
    SearchLock lock() const { return m_lock; }
    bool lockDirty() const { return m_lockDirty; }
    void clearLockDirty() { m_lockDirty = false; }
    int progress() const { return m_progress; }
    const char *note() const { return m_note; }
    float liveJth() const { return m_liveJth; }
    float liveGhs() const { return m_liveGhs; }
    int powerCapW() const;
    int logCount() const { return m_nlog; }
    const SearchStep &logAt(int i) const { return m_log[i]; }   // 0 = oldest
    bool enabled() const { return m_cfg.enabled; }
    const SearchConfig &config() const { return m_cfg; }
    OpPoint clamp(const OpPoint &p) const;

  private:
    enum class Mode : uint8_t { Climb, Trim, Watch };
    enum class Wait : uint8_t { Idle, Warmup, Measure };

    Envelope m_env;
    Limits m_lim;
    SearchConfig m_cfg;
    SearchPhase m_phase = SearchPhase::OFF;
    Mode m_mode = Mode::Climb;
    Wait m_wait = Wait::Idle;
    OpPoint m_start{}, m_trial{};
    SearchLock m_best{}, m_lock{};
    bool m_lockDirty = false;
    bool m_needStart = true;
    bool m_didTrim = false;
    bool m_holdFail = false;    // no immediate fail until this point has warmed up
    int m_worse = 0;
    int m_progress = 0;
    float m_liveJth = 0, m_liveGhs = 0;
    float m_accGhs = 0, m_accPwr = 0, m_accErr = 0, m_accRej = 0;
    int m_accN = 0;
    float m_preTrimGhs = 0;
    uint64_t m_waitStart = 0, m_idleUntil = 0;
    char m_note[96] = "";
    SearchStep m_log[kLog]{};
    int m_nlog = 0;

    void resetAccum();
    void beginWarmup(uint64_t now);
    void addSample(const Telemetry &t);
    void evaluate(uint64_t now);
    void failPoint(const char *why, uint64_t now);
    void succeedClimb(float ghs, float jth, bool nearWall, uint64_t now);
    void succeedTrim(float ghs, float jth, uint64_t now);
    void succeedWatch(float ghs, float jth, uint64_t now);
    void startTrim(uint64_t now, const char *why);
    void finishTrim(uint64_t now);
    void applyLock(const SearchLock &L, uint64_t now, const char *why);
    void logStep(int mhz, int mv, float jth, const char *result);
    void setNote(const char *s);
    void setNotef(const char *fmt, ...);
    bool tempsKnown(const Telemetry &t) const;
    bool overLimit(const Telemetry &t) const;
    bool nearWall(const Telemetry &t) const;
    bool errorsBad(const Telemetry &t) const;
    bool stableWindow(float ghs, float err, float rej, int mhz) const;
    float joulesPerTh(float powerW, float ghs) const;
};

} // namespace costean
