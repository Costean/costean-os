// Costean OS boot guard: pure logic (no ESP-IDF headers) so it is unit-tested on the host.
// Reset reason numbers follow esp_reset_reason_t (esp_system.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BG_RST_UNKNOWN = 0, BG_RST_POWERON = 1, BG_RST_EXT = 2, BG_RST_SW = 3, BG_RST_PANIC = 4, BG_RST_INT_WDT = 5,
    BG_RST_TASK_WDT = 6, BG_RST_WDT = 7, BG_RST_DEEPSLEEP = 8, BG_RST_BROWNOUT = 9, BG_RST_SDIO = 10, BG_RST_USB = 11,
    BG_RST_JTAG = 12, BG_RST_EFUSE = 13, BG_RST_PWR_GLITCH = 14, BG_RST_CPU_LOCKUP = 15,
};

// Where the previous run was when it ended (kept in RTC memory, which survives a crash or watchdog reset).
typedef enum {
    BG_PHASE_NONE = 0,
    BG_PHASE_BOOT,          // early start-up
    BG_PHASE_WIFI_START,    // starting Wi-Fi and the setup hotspot
    BG_PHASE_JOINING,       // joining the saved Wi-Fi
    BG_PHASE_DHCP,          // joined, waiting for an IP address
    BG_PHASE_GOT_IP,        // got an IP address
    BG_PHASE_ASIC_INIT,     // powering up the ASIC
    BG_PHASE_SERVICES,      // starting mining, tuning and pool services
    BG_PHASE_RUNNING,       // running, not yet stable
    BG_PHASE_STABLE,        // ran long enough to count as a good boot
    BG_PHASE_RESTART_REQ,   // restart asked for (settings saved, web UI, update)
    BG_PHASE_COUNT
} bg_phase_t;

#define BG_SAFE_THRESHOLD 3   // this many unexpected resets in a row, each before the boot was stable -> safe mode

typedef struct {
    uint32_t boots;        // boots since the counters were created
    uint8_t quick;         // unexpected resets in a row that happened before the boot became stable
    bool safe;             // safe mode (sticky until the user leaves it)
    bool safe_entered;     // safe mode started on this boot
    uint8_t last_reason;   // why the previous run ended (BG_RST_*)
    uint8_t last_phase;    // what the previous run was doing (bg_phase_t), BG_PHASE_NONE if unknown
    bool last_unexpected;  // previous run ended in a crash, watchdog, brownout or an unasked restart
} bg_state_t;

// True when this reset reason means the firmware did not end the previous run on purpose.
bool bg_reason_unexpected(int reason, int prev_phase);
// Update the persistent counters for a new boot. prev_phase: BG_PHASE_NONE if the RTC record was lost.
void bg_on_boot(bg_state_t *st, int reason, int prev_phase, int threshold);
// The boot ran long enough: clear the quick-reset counter (safe mode stays until the user leaves it).
void bg_on_stable(bg_state_t *st);
// User asked to leave safe mode.
void bg_leave_safe(bg_state_t *st);

const char *bg_reason_text(int reason);      // "Crash (panic)", "Power dip (brownout)", ...
const char *bg_reason_short(int reason);     // 7 chars max for the OLED: "PANIC", "BROWNOUT"...
const char *bg_phase_text(int phase);        // "waiting for an IP address", ...
const char *bg_phase_short(int phase);       // 6 chars max for the OLED: "DHCP", "ASIC"...

#ifdef __cplusplus
}
#endif
