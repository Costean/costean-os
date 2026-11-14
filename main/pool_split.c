#include "pool_split.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_config.h"
#include "global_state.h"
#include "connect.h"
#include "stratum_v2_task.h"
#include "esp_timer.h"
#include "costean/costean_split.h"

static const char *TAG = "pool_split";

static GlobalState *s_gs;
static CosteanPoolRuntime s_rt[COSTEAN_MAX_SPLIT_POOLS];
static int s_count;
static bool s_enabled;
static bool s_active;
static pool_split_error_t s_error = POOL_SPLIT_ERR_DISABLED;
static portMUX_TYPE s_pick_mux = portMUX_INITIALIZER_UNLOCKED;

void pool_split_snapshot_config(GlobalState *gs, pool_split_config_t *out, bool *enabled)
{
    memset(out, 0, sizeof(*out));
    out->count = COSTEAN_MAX_SPLIT_POOLS;
    /* First four configured pool slots (0..3) are the split participants. */
    for (int i = 0; i < COSTEAN_MAX_SPLIT_POOLS; i++) {
        PoolConfig *p = &gs->SYSTEM_MODULE.pools[i];
        out->percent[i] = p->split_percent;
        out->is_sv2[i] = (p->protocol == STRATUM_PROTOCOL_V2);
        out->has_url[i] = (p->url && p->url[0] != '\0');
    }
    if (enabled) {
        *enabled = nvs_config_get_bool(NVS_CONFIG_POOL_SPLIT_ENABLED);
    }
}

GlobalState *pool_split_global(void)
{
    return s_gs;
}

void pool_split_init(GlobalState *gs)
{
    s_gs = gs;
    s_count = 0;
    s_active = false;
    s_enabled = nvs_config_get_bool(NVS_CONFIG_POOL_SPLIT_ENABLED);
    memset(s_rt, 0, sizeof(s_rt));

    pool_split_config_t cfg;
    bool enabled = false;
    pool_split_snapshot_config(gs, &cfg, &enabled);
    s_enabled = enabled;
    s_error = pool_split_validate(&cfg, enabled);
    if (s_error != POOL_SPLIT_OK) {
        if (enabled) {
            ESP_LOGW(TAG, "Pool split requested but not active: %s", pool_split_error_string(s_error));
        }
        s_active = false;
        return;
    }

    /* Weighted pools first take their share; V1 pools with a URL at 0 % join as backups
     * (they only get work when every weighted pool is down). */
    uint8_t weights[COSTEAN_MAX_SPLIT_POOLS] = {0};
    for (int i = 0; i < COSTEAN_MAX_SPLIT_POOLS; i++) {
        bool backup = cfg.percent[i] == 0 && cfg.has_url[i] && !cfg.is_sv2[i];
        if (cfg.percent[i] == 0 && !backup) {
            continue;
        }
        CosteanPoolRuntime *rt = &s_rt[s_count];
        memset(rt, 0, sizeof(*rt));
        rt->slot = s_count;
        rt->pool_index = i;
        rt->configured_percent = cfg.percent[i];
        rt->effective_percent = cfg.percent[i];
        rt->mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        rt->lock = xSemaphoreCreateMutex();
        rt->difficulty = 1.0;
        rt->version_mask = 0x1fffe000;
        rt->send_uid = 1;
        if (STRATUM_V1_recv_buf_init(&rt->recv, 2048) != 0) {
            ESP_LOGE(TAG, "Failed to allocate recv buffer for pool %d", i);
            s_active = false;
            s_error = POOL_SPLIT_ERR_EMPTY;
            return;
        }
        weights[s_count] = (uint8_t) cfg.percent[i];
        s_count++;
    }
    costean_split_setup(weights, s_count);

    s_active = (s_count >= 2);
    ESP_LOGI(TAG, "Costean pool split initialized (%d pools incl. backups)", s_count);
}

bool pool_split_is_enabled(void) { return s_enabled; }
bool pool_split_is_active(void) { return s_active; }
pool_split_error_t pool_split_current_error(void) { return s_error; }
int pool_split_participant_count(void) { return s_count; }

CosteanPoolRuntime *pool_split_get(int slot)
{
    if (slot < 0 || slot >= s_count) {
        return NULL;
    }
    return &s_rt[slot];
}

CosteanPoolRuntime *pool_split_get_by_pool_index(int pool_index)
{
    for (int i = 0; i < s_count; i++) {
        if (s_rt[i].pool_index == pool_index) {
            return &s_rt[i];
        }
    }
    return NULL;
}

static uint64_t now_ms(void) { return (uint64_t) (esp_timer_get_time() / 1000); }

