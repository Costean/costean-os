#include "pool_split.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "freertos/task.h"

#include "global_state.h"
#include "system.h"
#include "connect.h"
#include "stratum_api.h"
#include "utils.h"
#include "coinbase_decoder.h"

#define MAX_RETRY_ATTEMPTS 5
#define TRANSPORT_TIMEOUT_MS 5000
#define MAX_ACTIVE_JOB_IDS 16
#define MAX_EXTRANONCE_2_LEN 32

static const char *TAG = "stratum_split";

static int next_uid(CosteanPoolRuntime *rt)
{
    taskENTER_CRITICAL(&rt->mux);
    int uid = rt->send_uid++;
    taskEXIT_CRITICAL(&rt->mux);
    return uid;
}

static void close_pool_transport(CosteanPoolRuntime *rt)
{
    taskENTER_CRITICAL(&rt->mux);
    esp_transport_handle_t t = rt->transport;
    rt->transport = NULL;
    rt->connected = false;
    rt->ready = false;
    taskEXIT_CRITICAL(&rt->mux);
    if (t) {
        esp_transport_close(t);
        esp_transport_destroy(t);
    }
    STRATUM_V1_recv_buf_reset(&rt->recv);
    pool_split_refresh_effective();
}

static void store_notify(CosteanPoolRuntime *rt, mining_notify *notify)
{
    if (xSemaphoreTake(rt->lock, portMAX_DELAY) != pdTRUE) {
        STRATUM_V1_free_mining_notify(notify);
        return;
    }
    if (rt->current_notify) {
        STRATUM_V1_free_mining_notify(rt->current_notify);
    }
    rt->current_notify = notify;
    rt->ready = (rt->connected && rt->extranonce_str && rt->current_notify);
    xSemaphoreGive(rt->lock);
}

static void decode_notify(GlobalState *gs, CosteanPoolRuntime *rt, const mining_notify *notify)
{
    if (!gs || !notify) {
        return;
    }
    mining_notification_result_t *result = calloc(1, sizeof(*result));
    if (!result) {
        return;
    }
    const char *user = gs->SYSTEM_MODULE.pools[rt->pool_index].user;
    bool decode = gs->SYSTEM_MODULE.pools[rt->pool_index].decode_coinbase_tx;
    if (coinbase_process_notification(notify, rt->extranonce_str, rt->extranonce_2_len, user, decode, result) == ESP_OK) {
        gs->network_nonce_diff = (uint64_t)result->network_difficulty;
        suffixString(result->network_difficulty, gs->network_diff_string, DIFF_STRING_SIZE, 0);
        if (result->block_height != gs->block_height) {
            gs->block_height = result->block_height;
        }
        if (result->scriptsig) {
            strncpy(gs->scriptsig, result->scriptsig, sizeof(gs->scriptsig) - 1);
            gs->scriptsig[sizeof(gs->scriptsig) - 1] = '\0';
            free(result->scriptsig);
        }
        gs->coinbase_value_total_satoshis = result->total_value_satoshis;
        gs->coinbase_value_user_satoshis = result->user_value_satoshis;
    }
    free(result);
}

static void set_extranonce(CosteanPoolRuntime *rt, char *en, int en2_len)
{
    if (en2_len > MAX_EXTRANONCE_2_LEN) {
        en2_len = MAX_EXTRANONCE_2_LEN;
    }
    if (xSemaphoreTake(rt->lock, portMAX_DELAY) == pdTRUE) {
        free(rt->extranonce_str);
        rt->extranonce_str = en;
        rt->extranonce_2_len = en2_len;
        rt->extranonce_2 = 0;
        rt->ready = (rt->connected && rt->extranonce_str && rt->current_notify);
        xSemaphoreGive(rt->lock);
    } else {
        free(en);
    }
}

