#ifndef CONNECT_H_
#define CONNECT_H_

#include "lwip/sys.h"
#include <arpa/inet.h>
#include <lwip/netdb.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "wifi_fail.h"

typedef struct GlobalState GlobalState;

// Structure to hold WiFi scan results
typedef struct {
    char ssid[33];  // 32 chars + null terminator
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_simple_t;

void toggle_wifi_softap(void);
void wifi_init(GlobalState * GLOBAL_STATE);
esp_err_t wifi_apply_hostname(const char *hostname);
esp_err_t wifi_scan(wifi_ap_record_simple_t *ap_records, uint16_t *ap_count);
uint16_t wifi_scan_max_records(void);  // capacity wifi_scan() may fill
esp_err_t get_wifi_current_rssi(int8_t *rssi);
bool wifi_is_connected(void);
typedef enum { WIFI_LINK_IDLE = 0, WIFI_LINK_JOINING, WIFI_LINK_DHCP, WIFI_LINK_UP } wifi_link_t;
wifi_link_t wifi_link_state(void);          // joining / waiting for an IP / connected
int wifi_last_disconnect_reason(void);      // raw reason of the last failed join (0 = none since connecting)
wifi_fail_t wifi_last_fail(void);           // the same, classified (wrong password, not found, ...)
int wifi_retry_count(void);                 // failed joins since the last success
esp_err_t update_mdns_hostname(const char *new_hostname, GlobalState *GLOBAL_STATE);

#endif /* CONNECT_H_ */
