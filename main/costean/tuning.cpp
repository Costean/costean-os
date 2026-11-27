#include "tuning.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace costean {

const char *profileName(Profile p)
{
    switch (p) {
    case Profile::ECO: return "eco";
    case Profile::STOCK: return "stock";
    case Profile::TURBO: return "turbo";
    case Profile::CUSTOM: return "custom";
    case Profile::USER1: return "user1";
    case Profile::USER2: return "user2";
    default: return "none";
    }
}

Profile profileFromName(const char *s)
{
    if (!s) return Profile::NONE;
    if (!strcmp(s, "eco")) return Profile::ECO;
    if (!strcmp(s, "stock")) return Profile::STOCK;
    if (!strcmp(s, "turbo")) return Profile::TURBO;
    if (!strcmp(s, "custom")) return Profile::CUSTOM;
    if (!strcmp(s, "user1")) return Profile::USER1;
    if (!strcmp(s, "user2")) return Profile::USER2;
    return Profile::NONE;
}

const char *govStateName(GovState s)
{
    switch (s) {
    case GovState::THROTTLED: return "throttled";
    case GovState::TUNING_UP: return "tuning";
    case GovState::BACKED_OFF: return "backed-off";
    default: return "hold";
    }
}

OpPoint Presets::of(Profile p) const
{
    switch (p) {
    case Profile::ECO: return eco;
    case Profile::TURBO: return turbo;
    case Profile::CUSTOM: return custom;
    case Profile::USER1: return user1;
    case Profile::USER2: return user2;
    default: return stock;
    }
}

void Governor::configure(const Envelope &e, const Limits &l, const AutoTune &a)
{
    m_env = e;
    if (m_env.stepMhz <= 0) m_env.stepMhz = 25;
    m_lim = l;
    bool wasOn = m_at.enabled;
    m_at = a;
    if (m_at.maxStepsUp < 0) m_at.maxStepsUp = 0;
    if (m_at.maxStepsUp > 4) m_at.maxStepsUp = 4;
    if (wasOn && !m_at.enabled) resetTune();
}

void Governor::resetTune()
{
    m_tune = 0;
    m_bestStable = 0;
    m_failCeil = 1000;
    m_lastTune = 0;
    m_tuneStarted = false;
}

OpPoint Governor::clamp(const OpPoint &p) const
{
    OpPoint o = p;
    if (o.mhz > m_env.maxMhz) o.mhz = m_env.maxMhz;
    if (o.mhz < m_env.minMhz) o.mhz = m_env.minMhz;
    if (o.mv > m_env.maxMv) o.mv = m_env.maxMv;
    if (o.mv < m_env.minMv) o.mv = m_env.minMv;
    return o;
}

