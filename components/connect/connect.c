#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/lwip_napt.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi_types_generic.h"

#include "connect.h"
#include "global_state.h"
#include "nvs_config.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "wifi_fail.h"

// Maximum number of access points to scan
#define MAX_AP_COUNT 32

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""

#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID

#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

static const char * TAG = "connect";

static TimerHandle_t ip_acquire_timer = NULL;

static bool is_scanning = false;
static uint16_t ap_number = 0;
static wifi_ap_record_t ap_info[MAX_AP_COUNT];
static int s_retry_num = 0;
static int clients_connected_to_ap = 0;
// Costean OS: true once a station SSID is saved. With no SSID (fresh flash) the STA side only scans, it never
// tries to join, so setup scans are not interrupted by connect/retry cycles.
static bool sta_configured = false;
static bool mdns_initialized = false;
static bool mdns_init_in_progress = false;

// Costean OS (costean.6): connection state machine helpers. Nothing in the Wi-Fi event handler blocks or aborts any
// more; delayed work (join retries, turning the setup hotspot off after the IP arrives) runs in the small
// "wifi ctl" task below. A failed esp_wifi_* call is logged and retried, never turned into a reboot.
static volatile wifi_link_t s_link = WIFI_LINK_IDLE;
static volatile int s_last_reason = 0;            // last disconnect reason (0 = none since the last success)
static volatile wifi_fail_t s_last_fail = WIFI_FAIL_NONE;
static volatile bool s_ip_timed_out = false;      // our own disconnect after the DHCP timeout
static volatile int64_t s_attempt_us = 0;         // last esp_wifi_connect() call
static volatile int64_t s_retry_at_us = 0;        // pending join retry (0 = none)
static volatile int64_t s_ap_off_at_us = 0;       // pending hotspot shutdown (0 = none)
static volatile int s_ap_off_tries = 0;
static TaskHandle_t s_ctl_task = NULL;
#define JOIN_IN_FLIGHT_US   (12 * 1000000LL)      // a join attempt normally ends (connected or failed) well within this
#define AP_OFF_DELAY_US     (4 * 1000000LL)       // keep the hotspot a few seconds after the IP arrives

static const char *get_wifi_reason_string(int reason);
static void wifi_softap_on(void);
static void wifi_softap_off(void);
static void sta_connect_now(void);
static void wifi_schedule_retry(int delay_ms);