void pool_split_connection_task(void *pvParameters)
{
    CosteanPoolRuntime *rt = (CosteanPoolRuntime *)pvParameters;
    GlobalState *gs = pool_split_global();
    if (!gs) {
        ESP_LOGE(TAG, "GlobalState not set for split connection task");
        vTaskDelete(NULL);
        return;
    }

    PoolConfig *pool = &gs->SYSTEM_MODULE.pools[rt->pool_index];
    ESP_LOGI(TAG, "Split V1 connection starting for pool %d (%s:%u, %u%%)",
             rt->pool_index, pool->url ? pool->url : "?", pool->port, rt->configured_percent);

    int retry_attempts = 0;
    char *active_job_ids[MAX_ACTIVE_JOB_IDS] = {0};
    int active_job_ids_count = 0;

    while (!rt->should_stop) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!gs->ASIC_initalized) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (retry_attempts >= MAX_RETRY_ATTEMPTS) {
            ESP_LOGW(TAG, "Pool %d reached max retries, backing off", rt->pool_index);
            close_pool_transport(rt);
            retry_attempts = 0;
            vTaskDelay(pdMS_TO_TICKS(15000));
            continue;
        }

        pool = &gs->SYSTEM_MODULE.pools[rt->pool_index];
        if (!pool->url || !pool->url[0]) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        close_pool_transport(rt);
        esp_transport_handle_t transport = STRATUM_V1_transport_init(pool->tls, pool->cert);
        if (!transport) {
            retry_attempts++;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Connecting pool %d to %s:%u", rt->pool_index, pool->url, pool->port);
        if (esp_transport_connect(transport, pool->url, pool->port, TRANSPORT_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGW(TAG, "Pool %d connect failed", rt->pool_index);
            esp_transport_close(transport);
            esp_transport_destroy(transport);
            retry_attempts++;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        taskENTER_CRITICAL(&rt->mux);
        rt->transport = transport;
        rt->connected = true;
        rt->send_uid = 1;
        taskEXIT_CRITICAL(&rt->mux);
        pool_split_refresh_effective();

        STRATUM_V1_subscribe(transport, next_uid(rt), gs->DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_configure_version_rolling(transport, next_uid(rt), &rt->version_mask);
        int authorize_message_id = next_uid(rt);
        STRATUM_V1_authorize(transport, authorize_message_id, pool->user, pool->pass ? pool->pass : "x");

        StratumApiV1Message msg;
        memset(&msg, 0, sizeof(msg));

        while (!rt->should_stop) {
            char *line = STRATUM_V1_receive_jsonrpc_line_buf(transport, &rt->recv);
            if (!line) {
                retry_attempts++;
                break;
            }
            int64_t receive_time_us = esp_timer_get_time();
            bool reconnect = false;
            if (!STRATUM_V1_parse(&msg, line)) {
                free(line);
                STRATUM_V1_reset_message(&msg);
                continue;
            }
            free(line);

            switch (msg.method) {
                case MINING_NOTIFY: {
                    mining_notify *notify = msg.mining_notification;
                    msg.mining_notification = NULL;
                    if (notify && notify->job_id) {
                        rt->work_received++;
                        gs->SYSTEM_MODULE.work_received++;
                        SYSTEM_notify_new_ntime(gs, notify->ntime);
                        decode_notify(gs, rt, notify);
                        store_notify(rt, notify);
                    } else if (notify) {
                        STRATUM_V1_free_mining_notify(notify);
                    }
                    retry_attempts = 0;
                    break;
                }
                case MINING_SET_DIFFICULTY:
                    rt->difficulty = msg.new_difficulty;
                    ESP_LOGI(TAG, "Pool %d difficulty %.2f", rt->pool_index, rt->difficulty);
                    break;
                case MINING_SET_VERSION_MASK:
                case STRATUM_RESULT_CONFIGURE:
                    if (msg.version_mask) {
                        rt->version_mask = msg.version_mask;
                    }
                    break;
                case MINING_SET_EXTRANONCE:
                case STRATUM_RESULT_SUBSCRIBE:
                    if (msg.extranonce_str) {
                        char *en = msg.extranonce_str;
                        msg.extranonce_str = NULL;
                        set_extranonce(rt, en, msg.extranonce_2_len);
                    }
                    break;
                case MINING_PING:
                    STRATUM_V1_pong(transport, msg.message_id);
                    break;
                case CLIENT_RECONNECT:
                    reconnect = true;
                    break;
                case CLIENT_GET_VERSION:
                    STRATUM_V1_send_version(transport, msg.message_id);
                    break;
                case STRATUM_RESULT: {
                    float response_time_ms = STRATUM_V1_get_response_time_ms(msg.message_id, receive_time_us);
                    if (response_time_ms >= 0) {
                        if (msg.response_success) {
                            rt->shares_accepted++;
                            pool_split_on_accepted(rt->slot, rt->difficulty);
                            SYSTEM_notify_accepted_share(gs);
                        } else {
                            rt->shares_rejected++;
                            SYSTEM_notify_rejected_share(gs, msg.error_str);
                        }
                    } else if (msg.message_id == authorize_message_id && msg.response_success) {
                        if (pool->difficulty > 0) {
                            STRATUM_V1_suggest_difficulty(transport, next_uid(rt), pool->difficulty);
                        }
                        if (pool->extranonce_subscribe) {
                            STRATUM_V1_extranonce_subscribe(transport, next_uid(rt));
                        }
                        retry_attempts = 0;
                    }
                    break;
                }
                default:
                    break;
            }
            STRATUM_V1_reset_message(&msg);
            if (reconnect) {
                break;
            }
        }
        close_pool_transport(rt);
        for (int i = 0; i < active_job_ids_count; i++) {
            free(active_job_ids[i]);
            active_job_ids[i] = NULL;
        }
        active_job_ids_count = 0;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    close_pool_transport(rt);
    vTaskDelete(NULL);
}