OpPoint Governor::step(const OpPoint &targetIn, const Telemetry &t, uint64_t now)
{
    OpPoint target = clamp(targetIn);
    if (target.mhz != m_lastTarget.mhz || target.mv != m_lastTarget.mv) {
        // new profile (user or schedule): auto-tune starts over from it
        if (m_lastTarget.mhz) resetTune();
        m_lastTarget = target;
    }

    // ---- limits: throttle instead of crashing
    bool over = false, near = false;
    m_reason = "";
    if (t.chipTempC > 0 && t.chipTempC >= m_lim.chipTempC) { over = true; m_reason = "chip temperature"; }
    else if (t.vrTempC > 0 && t.vrTempC >= m_lim.vrTempC) { over = true; m_reason = "regulator temperature"; }
    else if (t.powerW > 0 && t.powerW >= m_lim.powerW) { over = true; m_reason = "power limit"; }
    if (!over) {
        near = (t.chipTempC > m_lim.chipTempC - m_lim.hysteresisC) || (t.vrTempC > m_lim.vrTempC - m_lim.hysteresisC) ||
               (t.powerW > m_lim.powerW * 0.95f);
    }

    int floorSteps = (target.mhz - m_env.minMhz) / m_env.stepMhz;   // how far down we may go
    if (floorSteps < 0) floorSteps = 0;

    if (t.mining && over) {
        if (m_tune > 0) {
            // first give back anything auto-tune added, and never retry that step
            m_failCeil = m_tune - 1 < m_failCeil ? m_tune - 1 : m_failCeil;
            m_tune = 0;
            m_lastDown = now;
            m_state = GovState::BACKED_OFF;
        } else if (m_lastDown == 0 || now - m_lastDown >= kThrottleEveryMs) {
            if (m_throttle < floorSteps) m_throttle++;
            m_lastDown = now;
            m_state = GovState::THROTTLED;
        }
    } else if (m_throttle > 0) {
        if (!near && now - m_lastDown >= kRecoverEveryMs && (m_lastUp == 0 || now - m_lastUp >= kRecoverEveryMs)) {
            m_throttle--;
            m_lastUp = now;
        }
        m_state = m_throttle > 0 ? GovState::THROTTLED : GovState::HOLD;
        if (m_throttle > 0) m_reason = "cooling down";
    } else if (m_at.enabled && t.mining) {
        // ---- careful auto-tune: frequency only, voltage is never raised
        bool errBad = t.errorPct > m_at.maxErrorPct || t.rejectPct > m_at.maxRejectPct;
        if (errBad && m_tune > 0) {
            m_failCeil = m_tune - 1;
            m_tune--;
            m_lastTune = now;
            m_state = GovState::BACKED_OFF;
            m_reason = "error rate";
        } else if (!m_tuneStarted) {
            m_tuneStarted = true;
            m_lastTune = now; // start dwell at the profile point
            m_state = GovState::HOLD;
        } else if (now - m_lastTune >= m_at.holdMs) {
            if (!errBad) m_bestStable = m_tune;
            bool headroom = t.chipTempC > 0 && t.chipTempC <= m_lim.chipTempC - m_at.headroomC &&
                            (t.vrTempC <= 0 || t.vrTempC <= m_lim.vrTempC - m_at.headroomC) &&
                            (t.powerW <= 0 || t.powerW <= m_lim.powerW * 0.9f);
            int cap = m_at.maxStepsUp < m_failCeil ? m_at.maxStepsUp : m_failCeil;
            if (!errBad && headroom && m_tune < cap && target.mhz + (m_tune + 1) * m_env.stepMhz <= m_env.maxMhz) {
                m_tune++;
                m_state = GovState::TUNING_UP;
            } else {
                m_state = GovState::HOLD;
            }
            m_lastTune = now;
        }
    } else {
        m_state = GovState::HOLD;
    }

    OpPoint out = target;
    out.mhz = target.mhz + (m_tune - m_throttle) * m_env.stepMhz;
    return clamp(out);
}


const char *tuneGoalName(TuneGoal g)
{
    switch (g) {
    case TuneGoal::HASHRATE: return "hashrate";
    case TuneGoal::POWER_CAP: return "power";
    default: return "efficiency";
    }
}

TuneGoal tuneGoalFromName(const char *s)
{
    if (s && !strcmp(s, "hashrate")) return TuneGoal::HASHRATE;
    if (s && !strcmp(s, "power")) return TuneGoal::POWER_CAP;
    return TuneGoal::EFFICIENCY;
}

const char *searchPhaseName(SearchPhase p)
{
    switch (p) {
    case SearchPhase::SEARCHING: return "searching";
    case SearchPhase::TRIMMING: return "trimming";
    case SearchPhase::LOCKED: return "locked";
    case SearchPhase::WATCHING: return "watching";
    case SearchPhase::BACKOFF: return "backoff";
    default: return "off";
    }
}

int Searcher::powerCapW() const
{
    int cap = m_cfg.powerCapW > 0 ? m_cfg.powerCapW : m_lim.powerW;
    if (m_cfg.goal != TuneGoal::POWER_CAP) cap = m_lim.powerW;
    if (m_lim.powerW > 0 && cap > m_lim.powerW) cap = m_lim.powerW;
    if (m_lim.psuW > 0 && cap > m_lim.psuW) cap = m_lim.psuW;
    if (cap < 1) cap = m_lim.powerW;
    return cap;
}

