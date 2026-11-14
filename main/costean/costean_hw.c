#include "costean_hw.h"

#include <math.h>

#include "global_state.h"
#include "nvs_config.h"
#include "pool_split.h"

static GlobalState *G;

void costean_hw_set_global(GlobalState *gs) { G = gs; }

void costean_hw_read(costean_hw_t *o)
{
    PowerManagementModule *pm = &G->POWER_MANAGEMENT_MODULE;
    SystemModule *sm = &G->SYSTEM_MODULE;
    o->chip_c = pm->chip_temp2_avg > pm->chip_temp_avg ? pm->chip_temp2_avg : pm->chip_temp_avg;
    if (o->chip_c < 0) o->chip_c = 0;
    o->vr_c = pm->vr_temp > 0 ? pm->vr_temp : 0;
    o->power_w = pm->power > 0 ? pm->power : 0;
    o->error_pct = sm->error_percentage > 0 ? sm->error_percentage : 0;
    o->ghs = sm->current_hashrate > 0 ? sm->current_hashrate : 0;
    o->accepted = sm->shares_accepted;
    o->rejected = sm->shares_rejected;
    o->self_test = G->SELF_TEST_MODULE.is_active;
    o->overheat = sm->overheat_mode || nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
    o->mining = G->ASIC_initalized && !o->overheat && !sm->mining_paused && !sm->hardware_fault && !o->self_test;
    o->failover = pool_split_failover_active() || (!pool_split_is_active() && sm->is_using_fallback);
    o->stock_mhz = G->DEVICE_CONFIG.family.asic.default_frequency_mhz;
    o->stock_mv = G->DEVICE_CONFIG.family.asic.default_voltage_mv;
    o->user_mhz = (int) lroundf(nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY));
    o->user_mv = (int) nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
}

void costean_hw_split_json(cJSON *o)
{
    cJSON *sp = cJSON_AddObjectToObject(o, "split");
    cJSON_AddBoolToObject(sp, "enabled", pool_split_is_enabled());
    cJSON_AddBoolToObject(sp, "active", pool_split_is_active());
    cJSON_AddStringToObject(sp, "error", pool_split_is_enabled() && !pool_split_is_active() ? pool_split_error_string(pool_split_current_error()) : "");
    cJSON_AddBoolToObject(sp, "restartNeeded", pool_split_restart_needed());
    cJSON_AddBoolToObject(sp, "failover", pool_split_failover_active());
    cJSON_AddNumberToObject(o, "maxPools", COSTEAN_MAX_SPLIT_POOLS);
}
