#pragma once
/* C side of the Costean OS runtime: reads ESP-Miner state (GlobalState, NVS, pool split) for the
 * C++ core, which cannot include ESP-Miner's C-only headers. */
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GlobalState GlobalState;

typedef struct {
    float chip_c;        /* hottest ASIC sensor */
    float vr_c;
    float power_w;
    float error_pct;     /* ASIC hardware error % */
    float ghs;
    uint64_t accepted, rejected;
    bool mining;         /* ASIC up, not paused / faulted / in safe mode / self test */
    bool overheat;       /* stock overheat safe mode */
    bool self_test;
    bool failover;       /* split failover or ESP-Miner fallback pool in use */
    int stock_mhz, stock_mv;   /* board defaults */
    int user_mhz, user_mv;     /* saved settings (Manual) */
} costean_hw_t;

void costean_hw_set_global(GlobalState *gs);
void costean_hw_read(costean_hw_t *out);
void costean_hw_split_json(cJSON *status);

#ifdef __cplusplus
}
#endif