esp_err_t wifi_apply_hostname(const char *hostname)
{
    if (hostname == NULL || hostname[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *esp_netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (esp_netif_sta == NULL) {
        ESP_LOGW(TAG, "STA netif not ready; hostname will apply on next Wi-Fi start");
        return ESP_ERR_ESP_NETIF_IF_NOT_READY;
    }

    esp_err_t err = esp_netif_set_hostname(esp_netif_sta, hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Set Wi-Fi hostname to: %s", hostname);

    // Bounce the DHCP client so the new hostname is sent in option 12 on the
    // next DISCOVER/REQUEST. This keeps the Wi-Fi link up — no AP flap.
    err = esp_netif_dhcpc_stop(esp_netif_sta);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "esp_netif_dhcpc_stop failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_dhcpc_start(esp_netif_sta);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(TAG, "esp_netif_dhcpc_start failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static void initialize_mdns_if_needed(GlobalState *GLOBAL_STATE);
static char* generate_unique_hostname(const char *base);
static char* check_and_resolve_hostname_conflict(const char *hostname, const char *current_ip);

static void mdns_init_task(void *pvParameters) {
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    initialize_mdns_if_needed(GLOBAL_STATE);
    mdns_init_in_progress = false;
    vTaskDelete(NULL);
}

static void spawn_mdns_init_if_needed(GlobalState *GLOBAL_STATE) {
    if (mdns_initialized || mdns_init_in_progress) {
        return;
    }
    mdns_init_in_progress = true;
    BaseType_t ret = xTaskCreate(mdns_init_task, "mdns_init", 4096, GLOBAL_STATE, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create mDNS init task");
        mdns_init_in_progress = false;
    }
}

static void initialize_mdns_if_needed(GlobalState *GLOBAL_STATE) {
    if (mdns_initialized) {
        return;
    }

    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    if (hostname == NULL) {
        ESP_LOGW(TAG, "Hostname not configured, skipping mDNS setup");
        return;
    }
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS/Avahi initialization failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Device will not be discoverable via mDNS/Bonjour/Avahi");
    } else {
        ESP_LOGI(TAG, "mDNS/Avahi initialized successfully - device discoverable on network");

        /* Get current IP */
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif == NULL) {
            netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        }
        char current_ip[16];
        if (netif == NULL || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
            ESP_LOGW(TAG, "No active network interface for mDNS init, using 0.0.0.0");
            strlcpy(current_ip, "0.0.0.0", sizeof(current_ip));
        } else {
            snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));
        }

        /* Check for hostname conflicts */
        char *final_hostname = check_and_resolve_hostname_conflict(hostname, current_ip);
        if (final_hostname == NULL) {
            ESP_LOGE(TAG, "Failed to resolve hostname conflicts, skipping mDNS hostname setup");
            free(hostname);
            return;
        }

        /* If conflict resolution changed the hostname, persist to NVS */
        if (strcmp(final_hostname, hostname) != 0) {
            nvs_config_set_string(NVS_CONFIG_HOSTNAME, final_hostname);
            ESP_LOGI(TAG, "Hostname conflict resolved, updated NVS to: %s", final_hostname);
        }

        /* Set mDNS hostname */
        err = mdns_hostname_set(final_hostname);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS hostname setup failed: %s", esp_err_to_name(err));
            ESP_LOGW(TAG, "Device hostname not set for mDNS discovery");
        } else {
            ESP_LOGI(TAG, "mDNS hostname set to: %s.local", final_hostname);
            ESP_LOGI(TAG, "Access device at: http://%s.local", final_hostname);
            strlcpy(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname, final_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname));
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.full_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.full_hostname), "%s.local", final_hostname);
        }

        free(final_hostname);

        /* Set mDNS instance name */
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        char mac_suffix[6];
        snprintf(mac_suffix, sizeof(mac_suffix), "%02X%02X", mac[4], mac[5]);

        char instance_name[64];
        snprintf(instance_name, sizeof(instance_name), "Bitaxe %s %s (%s)",
                 GLOBAL_STATE->DEVICE_CONFIG.family.name,
                 GLOBAL_STATE->DEVICE_CONFIG.board_version,
                 mac_suffix);
        
        /* Add HTTP service */
        err = mdns_service_add(instance_name, "_http", "_tcp", 80, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS HTTP service registration failed: %s", esp_err_to_name(err));
            ESP_LOGW(TAG, "HTTP service not advertised via mDNS");
        } else {
            ESP_LOGI(TAG, "mDNS HTTP service registered: _http._tcp port 80");
            ESP_LOGI(TAG, "Discover with: avahi-browse _http._tcp");
            ESP_LOGI(TAG, "mDNS instance: %s", instance_name);

            /* Add AxeOS subtype for DNS-SD discovery */
            err = mdns_service_subtype_add_for_host(instance_name, "_http", "_tcp", NULL, "_axeos");
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "mDNS AxeOS subtype registration failed: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "mDNS AxeOS subtype registered: _axeos._sub._http._tcp");
                ESP_LOGI(TAG, "Discover AxeOS devices with: avahi-browse _axeos._sub._http._tcp");
            }

            /* Add TXT records for device identification */
            err = mdns_service_txt_item_set_for_host(instance_name, "_http", "_tcp", NULL, "board", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            if (err != ESP_OK) ESP_LOGW(TAG, "mDNS TXT 'board' failed: %s", esp_err_to_name(err));
            err = mdns_service_txt_item_set_for_host(instance_name, "_http", "_tcp", NULL, "family", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            if (err != ESP_OK) ESP_LOGW(TAG, "mDNS TXT 'family' failed: %s", esp_err_to_name(err));
            err = mdns_service_txt_item_set_for_host(instance_name, "_http", "_tcp", NULL, "asic", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
            if (err != ESP_OK) ESP_LOGW(TAG, "mDNS TXT 'asic' failed: %s", esp_err_to_name(err));
            char asic_count_str[4];
            snprintf(asic_count_str, sizeof(asic_count_str), "%u", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count);
            err = mdns_service_txt_item_set_for_host(instance_name, "_http", "_tcp", NULL, "asic_count", asic_count_str);
            if (err != ESP_OK) ESP_LOGW(TAG, "mDNS TXT 'asic_count' failed: %s", esp_err_to_name(err));
            const esp_app_desc_t *app_desc = esp_app_get_description();
            err = mdns_service_txt_item_set_for_host(instance_name, "_http", "_tcp", NULL, "fw_version", app_desc->version);
            if (err != ESP_OK) ESP_LOGW(TAG, "mDNS TXT 'fw_version' failed: %s", esp_err_to_name(err));
            ESP_LOGI(TAG, "mDNS TXT records added: board=%s, family=%s, asic=%s, asic_count=%s, fw_version=%s",
                     GLOBAL_STATE->DEVICE_CONFIG.board_version,
                     GLOBAL_STATE->DEVICE_CONFIG.family.name,
                     GLOBAL_STATE->DEVICE_CONFIG.family.asic.name,
                     asic_count_str,
                     app_desc->version);
        }

        ESP_LOGI(TAG, "mDNS/Avahi setup complete - device ready for network discovery");
        mdns_initialized = true;
    }
    free(hostname);
}

