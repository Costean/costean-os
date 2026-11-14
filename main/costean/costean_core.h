#pragma once
/* Costean OS 1.2 runtime for the Bitaxe Gamma 602: tuning profiles, safety governor, optional
 * auto-tune, weekly schedules (profile + screen) and the 48 h history kept in flash.
 * ESP-IDF glue around the host-tested logic in tuning / scheduler / rollup / board602. */
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GlobalState GlobalState;

typedef struct {
    char profile[8];      /* "ECO" "STOCK" "TURBO" "MANUAL" */
    bool throttled;
    bool scheduled;       /* a schedule window chose the profile */
    int tune_steps;
    int lim_chip, lim_vr, lim_power;
    uint8_t screen_mode;  /* 0 normal, 1 dim, 2 off */
} costean_screen_info_t;

void costean_init(GlobalState *gs);      /* after Wi-Fi and the ASIC are up */
void costean_before_restart(void);       /* flush the history to flash (waits up to 1.5 s) */

/* Operating point for the power task. false = no override (Manual: the saved frequency/voltage). */
bool costean_operating_point(float *mhz, uint16_t *mv);

uint8_t costean_screen_mode(void);
void costean_screen_info(costean_screen_info_t *out);

/* HTTP: GET /api/costean -> {status, config}; GET /api/costean/history; PATCH /api/costean */
cJSON *costean_status_json(void);
cJSON *costean_config_json(void);
cJSON *costean_history_json(void);
/* Validates and applies a config change. NULL on success, else a short error. */
const char *costean_patch_config(const cJSON *body);

#ifdef __cplusplus
}
#endif