OpPoint Searcher::clamp(const OpPoint &p) const
{
    OpPoint o = p;
    if (o.mhz > m_env.maxMhz) o.mhz = m_env.maxMhz;
    if (o.mhz < m_env.minMhz) o.mhz = m_env.minMhz;
    if (o.mv > m_env.maxMv) o.mv = m_env.maxMv;
    if (o.mv < m_env.minMv) o.mv = m_env.minMv;
    // Best efficiency (and this searcher in general) never raises voltage above the profile.
    if (m_start.mv && o.mv > m_start.mv) o.mv = m_start.mv;
    return o;
}

void Searcher::configure(const Envelope &e, const Limits &l, const SearchConfig &c)
{
    TuneGoal oldGoal = m_cfg.goal;
    int oldCap = m_cfg.powerCapW;
    bool wasOn = m_cfg.enabled;
    m_env = e;
    if (m_env.stepMhz <= 0) m_env.stepMhz = 25;
    m_lim = l;
    m_cfg = c;
    if (m_cfg.stepMhz <= 0) m_cfg.stepMhz = m_env.stepMhz;
    if (m_cfg.stepMv <= 0) m_cfg.stepMv = 10;
    if (m_cfg.trimMarginMv < 0) m_cfg.trimMarginMv = 0;
    if (m_cfg.warmupMs < 1) m_cfg.warmupMs = 1;
    if (m_cfg.windowMs < 1) m_cfg.windowMs = 1;
    if (m_cfg.watchMs < 1) m_cfg.watchMs = 1;
    if (!m_cfg.enabled) {
        m_phase = SearchPhase::OFF;
        m_wait = Wait::Idle;
        return;
    }
    if (!wasOn) m_needStart = true;
    else if (oldGoal != m_cfg.goal || oldCap != m_cfg.powerCapW) restart();
}

void Searcher::setLock(const SearchLock &lock)
{
    m_lock = lock;
    m_best = lock;
    m_lockDirty = false;
}

void Searcher::restart()
{
    m_lock = SearchLock{};
    m_best = SearchLock{};
    m_lockDirty = true;   // persist the cleared lock
    m_needStart = true;
    m_didTrim = false;
    m_holdFail = false;
    m_worse = 0;
    m_nlog = 0;
    m_wait = Wait::Idle;
    m_phase = m_cfg.enabled ? SearchPhase::SEARCHING : SearchPhase::OFF;
    m_note[0] = 0;
    m_liveJth = m_liveGhs = 0;
    m_progress = 0;
}

void Searcher::resetAccum()
{
    m_accGhs = m_accPwr = m_accErr = m_accRej = 0;
    m_accN = 0;
}

void Searcher::beginWarmup(uint64_t now)
{
    m_wait = Wait::Warmup;
    m_waitStart = now;
    m_progress = 0;
    resetAccum();
    m_trial = clamp(m_trial);
    if (m_mode == Mode::Trim) m_phase = SearchPhase::TRIMMING;
    else if (m_mode == Mode::Watch) m_phase = SearchPhase::WATCHING;
    else m_phase = SearchPhase::SEARCHING;
}

void Searcher::addSample(const Telemetry &t)
{
    m_accGhs += t.ghs;
    m_accPwr += t.powerW;
    m_accErr += t.errorPct;
    m_accRej += t.rejectPct;
    m_accN++;
}

void Searcher::logStep(int mhz, int mv, float jth, const char *result)
{
    SearchStep s;
    s.mhz = mhz;
    s.mv = mv;
    s.jth = jth;
    snprintf(s.result, sizeof s.result, "%s", result ? result : "");
    if (m_nlog < kLog) m_log[m_nlog++] = s;
    else {
        for (int i = 1; i < kLog; i++) m_log[i - 1] = m_log[i];
        m_log[kLog - 1] = s;
    }
}

void Searcher::setNote(const char *s)
{
    snprintf(m_note, sizeof m_note, "%s", s ? s : "");
}

void Searcher::setNotef(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m_note, sizeof m_note, fmt, ap);
    va_end(ap);
}

bool Searcher::tempsKnown(const Telemetry &t) const { return t.chipTempC > 0 && t.vrTempC > 0; }

bool Searcher::overLimit(const Telemetry &t) const
{
    if (t.chipTempC > 0 && t.chipTempC >= m_lim.chipTempC) return true;
    if (t.vrTempC > 0 && t.vrTempC >= m_lim.vrTempC) return true;
    int cap = powerCapW();
    if (cap > 0 && t.powerW > 0 && t.powerW >= cap) return true;
    return false;
}