void pool_split_refresh_effective(void)
{
    bool healthy[COSTEAN_MAX_SPLIT_POOLS] = {0};
    uint8_t eff[COSTEAN_MAX_SPLIT_POOLS] = {0};
    int live = 0;
    for (int i = 0; i < s_count; i++) {
        healthy[i] = s_rt[i].connected && s_rt[i].ready && s_rt[i].current_notify != NULL;
        if (s_rt[i].connected) {
            live++;
        }
    }
    bool changed;
    taskENTER_CRITICAL(&s_pick_mux);
    changed = costean_split_update(healthy, now_ms(), eff);
    for (int i = 0; i < s_count; i++) {
        s_rt[i].effective_percent = eff[i];
    }
    taskEXIT_CRITICAL(&s_pick_mux);
    if (changed) {
        ESP_LOGI(TAG, "Effective split: %u/%u/%u/%u %%", eff[0], eff[1], eff[2], eff[3]);
    }
    if (s_gs) {
        s_gs->SYSTEM_MODULE.pools_unavailable = (live == 0);
    }
}

int pool_split_take_job_slot(void)
{
    taskENTER_CRITICAL(&s_pick_mux);
    int slot = costean_split_next();   /* smooth weighted round robin over the live split */
    if (slot >= 0 && slot < s_count) {
        CosteanPoolRuntime *rt = &s_rt[slot];
        if (rt->ready && rt->connected && rt->current_notify != NULL) {
            rt->jobs_sent++;
        } else {
            slot = -1;   /* went down since the last health update: the next refresh moves its share */
        }
    } else {
        slot = -1;
    }
    taskEXIT_CRITICAL(&s_pick_mux);
    return slot;
}

void pool_split_on_accepted(int slot, double pool_diff)
{
    taskENTER_CRITICAL(&s_pick_mux);
    costean_split_on_accepted(slot, pool_diff, now_ms());
    taskEXIT_CRITICAL(&s_pick_mux);
}

void pool_split_stats(int slot, pool_split_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    if (slot < 0 || slot >= s_count) {
        return;
    }
    taskENTER_CRITICAL(&s_pick_mux);
    out->weight = costean_split_weight(slot);
    out->effective = s_rt[slot].effective_percent;
    out->healthy = costean_split_healthy(slot);
    out->backup = s_rt[slot].configured_percent == 0;
    out->job_share = costean_split_job_share(slot);
    out->credited_ghs = costean_split_credited_ghs(slot, now_ms());
    taskEXIT_CRITICAL(&s_pick_mux);
}

bool pool_split_failover_active(void)
{
    if (!s_active) {
        return false;
    }
    taskENTER_CRITICAL(&s_pick_mux);
    bool f = costean_split_failover();
    taskEXIT_CRITICAL(&s_pick_mux);
    return f;
}

/* Weight changes from the web UI apply live when the same pools take part; adding or
 * removing a pool needs a restart (each pool has its own connection task). */
static bool s_restart_needed;
bool pool_split_restart_needed(void) { return s_restart_needed; }

static void reload_weights(GlobalState *gs)
{
    bool differs = false;
    for (int i = 0; i < s_count; i++) {
        if (gs->SYSTEM_MODULE.pools[s_rt[i].pool_index].split_percent != s_rt[i].configured_percent) {
            differs = true;
        }
    }
    if (!differs) {
        return;
    }
    pool_split_config_t cfg;
    bool enabled = false;
    pool_split_snapshot_config(gs, &cfg, &enabled);
    bool same_set = pool_split_validate(&cfg, enabled) == POOL_SPLIT_OK;
    int n = 0;
    for (int i = 0; i < COSTEAN_MAX_SPLIT_POOLS && same_set; i++) {
        bool member = cfg.percent[i] > 0 || (cfg.has_url[i] && !cfg.is_sv2[i]);
        if (!member) {
            continue;
        }
        if (n >= s_count || s_rt[n].pool_index != i) {
            same_set = false;
        }
        n++;
    }
    if (!same_set || n != s_count) {
        if (!s_restart_needed) {
            ESP_LOGW(TAG, "Pool split changed: restart to apply the new pool set");
        }
        s_restart_needed = true;
        return;
    }
    uint8_t weights[COSTEAN_MAX_SPLIT_POOLS] = {0};
    for (int i = 0; i < s_count; i++) {
        s_rt[i].configured_percent = cfg.percent[s_rt[i].pool_index];
        weights[i] = (uint8_t) s_rt[i].configured_percent;
    }
    taskENTER_CRITICAL(&s_pick_mux);
    costean_split_setup(weights, s_count);
    taskEXIT_CRITICAL(&s_pick_mux);
    s_restart_needed = false;
    ESP_LOGI(TAG, "Pool split weights updated live: %u/%u/%u/%u %%", weights[0], weights[1], weights[2], weights[3]);
    pool_split_refresh_effective();
}

extern void pool_split_connection_task(void *pvParameters);

void pool_split_manager_task(void *pvParameters)
{
    GlobalState *gs = (GlobalState *)pvParameters;
    if (!s_active) {
        ESP_LOGW(TAG, "Split manager started but split is not active, exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting %d simultaneous V1 pool connections for Costean split", s_count);
    for (int i = 0; i < s_count; i++) {
        char name[20];
        snprintf(name, sizeof(name), "split_p%d", s_rt[i].pool_index);
        if (xTaskCreateWithCaps(pool_split_connection_task, name, 8192, (void *)&s_rt[i], 5, &s_rt[i].task, MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start connection task for pool %d", s_rt[i].pool_index);
        }
    }

    while (1) {
        reload_weights(gs);
        pool_split_refresh_effective();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
