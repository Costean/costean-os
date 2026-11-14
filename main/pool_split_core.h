#ifndef POOL_SPLIT_CORE_H_
#define POOL_SPLIT_CORE_H_

#include <stdbool.h>
#include <stdint.h>

#define COSTEAN_MAX_SPLIT_POOLS 4

typedef struct {
    uint16_t percent[COSTEAN_MAX_SPLIT_POOLS];
    bool is_sv2[COSTEAN_MAX_SPLIT_POOLS];
    bool has_url[COSTEAN_MAX_SPLIT_POOLS];
    int count; /* number of slots inspected, <= COSTEAN_MAX_SPLIT_POOLS */
} pool_split_config_t;

typedef enum {
    POOL_SPLIT_OK = 0,
    POOL_SPLIT_ERR_DISABLED,
    POOL_SPLIT_ERR_TOO_FEW,
    POOL_SPLIT_ERR_TOO_MANY,
    POOL_SPLIT_ERR_SUM,
    POOL_SPLIT_ERR_SV2,
    POOL_SPLIT_ERR_EMPTY,
} pool_split_error_t;

/* Validate a requested split. percents of participating (percent>0) pools must
 * sum to 100, there must be 2-4 of them, and none may be SV2. */
pool_split_error_t pool_split_validate(const pool_split_config_t *cfg, bool enabled);

const char *pool_split_error_string(pool_split_error_t err);

/* Redistribute configured percents across currently connected participants so
 * the live weights still sum to 100. Unconnected or 0% slots get 0. */
void pool_split_redistribute(const uint16_t *configured, const bool *connected,
                             uint16_t *effective, int n);

/* Weighted deficit round-robin. Returns the ready participant whose sent-job
 * share is furthest below its effective target, or -1 if none are ready. */
int pool_split_pick(const uint16_t *effective_pct, const bool *ready,
                    const uint32_t *jobs_sent, int n, uint32_t total_sent);

#endif /* POOL_SPLIT_CORE_H_ */