bool Searcher::nearWall(const Telemetry &t) const
{
    if (t.chipTempC > 0 && t.chipTempC >= m_lim.chipTempC - kThermalMarginC) return true;
    if (t.vrTempC > 0 && t.vrTempC >= m_lim.vrTempC - kThermalMarginC) return true;
    int cap = powerCapW();
    if (cap > 0 && t.powerW > 0 && t.powerW >= cap * (1.0f - kPowerMargin)) return true;
    return false;
}

bool Searcher::errorsBad(const Telemetry &t) const
{
    return t.errorPct > m_cfg.maxErrorPct || t.rejectPct > m_cfg.maxRejectPct;
}

bool Searcher::stableWindow(float ghs, float err, float rej, int mhz) const
{
    if (err > m_cfg.maxErrorPct || rej > m_cfg.maxRejectPct) return false;
    float exp = expectedGhs(mhz);
    if (exp <= 0 || ghs < exp * kMinHashRatio) return false;
    return true;
}

float Searcher::joulesPerTh(float powerW, float ghs) const
{
    if (ghs <= 0) return 0;
    return powerW / (ghs / 1000.0f);
}

void Searcher::applyLock(const SearchLock &L, uint64_t now, const char *why)
{
    SearchLock c = L;
    OpPoint p = clamp({c.mhz, c.mv});
    c.mhz = p.mhz;
    c.mv = p.mv;
    m_lock = c;
    m_best = c;
    m_trial = p;
    m_lockDirty = true;
    m_mode = Mode::Watch;
    m_wait = Wait::Idle;
    m_idleUntil = now + m_cfg.watchMs;
    m_phase = SearchPhase::LOCKED;
    m_progress = 100;
    m_liveGhs = c.ghs;
    m_liveJth = c.jth;
    if (why) setNote(why);
}

void Searcher::startTrim(uint64_t now, const char *why)
{
    if (m_best.valid()) m_trial = clamp({m_best.mhz, m_start.mv ? m_start.mv : m_best.mv});
    m_preTrimGhs = m_best.ghs;
    m_didTrim = false;
    m_mode = Mode::Trim;
    int next = m_trial.mv - m_cfg.stepMv;
    if (next < m_env.minMv) {
        finishTrim(now);
        return;
    }
    if (why) logStep(m_trial.mhz, m_trial.mv, m_best.jth, why);
    m_trial.mv = next;
    m_phase = SearchPhase::TRIMMING;
    setNotef("Trimming voltage at %d MHz. Next trial %d mV.", m_trial.mhz, m_trial.mv);
    beginWarmup(now);
}

void Searcher::finishTrim(uint64_t now)
{
    OpPoint p = m_best.valid() ? OpPoint{m_best.mhz, m_best.mv} : m_trial;
    if (m_didTrim && m_cfg.trimMarginMv > 0) {
        p.mv += m_cfg.trimMarginMv;
        if (m_start.mv && p.mv > m_start.mv) p.mv = m_start.mv;
        p = clamp(p);
        logStep(p.mhz, p.mv, m_best.jth, "locked with 10 mV margin");
        setNotef("Locked %d MHz @ %d mV with a 10 mV margin.", p.mhz, p.mv);
    } else {
        p = clamp(p);
        logStep(p.mhz, p.mv, m_best.jth, "locked");
        setNotef("Locked %d MHz @ %d mV.", p.mhz, p.mv);
    }
    SearchLock L = m_best;
    L.mhz = p.mhz;
    L.mv = p.mv;
    applyLock(L, now, m_note);
}