esp_err_t update_mdns_hostname(const char *new_hostname, GlobalState *GLOBAL_STATE) {
    if (new_hostname == NULL || strlen(new_hostname) == 0) {
        ESP_LOGW(TAG, "Invalid hostname provided for mDNS update");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get current IP for conflict checking */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }
    if (netif == NULL || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "No active network interface for mDNS hostname update");
        ip_info.ip.addr = 0;
    }
    char current_ip[16];
    if (ip_info.ip.addr == 0) {
        strlcpy(current_ip, "0.0.0.0", sizeof(current_ip));
    } else {
        snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));
    }

    /* Check for hostname conflicts and resolve if needed */
    char *resolved_hostname = check_and_resolve_hostname_conflict(new_hostname, current_ip);
    if (resolved_hostname == NULL) {
        ESP_LOGE(TAG, "Failed to resolve hostname conflicts");
        return ESP_ERR_NO_MEM;
    }

    /* If the hostname was resolved to a different one, update NVS */
    if (strcmp(resolved_hostname, new_hostname) != 0) {
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, resolved_hostname);
        ESP_LOGI(TAG, "Hostname conflict resolved, updated NVS to: %s", resolved_hostname);
    }

    esp_err_t err = mdns_hostname_set(resolved_hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update mDNS hostname to: %s, error: %s", resolved_hostname, esp_err_to_name(err));
        free(resolved_hostname);
        return err;
    }

    ESP_LOGI(TAG, "mDNS hostname updated to: %s", resolved_hostname);
    if (GLOBAL_STATE != NULL) {
        strlcpy(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname, resolved_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname));
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.full_hostname, sizeof(GLOBAL_STATE->SYSTEM_MODULE.full_hostname), "%s.local", resolved_hostname);
    }
    free(resolved_hostname);
    return ESP_OK;
}

esp_err_t get_wifi_current_rssi(int8_t *rssi)
{
    wifi_ap_record_t current_ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&current_ap_info);

    if (err == ESP_OK) {
        *rssi = current_ap_info.rssi;
        return ERR_OK;
    }

    return err;
}

// Function to scan for available WiFi networks.
// Costean OS: runs while the setup hotspot stays up (APSTA). Short active dwell per channel and regular trips back to
// the hotspot channel keep a phone that is connected to the hotspot from dropping off during the scan.
esp_err_t wifi_scan(wifi_ap_record_simple_t *ap_records, uint16_t *ap_count)
{
    *ap_count = 0;
    if (is_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // costean.6: never scan while the miner is getting its IP address (off-channel trips make DHCP time out), and
    // never cut a join attempt that is still in flight. The web page keeps its last list meanwhile.
    if (sta_configured && s_link == WIFI_LINK_DHCP) {
        ESP_LOGI(TAG, "Not scanning: waiting for an IP address");
        return ESP_ERR_INVALID_STATE;
    }
    int64_t now_us = esp_timer_get_time();
    bool in_flight = sta_configured && s_link == WIFI_LINK_JOINING && s_retry_at_us == 0 && s_attempt_us != 0 &&
                     now_us - s_attempt_us < JOIN_IN_FLIGHT_US;
    if (in_flight) {
        ESP_LOGI(TAG, "Not scanning: a Wi-Fi join attempt is in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi scan!");
    is_scanning = true;

    wifi_ap_record_t current_ap_info;
    bool sta_linked = esp_wifi_sta_get_ap_info(&current_ap_info) == ESP_OK;
    if (sta_configured && !sta_linked && s_retry_at_us == 0) {
        // A join attempt that never finished blocks the scan; stop it, the join is resumed after the scan.
        ESP_LOGI(TAG, "Pausing Wi-Fi join so that we can scan");
        esp_wifi_disconnect();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 60, .max = 120 } },
        .home_chan_dwell_time = 60,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err == ESP_ERR_WIFI_STATE) {
        // Station still busy connecting: give it a moment and try once more.
        esp_wifi_disconnect();
        vTaskDelay(500 / portTICK_PERIOD_MS);
        err = esp_wifi_scan_start(&scan_config, false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan start failed with error: %s", esp_err_to_name(err));
        is_scanning = false;
        if (sta_configured && !sta_linked) wifi_schedule_retry(1000);
        return err;
    }

    // 13 channels x (120 ms + 60 ms home dwell) is about 2.5 s; allow 8 s before giving up.
    int waited_ms = 0;
    while (is_scanning && waited_ms < 8000) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        waited_ms += 100;
    }
    if (is_scanning) {
        ESP_LOGW(TAG, "Wi-Fi scan timed out");
        esp_wifi_scan_stop();
        is_scanning = false;
        if (sta_configured && !sta_linked) wifi_schedule_retry(1000);
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Wi-Fi networks found: %d (%d ms)", ap_number, waited_ms);
    uint16_t n = ap_number > MAX_AP_COUNT ? MAX_AP_COUNT : ap_number;
    *ap_count = n;
    memset(ap_records, 0, n * sizeof(wifi_ap_record_simple_t));
    for (int i = 0; i < n; i++) {
        memcpy(ap_records[i].ssid, ap_info[i].ssid, sizeof(ap_records[i].ssid));
        ap_records[i].ssid[sizeof(ap_records[i].ssid) - 1] = '\0';
        ap_records[i].rssi = ap_info[i].rssi;
        ap_records[i].authmode = ap_info[i].authmode;
    }

    // Disconnect events were ignored while scanning, so resume the join here.
    if (sta_configured && !sta_linked && !wifi_is_connected()) {
        ESP_LOGI(TAG, "Resuming Wi-Fi join after scan");
        wifi_schedule_retry(500);
    }

    ESP_LOGD(TAG, "Finished Wi-Fi scan!");
    return ESP_OK;
}

uint16_t wifi_scan_max_records(void)
{
    return MAX_AP_COUNT;
}

static void ip_timeout_callback(TimerHandle_t xTimer)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvTimerGetTimerID(xTimer);
    if (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        ESP_LOGI(TAG, "Timeout waiting for IP address. Disconnecting...");
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.wifi_status), "%s (IP timeout)", wifi_fail_text(WIFI_FAIL_NO_IP));
        s_ip_timed_out = true;
        esp_wifi_disconnect();   // the disconnect event schedules the retry
    }
}

