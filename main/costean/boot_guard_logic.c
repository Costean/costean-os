#include "boot_guard_logic.h"

bool bg_reason_unexpected(int reason, int prev_phase)
{
    switch (reason) {
    case BG_RST_PANIC: case BG_RST_INT_WDT: case BG_RST_TASK_WDT: case BG_RST_WDT: case BG_RST_BROWNOUT:
    case BG_RST_PWR_GLITCH: case BG_RST_CPU_LOCKUP:
        return true;
    case BG_RST_SW:
        // esp_restart() that nobody asked for (a module gave up). Asked-for restarts mark the phase first.
        return prev_phase != BG_PHASE_NONE && prev_phase != BG_PHASE_RESTART_REQ;
    default:
        return false;   // power-on, reset button, USB flashing, deep sleep, unknown
    }
}

void bg_on_boot(bg_state_t *st, int reason, int prev_phase, int threshold)
{
    if (threshold < 1) threshold = BG_SAFE_THRESHOLD;
    st->boots++;
    st->last_reason = (uint8_t) reason;
    st->last_phase = (uint8_t) (prev_phase >= 0 && prev_phase < BG_PHASE_COUNT ? prev_phase : BG_PHASE_NONE);
    st->last_unexpected = bg_reason_unexpected(reason, st->last_phase);
    st->safe_entered = false;
    if (st->last_unexpected && st->last_phase != BG_PHASE_STABLE) {
        if (st->quick < 250) st->quick++;
    } else if (!st->last_unexpected) {
        // A clean start (power-on, asked-for restart) breaks the chain.
        st->quick = 0;
    } else {
        // Crashed after it had been running fine for a while: count it once, do not escalate.
        st->quick = 1;
    }
    if (st->quick >= threshold && !st->safe) {
        st->safe = true;
        st->safe_entered = true;
    }
}

void bg_on_stable(bg_state_t *st)
{
    st->quick = 0;
}

void bg_leave_safe(bg_state_t *st)
{
    st->safe = false;
    st->safe_entered = false;
    st->quick = 0;
}

const char *bg_reason_text(int reason)
{
    switch (reason) {
    case BG_RST_POWERON: return "Power on";
    case BG_RST_EXT: return "Reset pin";
    case BG_RST_SW: return "Software restart";
    case BG_RST_PANIC: return "Crash (panic)";
    case BG_RST_INT_WDT: return "Interrupt watchdog";
    case BG_RST_TASK_WDT: return "Task watchdog";
    case BG_RST_WDT: return "Watchdog";
    case BG_RST_DEEPSLEEP: return "Wake from sleep";
    case BG_RST_BROWNOUT: return "Power dip (brownout)";
    case BG_RST_SDIO: return "SDIO reset";
    case BG_RST_USB: return "USB reset";
    case BG_RST_JTAG: return "JTAG reset";
    case BG_RST_EFUSE: return "eFuse error";
    case BG_RST_PWR_GLITCH: return "Power glitch";
    case BG_RST_CPU_LOCKUP: return "CPU lock-up";
    default: return "Unknown";
    }
}

const char *bg_reason_short(int reason)
{
    switch (reason) {
    case BG_RST_POWERON: return "POWER";
    case BG_RST_EXT: return "RSTPIN";
    case BG_RST_SW: return "RESTART";
    case BG_RST_PANIC: return "PANIC";
    case BG_RST_INT_WDT: return "INT WDT";
    case BG_RST_TASK_WDT: return "TSK WDT";
    case BG_RST_WDT: return "WDT";
    case BG_RST_BROWNOUT: return "BROWN";
    case BG_RST_USB: return "USB";
    case BG_RST_PWR_GLITCH: return "GLITCH";
    case BG_RST_CPU_LOCKUP: return "LOCKUP";
    default: return "OTHER";
    }
}

const char *bg_phase_text(int phase)
{
    switch (phase) {
    case BG_PHASE_BOOT: return "starting up";
    case BG_PHASE_WIFI_START: return "starting Wi-Fi";
    case BG_PHASE_JOINING: return "joining Wi-Fi";
    case BG_PHASE_DHCP: return "waiting for an IP address";
    case BG_PHASE_GOT_IP: return "finishing the Wi-Fi connection";
    case BG_PHASE_ASIC_INIT: return "powering up the ASIC";
    case BG_PHASE_SERVICES: return "starting mining services";
    case BG_PHASE_RUNNING: return "mining (first minutes)";
    case BG_PHASE_STABLE: return "mining";
    case BG_PHASE_RESTART_REQ: return "restarting on request";
    default: return "unknown";
    }
}

const char *bg_phase_short(int phase)
{
    switch (phase) {
    case BG_PHASE_BOOT: return "BOOT";
    case BG_PHASE_WIFI_START: return "WIFI";
    case BG_PHASE_JOINING: return "JOIN";
    case BG_PHASE_DHCP: return "DHCP";
    case BG_PHASE_GOT_IP: return "GOT IP";
    case BG_PHASE_ASIC_INIT: return "ASIC";
    case BG_PHASE_SERVICES: return "START";
    case BG_PHASE_RUNNING: return "MINING";
    case BG_PHASE_STABLE: return "MINING";
    case BG_PHASE_RESTART_REQ: return "ASKED";
    default: return "?";
    }
}