void Searcher::failPoint(const char *why, uint64_t now)
{
    m_holdFail = true;
    logStep(m_trial.mhz, m_trial.mv, m_liveJth, why);
    if (m_mode == Mode::Trim) {
        // undo this voltage; if a previous trim was good, finish with a margin
        int prev = m_trial.mv + m_cfg.stepMv;
        if (m_start.mv && prev > m_start.mv) prev = m_start.mv;
        if (prev > m_env.maxMv) prev = m_env.maxMv;
        m_trial.mv = prev;
        if (m_didTrim) {
            m_best.mv = prev;
            finishTrim(now);
        } else {
            m_best.mv = prev;
            m_best.mhz = m_trial.mhz;
            finishTrim(now);
        }
        return;
    }
    if (m_mode == Mode::Watch) {
        int down = m_trial.mhz - m_cfg.stepMhz;
        if (down < m_env.minMhz) down = m_env.minMhz;
        m_trial.mhz = down;
        m_trial.mv = m_lock.valid() ? m_lock.mv : m_trial.mv;
        m_trial = clamp(m_trial);
        SearchLock L = m_lock;
        L.mhz = m_trial.mhz;
        L.mv = m_trial.mv;
        m_phase = SearchPhase::BACKOFF;
        setNotef("Watch failed (%s). Backed off to %d MHz / %d mV.", why, m_trial.mhz, m_trial.mv);
        applyLock(L, now, m_note);
        m_phase = SearchPhase::BACKOFF;
        beginWarmup(now);
        m_mode = Mode::Watch;
        m_phase = SearchPhase::BACKOFF;
        return;
    }
    // Climb: last good point is a wall; otherwise drop a step from the start
    if (m_best.valid() && (m_best.mhz != m_trial.mhz || m_best.mv != m_trial.mv)) {
        m_phase = SearchPhase::BACKOFF;
        startTrim(now, why);
        return;
    }
    int down = m_trial.mhz - m_cfg.stepMhz;
    if (down >= m_env.minMhz) {
        m_trial.mhz = down;
        m_trial = clamp(m_trial);
        m_phase = SearchPhase::BACKOFF;
        setNotef("Start point was not stable. Retrying at %d MHz / %d mV.", m_trial.mhz, m_trial.mv);
        m_mode = Mode::Climb;
        beginWarmup(now);
        m_phase = SearchPhase::BACKOFF;
        return;
    }
    // Already at the floor: lock whatever we have if we have a best, else hold the floor
    if (m_best.valid()) applyLock(m_best, now, "Could not climb; locked the last stable point.");
    else {
        m_phase = SearchPhase::BACKOFF;
        setNotef("Cannot go below %d MHz. Holding and retrying.", m_env.minMhz);
        beginWarmup(now);
        m_phase = SearchPhase::BACKOFF;
    }
}

void Searcher::succeedClimb(float ghs, float jth, bool wall, uint64_t now)
{
    bool better = false;
    if (!m_best.valid()) better = true;
    else if (m_cfg.goal == TuneGoal::EFFICIENCY) {
        float slack = m_best.jth * 0.002f;
        if (slack < 0.03f) slack = 0.03f;
        if (jth + slack < m_best.jth) better = true;
        else if (jth > m_best.jth + slack) m_worse++;
        else m_worse = 0;   // about the same
    } else {
        if (ghs > m_best.ghs * 1.005f) better = true;
    }
    if (better) {
        m_best = {m_trial.mhz, m_trial.mv, ghs, jth};
        m_worse = 0;
        logStep(m_trial.mhz, m_trial.mv, jth, m_cfg.goal == TuneGoal::EFFICIENCY ? "better J/TH" : "better hashrate");
    } else if (m_cfg.goal == TuneGoal::EFFICIENCY && m_worse >= kWorseBeforePeak) {
        logStep(m_trial.mhz, m_trial.mv, jth, "worse — peak");
        startTrim(now, "efficiency peaked");
        return;
    } else if (m_cfg.goal == TuneGoal::EFFICIENCY) {
        logStep(m_trial.mhz, m_trial.mv, jth, m_worse ? "worse, confirming" : "about the same");
    } else {
        logStep(m_trial.mhz, m_trial.mv, jth, "stable");
    }

    int next = m_trial.mhz + m_cfg.stepMhz;
    bool canClimb = !wall && next <= m_env.maxMhz &&
                    (m_cfg.goal != TuneGoal::EFFICIENCY || m_worse < kWorseBeforePeak);
    // Never climb when the first point was recorded as not-better and we're at max? handled.
    if (canClimb) {
        m_trial.mhz = next;
        m_trial.mv = m_start.mv;   // hold the start voltage; never raise it
        m_trial = clamp(m_trial);
        m_mode = Mode::Climb;
        setNotef("Testing %d MHz @ %d mV. Started from %d / %d.", m_trial.mhz, m_trial.mv, m_start.mhz, m_start.mv);
        beginWarmup(now);
        return;
    }
    startTrim(now, wall ? "near a limit — stop" : "max clock");
}

