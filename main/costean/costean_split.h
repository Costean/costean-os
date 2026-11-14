#pragma once
/* C bridge to costean::PoolSplit (pool_split.h) for ESP-Miner's C split runtime (main/pool_split.c).
 * Not thread safe by itself: main/pool_split.c calls it inside its own critical section. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void costean_split_setup(const uint8_t *weights, int n);
/* health per slot; returns true when the effective split changed; eff_out gets the live % per slot */
bool costean_split_update(const bool *healthy, uint64_t now_ms, uint8_t *eff_out);
int costean_split_next(void);
void costean_split_on_accepted(int slot, double pool_diff, uint64_t now_ms);
double costean_split_credited_ghs(int slot, uint64_t now_ms);
float costean_split_job_share(int slot);
bool costean_split_healthy(int slot);
bool costean_split_failover(void);
int costean_split_weight(int slot);

#ifdef __cplusplus
}
#endif
