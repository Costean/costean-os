// Costean OS boot guard (costean.6): remembers why and where the previous run ended, counts quick unexpected resets
// and switches to safe mode (setup hotspot + web UI only, no ASIC/mining/extras) after BG_SAFE_THRESHOLD of them.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "boot_guard_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

void boot_guard_init(void);                 // right after nvs_config_init()
void boot_guard_set_phase(bg_phase_t phase);
bg_phase_t boot_guard_phase(void);
void boot_guard_note_wifi(const char *status);   // last Wi-Fi status text, kept across a crash
void boot_guard_restart_requested(void);    // call before an intended esp_restart()
void boot_guard_start_stable_timer(void);   // after 120 s without a reset the quick-reset counter is cleared
bool boot_guard_safe_mode(void);
const bg_state_t *boot_guard_state(void);
const char *boot_guard_last_wifi(void);     // Wi-Fi status when the previous run ended ("" if unknown)
void boot_guard_leave_safe(void);           // clears safe mode in flash; the caller restarts
// "Crash (panic) while waiting for an IP address" (empty when the previous run ended normally)
void boot_guard_describe_last(char *out, size_t n);

#ifdef __cplusplus
}
#endif