void Searcher::succeedTrim(float ghs, float jth, uint64_t now)
{
    bool keepHash = m_preTrimGhs <= 0 || ghs >= m_preTrimGhs * kTrimHashKeep;
    if (!keepHash) {
        failPoint("hashrate dropped", now);
        return;
    }
    m_didTrim = true;
    m_best = {m_trial.mhz, m_trial.mv, ghs, jth};
    logStep(m_trial.mhz, m_trial.mv, jth, "trim, stable");
    int next = m_trial.mv - m_cfg.stepMv;
    if (next < m_env.minMv) {
        finishTrim(now);
        return;
    }
    m_trial.mv = next;
    setNotef("Trimming voltage at %d MHz. Next trial %d mV.", m_trial.mhz, m_trial.mv);
    beginWarmup(now);
}

void Searcher::succeedWatch(float ghs, float jth, uint64_t now)
{
    m_best = {m_trial.mhz, m_trial.mv, ghs, jth};
    if (!m_lock.valid() || m_lock.mhz != m_trial.mhz || m_lock.mv != m_trial.mv ||
        (jth > 0 && m_lock.jth <= 0)) {
        applyLock(m_best, now, "Watch window stable");
    } else {
        m_lock.ghs = ghs;
        m_lock.jth = jth;
        m_lockDirty = true;
        m_wait = Wait::Idle;
        m_idleUntil = now + m_cfg.watchMs;
        m_phase = SearchPhase::LOCKED;
        m_progress = 100;
        setNotef("Watching %d MHz @ %d mV. Last window was stable.", m_trial.mhz, m_trial.mv);
        logStep(m_trial.mhz, m_trial.mv, jth, "watch window stable");
    }
}

void Searcher::evaluate(uint64_t now)
{
    float ghs = m_accN ? m_accGhs / m_accN : 0;
    float pwr = m_accN ? m_accPwr / m_accN : 0;
    float err = m_accN ? m_accErr / m_accN : 0;
    float rej = m_accN ? m_accRej / m_accN : 0;
    float jth = joulesPerTh(pwr, ghs);
    m_liveGhs = ghs;
    m_liveJth = jth;
    if (!stableWindow(ghs, err, rej, m_trial.mhz)) {
        const char *why = "not stable";
        if (err > m_cfg.maxErrorPct) why = "errors — stop";
        else if (rej > m_cfg.maxRejectPct) why = "rejects — stop";
        else if (ghs < expectedGhs(m_trial.mhz) * kMinHashRatio) why = "hashrate low — stop";
        failPoint(why, now);
        return;
    }
    if (m_mode == Mode::Trim) succeedTrim(ghs, jth, now);
    else if (m_mode == Mode::Watch) succeedWatch(ghs, jth, now);
    else succeedClimb(ghs, jth, false, now);  // near-wall is decided from live telemetry in step()
}