static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)arg;
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_SCAN_DONE) {
            esp_wifi_scan_get_ap_num(&ap_number);
            ESP_LOGI(TAG, "Wi-Fi Scan Done");
            // Never ask for more records than ap_info can hold (busy areas can report more).
            if (ap_number > MAX_AP_COUNT) ap_number = MAX_AP_COUNT;
            if (esp_wifi_scan_get_ap_records(&ap_number, ap_info) != ESP_OK) {
                ESP_LOGI(TAG, "Failed esp_wifi_scan_get_ap_records");
                ap_number = 0;
            }
            is_scanning = false;
        }

        // Hotspot bookkeeping must run even during a scan.
        if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "Configuration Access Point enabled");
            GLOBAL_STATE->SYSTEM_MODULE.ap_enabled = true;
        }
        if (event_id == WIFI_EVENT_AP_STOP) {
            ESP_LOGI(TAG, "Configuration Access Point disabled");
            GLOBAL_STATE->SYSTEM_MODULE.ap_enabled = false;
        }
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            clients_connected_to_ap += 1;
        }
        if (event_id == WIFI_EVENT_AP_STADISCONNECTED && clients_connected_to_ap > 0) {
            clients_connected_to_ap -= 1;
        }

        if (is_scanning) {
            ESP_LOGI(TAG, "Still scanning, ignore wifi event.");
            return;
        }

        if (event_id == WIFI_EVENT_STA_START && !sta_configured) {
            ESP_LOGI(TAG, "No Wi-Fi saved: setup hotspot only");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Setup: choose a Wi-Fi network");
        }

        if (event_id == WIFI_EVENT_STA_START && sta_configured) {
            ESP_LOGI(TAG, "Connecting...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Connecting...");
            sta_connect_now();
        }

        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "Acquiring IP...");
            strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Acquiring IP...");
            s_link = WIFI_LINK_DHCP;
            s_retry_at_us = 0;

            if (ip_acquire_timer == NULL) {
                ip_acquire_timer = xTimerCreate("ip_acquire_timer", pdMS_TO_TICKS(30000), pdFALSE, (void *)GLOBAL_STATE, ip_timeout_callback);
            }
            if (ip_acquire_timer != NULL) {
                xTimerStart(ip_acquire_timer, 0);
            }            
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            if (!sta_configured) {
                return;
            }
            if (event->reason == WIFI_REASON_ROAMING) {
                ESP_LOGI(TAG, "We are roaming, nothing to do");
                return;
            }
            if (ip_acquire_timer != NULL) {
                xTimerStop(ip_acquire_timer, 0);
            }

            ESP_LOGI(TAG, "Could not connect to '%.*s' [rssi %d]: reason %d", event->ssid_len, event->ssid, event->rssi, event->reason);
            wifi_fail_t fail = s_ip_timed_out ? WIFI_FAIL_NO_IP : wifi_fail_classify(event->reason);
            s_ip_timed_out = false;
            s_last_reason = event->reason;
            s_last_fail = fail;
            s_link = WIFI_LINK_JOINING;
            GLOBAL_STATE->SYSTEM_MODULE.is_connected = false;
            s_ap_off_at_us = 0;
            wifi_softap_on();

            // costean.6: keep retrying even while a phone is on the setup hotspot (stock firmware stopped here and
            // the miner could sit on its hotspot forever), but slower then, so the phone keeps a usable link.
            int delay_ms = clients_connected_to_ap > 0 ? 15000 : (s_retry_num < 3 ? 3000 : 10000);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, sizeof(GLOBAL_STATE->SYSTEM_MODULE.wifi_status), "%s (Error %d, retry #%d)",
                     wifi_fail_text(fail), event->reason, s_retry_num + 1);
            ESP_LOGI(TAG, "Wi-Fi status: %s (%s); next try in %d s", GLOBAL_STATE->SYSTEM_MODULE.wifi_status,
                     get_wifi_reason_string(event->reason), delay_ms / 1000);
            s_retry_num++;
            wifi_schedule_retry(delay_ms);
        }
        
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t * event = (ip_event_got_ip_t *) event_data;
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str, IP4ADDR_STRLEN_MAX, IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "IPv4 Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str);
        s_retry_num = 0;

        if (ip_acquire_timer != NULL) {
            xTimerStop(ip_acquire_timer, 0);
        }

        GLOBAL_STATE->SYSTEM_MODULE.is_connected = true;

        ESP_LOGI(TAG, "Connected to SSID: %s", GLOBAL_STATE->SYSTEM_MODULE.ssid);
        strcpy(GLOBAL_STATE->SYSTEM_MODULE.wifi_status, "Connected!");
        s_link = WIFI_LINK_UP;
        s_last_reason = 0;
        s_last_fail = WIFI_FAIL_NONE;
        s_retry_at_us = 0;

        // costean.6: do not switch the radio mode inside the event handler (a scan or a busy driver made that abort
        // and reboot the miner right after "Acquiring IP"). The wifi ctl task turns the hotspot off a moment later.
        s_ap_off_tries = 0;
        s_ap_off_at_us = esp_timer_get_time() + AP_OFF_DELAY_US;
        
        // Create IPv6 link-local address after WiFi connection
        esp_netif_t *netif = event->esp_netif;
        esp_err_t ipv6_err = netif ? esp_netif_create_ip6_linklocal(netif) : ESP_ERR_INVALID_ARG;
        if (ipv6_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create IPv6 link-local address: %s", esp_err_to_name(ipv6_err));
        }

        spawn_mdns_init_if_needed(GLOBAL_STATE);
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t * event = (ip_event_got_ip6_t *) event_data;
        
        // Convert IPv6 address to string
        char ipv6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &event->ip6_info.ip, ipv6_str, sizeof(ipv6_str));
        
        // Check if it's a link-local address (fe80::/10)
        if ((event->ip6_info.ip.addr[0] & 0xFFC0) == 0xFE80) {
            // For link-local addresses, append zone identifier using netif index
            int netif_index = esp_netif_get_netif_impl_index(event->esp_netif);
            if (netif_index >= 0) {
                snprintf(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str,
                        sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str),
                        "%s%%%d", ipv6_str, netif_index);
                ESP_LOGI(TAG, "IPv6 Link-Local Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
            } else {
                strncpy(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str, ipv6_str,
                       sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1);
                GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1] = '\0';
                ESP_LOGW(TAG, "IPv6 Link-Local Address: %s (could not get interface index)", ipv6_str);
            }
        } else {
            // Global or ULA address - no zone identifier needed
            strncpy(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str, ipv6_str,
                   sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1);
            GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str[sizeof(GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str) - 1] = '\0';
            ESP_LOGI(TAG, "IPv6 Address: %s", GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
        }

        spawn_mdns_init_if_needed(GLOBAL_STATE);
    }
}

