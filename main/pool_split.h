#ifndef POOL_SPLIT_H_
#define POOL_SPLIT_H_

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_transport.h"
#include "pool_split_core.h"
#include "stratum_api.h"

typedef struct GlobalState GlobalState;
typedef struct mining_notify mining_notify;

typedef struct CosteanPoolRuntime {
    int slot;
    int pool_index;
    uint16_t configured_percent;
    uint16_t effective_percent;
    bool connected;
    bool ready;
    bool should_stop;

    esp_transport_handle_t transport;
    portMUX_TYPE mux;
    SemaphoreHandle_t lock;
    int send_uid;

    char *extranonce_str;
    int extranonce_2_len;
    uint64_t extranonce_2;
    double difficulty;
    uint32_t version_mask;
    mining_notify *current_notify;
    stratum_recv_buf_t recv;

    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t jobs_sent;
    uint64_t work_received;

    TaskHandle_t task;
} CosteanPoolRuntime;

void pool_split_init(GlobalState *gs);
GlobalState *pool_split_global(void);
bool pool_split_is_enabled(void);
bool pool_split_is_active(void);
pool_split_error_t pool_split_current_error(void);

void pool_split_snapshot_config(GlobalState *gs, pool_split_config_t *out, bool *enabled);

int pool_split_participant_count(void);
CosteanPoolRuntime *pool_split_get(int slot);
CosteanPoolRuntime *pool_split_get_by_pool_index(int pool_index);

/* Pick a ready participant and increment its jobs_sent counter. -1 if none. */
int pool_split_take_job_slot(void);
void pool_split_refresh_effective(void);

typedef struct {
    int weight;          /* configured % (0 = backup) */
    int effective;       /* live % after failover */
    bool healthy;
    bool backup;
    float job_share;     /* recent share of ASIC jobs, 0..1 */
    double credited_ghs; /* pool-credited hashrate over 10 min */
} pool_split_stats_t;

void pool_split_on_accepted(int slot, double pool_diff);
void pool_split_stats(int slot, pool_split_stats_t *out);
bool pool_split_failover_active(void);
bool pool_split_restart_needed(void);

void pool_split_manager_task(void *pvParameters);
void pool_split_connection_task(void *pvParameters);

#endif /* POOL_SPLIT_H_ */