OpPoint Searcher::step(const OpPoint &startIn, const Telemetry &t, uint64_t now, bool throttled)
{
    if (!m_cfg.enabled) {
        m_phase = SearchPhase::OFF;
        m_progress = 0;
        m_trial = startIn;
        return startIn;
    }

    OpPoint start = startIn;
    // Detect a profile change (the start point moved). Ignore our own trial as "start".
    if (!m_needStart && m_start.mhz && (start.mhz != m_start.mhz || start.mv != m_start.mv)) {
        restart();
        m_lockDirty = true;
    }

    if (m_needStart) {
        m_start = start;
        m_needStart = false;
        m_worse = 0;
        m_didTrim = false;
        if (m_lock.valid()) {
            m_trial = clamp({m_lock.mhz, m_lock.mv});
            m_best = m_lock;
            m_mode = Mode::Watch;
            setNotef("Restored lock %d MHz @ %d mV. Rechecking.", m_trial.mhz, m_trial.mv);
            beginWarmup(now);
        } else {
            m_trial = clamp(start);
            m_mode = Mode::Climb;
            setNotef("Testing %d MHz @ %d mV. Started from %d / %d.", m_trial.mhz, m_trial.mv, m_start.mhz, m_start.mv);
            beginWarmup(now);
        }
        return clamp(m_trial);
    }

    m_liveGhs = t.ghs;
    if (t.ghs > 0 && t.powerW > 0) m_liveJth = joulesPerTh(t.powerW, t.ghs);

    // Pause while not mining (no window progress, keep the trial).
    if (!t.mining) return clamp(m_trial);

    // Immediate rollback only for overheating or a Governor throttle. Error rate is
    // judged at the end of the measurement window so one bad reading cannot drop the
    // clock every tick. After a rollback, the new point must finish warmup first.
    if (!m_holdFail && (overLimit(t) || throttled) && m_wait != Wait::Idle) {
        const char *why = throttled ? "governor throttled" : "overheating — stop";
        failPoint(why, now);
        return clamp(m_trial);
    }

    // Locked / idle between watch windows
    if (m_wait == Wait::Idle && m_mode == Mode::Watch) {
        m_phase = SearchPhase::LOCKED;
        m_progress = 100;
        if (now >= m_idleUntil) {
            m_mode = Mode::Watch;
            setNotef("Rechecking %d MHz @ %d mV.", m_trial.mhz, m_trial.mv);
            beginWarmup(now);
            m_phase = SearchPhase::WATCHING;
        }
        return clamp(m_trial);
    }

    if (m_wait == Wait::Warmup) {
        uint64_t span = m_cfg.warmupMs;
        m_progress = span ? (int) ((now - m_waitStart) * 100 / span) : 100;
        if (m_progress > 99) m_progress = 99;
        if (now - m_waitStart >= span) {
            m_wait = Wait::Measure;
            m_waitStart = m_waitStart + span;   // leftover time counts toward the measure window
            resetAccum();
            m_holdFail = false;
        } else {
            return clamp(m_trial);
        }
    }

    if (m_wait == Wait::Measure) {
        // Do not climb or lock when temperatures are missing.
        addSample(t);
        uint64_t span = m_cfg.windowMs;
        int p = span ? (int) ((now - m_waitStart) * 100 / span) : 100;
        if (p > 99) p = 99;
        m_progress = p;
        if (!tempsKnown(t) && now - m_waitStart >= span) {
            // Cannot judge this point: stay put and try the same trial again.
            logStep(m_trial.mhz, m_trial.mv, m_liveJth, "missing temperatures");
            setNotef("Waiting for chip and regulator temperatures at %d MHz / %d mV.", m_trial.mhz, m_trial.mv);
            beginWarmup(now);
            return clamp(m_trial);
        }
        // Near a wall at the end of a climb window: treat as a completed but un-climbable point.
        if (now - m_waitStart >= span) {
            if (m_mode == Mode::Climb && (nearWall(t) || !tempsKnown(t))) {
                float ghs = m_accN ? m_accGhs / m_accN : t.ghs;
                float pwr = m_accN ? m_accPwr / m_accN : t.powerW;
                float err = m_accN ? m_accErr / m_accN : t.errorPct;
                float rej = m_accN ? m_accRej / m_accN : t.rejectPct;
                float jth = joulesPerTh(pwr, ghs);
                m_liveGhs = ghs;
                m_liveJth = jth;
                if (!tempsKnown(t) || !stableWindow(ghs, err, rej, m_trial.mhz)) {
                    if (!tempsKnown(t)) {
                        logStep(m_trial.mhz, m_trial.mv, jth, "missing temperatures");
                        beginWarmup(now);
                    } else {
                        failPoint("not stable", now);
                    }
                } else {
                    if (!m_best.valid() || ghs >= m_best.ghs * 0.98f) m_best = {m_trial.mhz, m_trial.mv, ghs, jth};
                    logStep(m_trial.mhz, m_trial.mv, jth, "stable, near a limit");
                    startTrim(now, "near a limit — stop");
                }
            } else {
                evaluate(now);
            }
        }
    }
    return clamp(m_trial);
}

} // namespace costean