esp_netif_t * wifi_init_softap(GlobalState * GLOBAL_STATE)
{
    esp_netif_t * esp_netif_ap = esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    // Format the last 4 bytes of the MAC address as a hexadecimal string
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid, sizeof(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid), "Bitaxe_%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_ap_config = { 0 };
    wifi_ap_config.ap.ssid_len = strlen(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid);
    memcpy(wifi_ap_config.ap.ssid, GLOBAL_STATE->SYSTEM_MODULE.ap_ssid, wifi_ap_config.ap.ssid_len);
    wifi_ap_config.ap.channel = 1;
    wifi_ap_config.ap.max_connection = 10;
    wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_ap_config.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
    if (err != ESP_OK) ESP_LOGE(TAG, "Setup hotspot config failed: %s", esp_err_to_name(err));

    return esp_netif_ap;
}

static bool is_wifi_operation_allowed(esp_err_t err)
{
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_STOP_STATE) {
        ESP_LOGI(TAG, "WiFi not initialized or stopped, skipping operation");
        return false;
    }
    return true;
}

void toggle_wifi_softap(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (is_wifi_operation_allowed(err)) {
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
            return;
        }
        if (mode == WIFI_MODE_APSTA) {
            wifi_softap_off();
        } else {
            wifi_softap_on();
        }
    }
}

static void wifi_softap_off(void)
{
    s_ap_off_at_us = 0;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (is_wifi_operation_allowed(err) && err != ESP_OK) {
        ESP_LOGE(TAG, "Turning the setup hotspot off failed: %s", esp_err_to_name(err));
    }
}



static char* generate_unique_hostname(const char *base) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char suffix[6];
    snprintf(suffix, sizeof(suffix), "-%02x%02x", mac[4], mac[5]);
    char *new_hostname = malloc(strlen(base) + strlen(suffix) + 1);
    if (new_hostname == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for unique hostname");
        return NULL;
    }
    strcpy(new_hostname, base);
    strcat(new_hostname, suffix);
    return new_hostname;
}

