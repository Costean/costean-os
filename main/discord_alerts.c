#include "discord_alerts.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_config.h"
#include "global_state.h"
#include "connect.h"
#include "utils.h"

static const char *TAG = "discord";

#define DISCORD_QUEUE_LEN 8
#define DISCORD_MSG_LEN 400
#define DISCORD_MIN_INTERVAL_MS 2000
#define DISCORD_PAYLOAD_LEN 1024

typedef struct {
    char message[DISCORD_MSG_LEN];
    int priority; /* 2 = block, 1 = alert, 0 = status */
} discord_msg_t;

static QueueHandle_t s_queue;
static GlobalState *s_gs;
static int64_t s_last_send_us;
static bool s_was_overheat;
static bool s_was_offline;
static int64_t s_last_status_us;

static bool webhook_allowed(const char *url)
{
    if (!url || strncmp(url, "https://", 8) != 0) {
        return false;
    }
    return (strstr(url, "discord.com/api/webhooks/") != NULL) ||
           (strstr(url, "discordapp.com/api/webhooks/") != NULL);
}

void discord_webhook_preview(const char *url, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!url || !url[0]) {
        return;
    }
    /* Mask the token after the last slash. */
    const char *last = strrchr(url, '/');
    if (!last || last == url) {
        snprintf(out, out_len, "configured");
        return;
    }
    size_t prefix = (size_t)(last - url + 1);
    if (prefix + 4 >= out_len) {
        snprintf(out, out_len, "https://***/***");
        return;
    }
    memcpy(out, url, prefix);
    out[prefix] = '\0';
    strlcat(out, "****", out_len);
}

bool discord_webhook_is_configured(void)
{
    char *url = nvs_config_get_string(NVS_CONFIG_DISCORD_WEBHOOK);
    bool ok = url && webhook_allowed(url);
    free(url);
    return ok;
}

static bool enqueue(const char *message, int priority)
{
    if (!s_queue || !message) {
        return false;
    }
    discord_msg_t msg = {0};
    strlcpy(msg.message, message, sizeof(msg.message));
    msg.priority = priority;
    if (xQueueSend(s_queue, &msg, 0) == pdTRUE) {
        return true;
    }
    /* If the queue is full, drop a status message to make room for alerts. */
    if (priority > 0) {
        discord_msg_t dropped;
        if (xQueueReceive(s_queue, &dropped, 0) == pdTRUE) {
            return xQueueSend(s_queue, &msg, 0) == pdTRUE;
        }
    }
    ESP_LOGW(TAG, "Discord queue full, dropping message");
    return false;
}

static bool http_post(const char *webhook, const char *json_payload, int *status_out, int *retry_after_ms)
{
    *status_out = 0;
    *retry_after_ms = 0;
    esp_http_client_config_t config = {
        .url = webhook,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));
    esp_err_t err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    *status_out = status;
    if (status == 429) {
        char retry[16] = {0};
        if (esp_http_client_get_header(client, "Retry-After", (char **)&retry) == ESP_OK) {
            /* get_header signature may not fill this way; ignore if it fails */
        }
        *retry_after_ms = 5000;
    }
    esp_http_client_cleanup(client);
    return err == ESP_OK && status >= 200 && status < 300;
}

