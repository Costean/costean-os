#include "tuning.h"
#include <string.h>

namespace costean {

const char *profileName(Profile p)
{
    switch (p) {
    case Profile::ECO: return "eco";
    case Profile::STOCK: return "stock";
    case Profile::TURBO: return "turbo";
    case Profile::CUSTOM: return "custom";
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

} // namespace costean