static char* check_and_resolve_hostname_conflict(const char *hostname, const char *current_ip) {
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_generic(hostname, NULL, NULL, MDNS_TYPE_A, MDNS_QUERY_MULTICAST, 1000, 1, &results);
    if (err != ESP_OK || !results || !results->addr) {
        // No A record found, no conflict
        if (results) mdns_query_results_free(results);
        return strdup(hostname);
    }

    mdns_ip_addr_t *a = results->addr;
    char ip_str[INET6_ADDRSTRLEN];
    if (a->addr.type == IPADDR_TYPE_V4) {
        esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip_str, sizeof(ip_str));
    } else {
        inet_ntop(AF_INET6, &a->addr.u_addr.ip6, ip_str, sizeof(ip_str));
    }

    if (strcmp(ip_str, current_ip) != 0) {
        char *new_hostname = generate_unique_hostname(hostname);
        if (new_hostname == NULL) {
            ESP_LOGW(TAG, "mDNS conflict detected for '%s' but could not generate unique name, keeping original", hostname);
            mdns_query_results_free(results);
            return strdup(hostname);
        }
        ESP_LOGI(TAG, "mDNS conflict detected for '%s' at %s, renaming to '%s'", hostname, ip_str, new_hostname);
        mdns_query_results_free(results);
        return new_hostname;
    }

    mdns_query_results_free(results);
    return strdup(hostname);
}

static void wifi_softap_on(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (is_wifi_operation_allowed(err) && err != ESP_OK) {
        ESP_LOGE(TAG, "Turning the setup hotspot on failed: %s", esp_err_to_name(err));
    }
}

static void sta_connect_now(void)
{
    s_retry_at_us = 0;
    s_attempt_us = esp_timer_get_time();
    if (s_link != WIFI_LINK_UP) s_link = WIFI_LINK_JOINING;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s, retrying shortly", esp_err_to_name(err));
        s_retry_at_us = esp_timer_get_time() + 2000000LL;
    }
}

static void wifi_schedule_retry(int delay_ms)
{
    if (!sta_configured) return;
    int64_t at = esp_timer_get_time() + (int64_t) delay_ms * 1000;
    if (at == 0) at = 1;
    s_retry_at_us = at;
}

// Delayed Wi-Fi work. Runs outside the event loop, so the event task never sleeps or aborts.
static void wifi_ctl_task(void *arg)
{
    GlobalState *GLOBAL_STATE = (GlobalState *) arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        int64_t now = esp_timer_get_time();

        int64_t retry_at = s_retry_at_us;
        if (retry_at != 0 && now >= retry_at) {
            if (is_scanning) {
                s_retry_at_us = now + 1000000LL;          // after the scan
            } else if (GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
                s_retry_at_us = 0;
            } else {
                ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
                sta_connect_now();
            }
        }

        // Lost-event safety net: a join attempt that produced no event at all for 45 s is retried.
        if (sta_configured && s_link == WIFI_LINK_JOINING && s_retry_at_us == 0 && !is_scanning && s_attempt_us != 0 &&
            now - s_attempt_us > 45000000LL && !GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
            ESP_LOGW(TAG, "Join attempt went quiet, retrying");
            sta_connect_now();
        }

        int64_t off_at = s_ap_off_at_us;
        if (off_at != 0 && now >= off_at) {
            if (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
                s_ap_off_at_us = 0;                        // link dropped again: keep the hotspot
            } else if (is_scanning) {
                s_ap_off_at_us = now + 1000000LL;
            } else {
                esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
                if (err == ESP_OK || !is_wifi_operation_allowed(err)) {
                    ESP_LOGI(TAG, "Setup hotspot off (connected)");
                    s_ap_off_at_us = 0;
                } else if (++s_ap_off_tries < 10) {
                    ESP_LOGW(TAG, "Turning the setup hotspot off failed (%s), trying again", esp_err_to_name(err));
                    s_ap_off_at_us = now + 2000000LL;
                } else {
                    ESP_LOGE(TAG, "Setup hotspot could not be turned off (%s); leaving it on", esp_err_to_name(err));
                    s_ap_off_at_us = 0;
                }
            }
        }
    }
}

wifi_link_t wifi_link_state(void) { return s_link; }
int wifi_last_disconnect_reason(void) { return s_last_reason; }
wifi_fail_t wifi_last_fail(void) { return s_last_fail; }
int wifi_retry_count(void) { return s_retry_num; }