static void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o + 2 < out_len; i++) {
        char c = in[i];
        if (c == '\\' || c == '"') {
            if (o + 3 >= out_len) break;
            out[o++] = '\\';
            out[o++] = c;
        } else if (c == '\n') {
            if (o + 3 >= out_len) break;
            out[o++] = '\\';
            out[o++] = 'n';
        } else if ((unsigned char)c < 0x20) {
            continue;
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static void discord_task(void *arg)
{
    (void)arg;
    char payload[DISCORD_PAYLOAD_LEN];
    char escaped[DISCORD_MSG_LEN * 2];
    discord_msg_t msg;
    ESP_LOGI(TAG, "Discord alerter started");

    while (1) {
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
            /* Periodic status and state watch are handled below. */
        } else {
            char *url = nvs_config_get_string(NVS_CONFIG_DISCORD_WEBHOOK);
            if (nvs_config_get_bool(NVS_CONFIG_DISCORD_ENABLED) && webhook_allowed(url)) {
                int64_t now = esp_timer_get_time();
                int64_t wait_us = (s_last_send_us + (int64_t)DISCORD_MIN_INTERVAL_MS * 1000) - now;
                if (wait_us > 0) {
                    vTaskDelay(pdMS_TO_TICKS((wait_us / 1000) + 1));
                }
                json_escape(msg.message, escaped, sizeof(escaped));
                snprintf(payload, sizeof(payload), "{\"content\":\"%s\"}", escaped);
                int status = 0, retry_ms = 0;
                bool ok = http_post(url, payload, &status, &retry_ms);
                if (ok) {
                    s_last_send_us = esp_timer_get_time();
                    ESP_LOGI(TAG, "Discord message sent (HTTP %d, priority %d)", status, msg.priority);
                } else if (status == 429) {
                    ESP_LOGW(TAG, "Discord rate limited, backing off");
                    vTaskDelay(pdMS_TO_TICKS(retry_ms > 0 ? retry_ms : 5000));
                    xQueueSendToFront(s_queue, &msg, 0);
                } else {
                    ESP_LOGW(TAG, "Discord post failed (HTTP %d)", status);
                }
            }
            /* Never log the webhook URL. */
            if (url) {
                memset(url, 0, strlen(url));
                free(url);
            }
        }

        if (!s_gs || !nvs_config_get_bool(NVS_CONFIG_DISCORD_ENABLED)) {
            continue;
        }

        bool offline = !wifi_is_connected() || s_gs->SYSTEM_MODULE.pools_unavailable;
        if (nvs_config_get_bool(NVS_CONFIG_DISCORD_OFFLINE)) {
            if (offline && !s_was_offline) {
                enqueue("Costean alert: miner is offline (Wi-Fi or all pools unreachable).", 1);
            } else if (!offline && s_was_offline) {
                enqueue("Costean alert: miner is back online.", 1);
            }
            s_was_offline = offline;
        }

        bool overheat = s_gs->SYSTEM_MODULE.overheat_mode;
        if (nvs_config_get_bool(NVS_CONFIG_DISCORD_OVERHEAT)) {
            if (overheat && !s_was_overheat) {
                enqueue("Costean alert: OVERHEAT mode is on. Frequency, voltage and fan were forced to a safe state.", 1);
            } else if (!overheat && s_was_overheat) {
                enqueue("Costean alert: overheat mode has cleared.", 1);
            }
            s_was_overheat = overheat;
        }

        uint16_t every = nvs_config_get_u16(NVS_CONFIG_DISCORD_STATUS_MINUTES);
        if (every > 0) {
            int64_t now = esp_timer_get_time();
            if (s_last_status_us == 0) {
                s_last_status_us = now;
            } else if ((now - s_last_status_us) / 1000000LL >= (int64_t)every * 60) {
                char best[DIFF_STRING_SIZE];
                char status[DISCORD_MSG_LEN];
                suffixString(s_gs->SYSTEM_MODULE.best_nonce_diff, best, sizeof(best), 0);
                snprintf(status, sizeof(status),
                         "Costean status: %.0f GH/s, temp %.1f C, best %s, shares %llu/%llu, uptime %llu s",
                         s_gs->SYSTEM_MODULE.current_hashrate,
                         s_gs->POWER_MANAGEMENT_MODULE.chip_temp_avg,
                         best,
                         (unsigned long long)s_gs->SYSTEM_MODULE.shares_accepted,
                         (unsigned long long)s_gs->SYSTEM_MODULE.shares_rejected,
                         (unsigned long long)s_gs->SYSTEM_MODULE.uptime_seconds);
                enqueue(status, 0);
                s_last_status_us = now;
            }
        }
    }
}

void discord_alerts_start(GlobalState *gs)
{
    s_gs = gs;
    if (!s_queue) {
        s_queue = xQueueCreate(DISCORD_QUEUE_LEN, sizeof(discord_msg_t));
    }
    if (xTaskCreateWithCaps(discord_task, "discord", 8192, NULL, 4, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start Discord task");
    }
}

void discord_alerts_reload(void)
{
    /* Config is read from NVS on each send. */
}

void discord_alerts_notify_block_found(double diff, double network_diff)
{
    if (!nvs_config_get_bool(NVS_CONFIG_DISCORD_ENABLED) || !nvs_config_get_bool(NVS_CONFIG_DISCORD_BLOCK_FOUND)) {
        return;
    }
    char d[DIFF_STRING_SIZE], n[DIFF_STRING_SIZE], buf[192];
    suffixString((uint64_t)diff, d, sizeof(d), 0);
    suffixString((uint64_t)network_diff, n, sizeof(n), 0);
    snprintf(buf, sizeof(buf), "Costean: BLOCK FOUND! Diff %s (network %s)", d, n);
    enqueue(buf, 2);
}

void discord_alerts_notify_best_diff(double diff, double network_diff)
{
    if (!nvs_config_get_bool(NVS_CONFIG_DISCORD_ENABLED) || !nvs_config_get_bool(NVS_CONFIG_DISCORD_BEST_DIFF)) {
        return;
    }
    char d[DIFF_STRING_SIZE], n[DIFF_STRING_SIZE], buf[192];
    suffixString((uint64_t)diff, d, sizeof(d), 0);
    suffixString((uint64_t)network_diff, n, sizeof(n), 0);
    snprintf(buf, sizeof(buf), "Costean: new best difficulty %s (network %s)", d, n);
    enqueue(buf, 1);
}

bool discord_alerts_send_test(void)
{
    if (!discord_webhook_is_configured()) {
        return false;
    }
    const char *host = "bitaxe";
    char *hn = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    char buf[192];
    snprintf(buf, sizeof(buf), "Costean test message from %s. If you can read this, Discord alerts are working.",
             hn && hn[0] ? hn : host);
    free(hn);
    return enqueue(buf, 1);
}