/* Initialize wifi station */
esp_netif_t * wifi_init_sta(const char * wifi_ssid, const char * wifi_pass)
{
    esp_netif_t * esp_netif_sta = esp_netif_create_default_wifi_sta();

    /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
    * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
    * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
    * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
    */
    wifi_auth_mode_t authmode;

    if (strlen(wifi_pass) == 0) {
        ESP_LOGI(TAG, "No Wi-Fi password provided, using open network");
        authmode = WIFI_AUTH_OPEN;
    } else {
        ESP_LOGI(TAG, "Wi-Fi Password provided, using WPA2");
        authmode = WIFI_AUTH_WPA2_PSK;
    }

    // On some new-ish Wi-Fi 7 routers, the Wi-Fi connection would fail, which leaves users in a bit of 
    // a pickle. This will drop down to WPA2, instead of having a connection failure due to a vague 
    // failed WPA3 handshake error.
    wifi_config_t wifi_sta_config = {
        .sta =
            {
                .threshold.authmode = authmode,
                .btm_enabled = 1,
                .rm_enabled = 1,
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .pmf_cfg =
                    {
                        .capable = true,
                        .required = false
                    },
                .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
                .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
                .disable_wpa3_compatible_mode = 1,
        },
    };

    size_t ssid_len = strlen(wifi_ssid);
    if (ssid_len > 32) ssid_len = 32;
    memcpy(wifi_sta_config.sta.ssid, wifi_ssid, ssid_len);
    if (ssid_len < 32) {
        wifi_sta_config.sta.ssid[ssid_len] = '\0';
    }

    if (authmode != WIFI_AUTH_OPEN) {
        strncpy((char *) wifi_sta_config.sta.password, wifi_pass, sizeof(wifi_sta_config.sta.password));
        wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password) - 1] = '\0';
    }
    // strncpy((char *) wifi_sta_config.sta.password, wifi_pass, 63);
    // wifi_sta_config.sta.password[63] = '\0';

    esp_err_t cfg_err = esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
    if (cfg_err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi station config failed: %s", esp_err_to_name(cfg_err));

    // IPv6 link-local address will be created after WiFi connection
    
    // Start DHCP client for IPv4
    esp_netif_dhcpc_start(esp_netif_sta);

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    return esp_netif_sta;
}

void wifi_init(GlobalState * GLOBAL_STATE)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_got_ip6;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, GLOBAL_STATE, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, GLOBAL_STATE, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, GLOBAL_STATE, &instance_got_ip6));

    /* Initialize Wi-Fi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (ip_acquire_timer == NULL) {
        ip_acquire_timer = xTimerCreate("ip_acquire_timer", pdMS_TO_TICKS(30000), pdFALSE, (void *)GLOBAL_STATE, ip_timeout_callback);
    }
    if (s_ctl_task == NULL && xTaskCreate(wifi_ctl_task, "wifi ctl", 3072, GLOBAL_STATE, 5, &s_ctl_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start the wifi ctl task");
        s_ctl_task = NULL;
    }

    wifi_softap_on();

    /* Initialize AP */
    wifi_init_softap(GLOBAL_STATE);

    GLOBAL_STATE->SYSTEM_MODULE.ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);

    /* Skip connection if SSID is null */
    if (strlen(GLOBAL_STATE->SYSTEM_MODULE.ssid) == 0) {
        ESP_LOGI(TAG, "No WiFi SSID provided, skipping connection");
        sta_configured = false;

        /* Start WiFi */
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK) ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));

        /* Disable power savings for best performance */
        esp_wifi_set_ps(WIFI_PS_NONE);

        return;
    } else {

        char * wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);
        sta_configured = true;

        /* Initialize STA */
        ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
        esp_netif_t * esp_netif_sta = wifi_init_sta(GLOBAL_STATE->SYSTEM_MODULE.ssid, wifi_pass);

        free(wifi_pass);

        /* Disable power savings for best performance */
        esp_wifi_set_ps(WIFI_PS_NONE);

        char * hostname  = nvs_config_get_string(NVS_CONFIG_HOSTNAME);

        /* Set Hostname */
        esp_err_t err = esp_netif_set_hostname(esp_netif_sta, hostname);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "ESP_WIFI setting hostname to: %s", hostname);
        }

        free(hostname);

        /* Start Wi-Fi */
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK) ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));

        ESP_LOGI(TAG, "wifi_init_sta finished.");
    }
}

typedef struct {
    int reason;
    const char *description;
} wifi_reason_desc_t;

static const wifi_reason_desc_t wifi_reasons[] = {
    {WIFI_REASON_UNSPECIFIED,                        "Unspecified reason"},
    {WIFI_REASON_AUTH_EXPIRE,                        "Authentication expired"},
    {WIFI_REASON_AUTH_LEAVE,                         "Deauthentication due to leaving"},
    {WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY,         "Disassociated due to inactivity"},
    {WIFI_REASON_ASSOC_TOOMANY,                      "Too many associated stations"},
    {WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA,      "Class 2 frame from non-authenticated STA"},
    {WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA,     "Class 3 frame from non-associated STA"},
    {WIFI_REASON_ASSOC_LEAVE,                        "Deassociated due to leaving"},
    {WIFI_REASON_ASSOC_NOT_AUTHED,                   "Association but not authenticated"},
    {WIFI_REASON_DISASSOC_PWRCAP_BAD,                "Disassociated due to poor power capability"},
    {WIFI_REASON_DISASSOC_SUPCHAN_BAD,               "Disassociated due to unsupported channel"},
    {WIFI_REASON_BSS_TRANSITION_DISASSOC,            "Disassociated due to BSS transition"},
    {WIFI_REASON_IE_INVALID,                         "Invalid Information Element"},
    {WIFI_REASON_MIC_FAILURE,                        "MIC failure detected"},
    {WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,             "Incorrect password entered"}, // 4-way handshake timeout
    {WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT,           "Group key update timeout"},
    {WIFI_REASON_IE_IN_4WAY_DIFFERS,                 "IE differs in 4-way handshake"},
    {WIFI_REASON_GROUP_CIPHER_INVALID,               "Invalid group cipher"},
    {WIFI_REASON_PAIRWISE_CIPHER_INVALID,            "Invalid pairwise cipher"},
    {WIFI_REASON_AKMP_INVALID,                       "Invalid AKMP"},
    {WIFI_REASON_UNSUPP_RSN_IE_VERSION,              "Unsupported RSN IE version"},
    {WIFI_REASON_INVALID_RSN_IE_CAP,                 "Invalid RSN IE capabilities"},
    {WIFI_REASON_802_1X_AUTH_FAILED,                 "802.1X authentication failed"},
    {WIFI_REASON_CIPHER_SUITE_REJECTED,              "Cipher suite rejected"},
    {WIFI_REASON_TDLS_PEER_UNREACHABLE,              "TDLS peer unreachable"},
    {WIFI_REASON_TDLS_UNSPECIFIED,                   "TDLS unspecified error"},
    {WIFI_REASON_SSP_REQUESTED_DISASSOC,             "SSP requested disassociation"},
    {WIFI_REASON_NO_SSP_ROAMING_AGREEMENT,           "No SSP roaming agreement"},
    {WIFI_REASON_BAD_CIPHER_OR_AKM,                  "Bad cipher or AKM"},
    {WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION,       "Not authorized in this location"},
    {WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS,        "Service change precludes TS"},
    {WIFI_REASON_UNSPECIFIED_QOS,                    "Unspecified QoS reason"},
    {WIFI_REASON_NOT_ENOUGH_BANDWIDTH,               "Not enough bandwidth"},
    {WIFI_REASON_MISSING_ACKS,                       "Missing ACKs"},
    {WIFI_REASON_EXCEEDED_TXOP,                      "Exceeded TXOP"},
    {WIFI_REASON_STA_LEAVING,                        "Station leaving"},
    {WIFI_REASON_END_BA,                             "End of Block Ack"},
    {WIFI_REASON_UNKNOWN_BA,                         "Unknown Block Ack"},
    {WIFI_REASON_TIMEOUT,                            "Timeout occured"},
    {WIFI_REASON_PEER_INITIATED,                     "Peer-initiated disassociation"},
    {WIFI_REASON_AP_INITIATED,                       "Access Point-initiated disassociation"},
    {WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT,      "Invalid FT action frame count"},
    {WIFI_REASON_INVALID_PMKID,                      "Invalid PMKID"},
    {WIFI_REASON_INVALID_MDE,                        "Invalid MDE"},
    {WIFI_REASON_INVALID_FTE,                        "Invalid FTE"},
    {WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED, "Transmission link establishment failed"},
    {WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED,        "Alternative channel occupied"},
    {WIFI_REASON_BEACON_TIMEOUT,                     "Beacon timeout"},
    {WIFI_REASON_NO_AP_FOUND,                        "No access point found"},
    {WIFI_REASON_AUTH_FAIL,                          "Authentication failed"},
    {WIFI_REASON_ASSOC_FAIL,                         "Association failed"},
    {WIFI_REASON_HANDSHAKE_TIMEOUT,                  "Handshake timeout"},
    {WIFI_REASON_CONNECTION_FAIL,                    "Connection failed"},
    {WIFI_REASON_AP_TSF_RESET,                       "Access point TSF reset"},
    {WIFI_REASON_ROAMING,                            "Roaming in progress"},
    {WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG,       "Association comeback time too long"},
    {WIFI_REASON_SA_QUERY_TIMEOUT,                   "SA query timeout"},
    {WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY,  "No access point found with compatible security"},
    {WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD,  "No access point found in auth mode threshold"},
    {WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD,      "No access point found in RSSI threshold"},
    {0,                                               NULL},
};

static const char *get_wifi_reason_string(int reason) {
    for (int i = 0; wifi_reasons[i].reason != 0; i++) {
        if (wifi_reasons[i].reason == reason) {
            return wifi_reasons[i].description;
        }
    }
    return "Unknown error";
}

bool wifi_is_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}
