// Costean OS: turn an ESP-IDF Wi-Fi disconnect reason into what the user can act on.
// Header-only and free of ESP-IDF includes so the host tests can use it (numbers from wifi_err_reason_t).
#pragma once
#include <stdbool.h>

typedef enum {
    WIFI_FAIL_NONE = 0,
    WIFI_FAIL_WRONG_PASSWORD,   // 4-way handshake never completed
    WIFI_FAIL_NOT_FOUND,        // SSID not seen (typo, out of range, 5 GHz only, hidden)
    WIFI_FAIL_SECURITY,         // seen, but its security is not one the miner accepts (WPA3-only / enterprise / WEP)
    WIFI_FAIL_AUTH,             // router refused us (MAC filter, too many clients, auth error)
    WIFI_FAIL_WEAK_SIGNAL,      // lost beacons / association timed out
    WIFI_FAIL_NO_IP,            // joined, but the router never handed out an IP address (DHCP)
    WIFI_FAIL_OTHER,
} wifi_fail_t;

static inline wifi_fail_t wifi_fail_classify(int reason)
{
    switch (reason) {
    case 0: return WIFI_FAIL_NONE;
    case 14: case 15: case 16: case 17: case 204: case 2:
        return WIFI_FAIL_WRONG_PASSWORD;           // MIC failure / handshake timeouts / auth expired mid-handshake
    case 201: case 212:
        return WIFI_FAIL_NOT_FOUND;
    case 210: case 211: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 29:
        return WIFI_FAIL_SECURITY;
    case 202: case 5: case 6: case 7: case 9: case 30: case 49:   // 202 = AUTH_FAIL: not proof of a wrong password
        return WIFI_FAIL_AUTH;
    case 200: case 203: case 205: case 4: case 34: case 39: case 208: case 209:
        return WIFI_FAIL_WEAK_SIGNAL;
    default:
        return WIFI_FAIL_OTHER;
    }
}

static inline const char *wifi_fail_text(wifi_fail_t f)
{
    switch (f) {
    case WIFI_FAIL_NONE: return "";
    case WIFI_FAIL_WRONG_PASSWORD: return "Wrong password?";
    case WIFI_FAIL_NOT_FOUND: return "Network not found";
    case WIFI_FAIL_SECURITY: return "Security type not supported";
    case WIFI_FAIL_AUTH: return "Router refused the miner";
    case WIFI_FAIL_WEAK_SIGNAL: return "Weak signal";
    case WIFI_FAIL_NO_IP: return "No IP address from router";
    default: return "Could not connect";
    }
}

// Longer hint for the setup page.
static inline const char *wifi_fail_hint(wifi_fail_t f)
{
    switch (f) {
    case WIFI_FAIL_WRONG_PASSWORD: return "The router rejected the password. Passwords are case-sensitive.";
    case WIFI_FAIL_NOT_FOUND: return "The miner cannot see this network. It only uses 2.4 GHz; check the name and move it closer.";
    case WIFI_FAIL_SECURITY: return "The network uses a security mode the miner cannot join (WPA3-only, Enterprise or WEP). Enable WPA2 or WPA2/WPA3 mixed mode on the router.";
    case WIFI_FAIL_AUTH: return "The router refused the login (error 202 is often a WPA3 handshake problem, not the password). Try again close to the router; if it keeps failing, set the router to WPA2 or WPA2/WPA3 mixed, and check MAC filtering or the client limit.";
    case WIFI_FAIL_WEAK_SIGNAL: return "The connection timed out. Move the miner closer to the router.";
    case WIFI_FAIL_NO_IP: return "Joined the network but the router did not hand out an IP address (DHCP).";
    case WIFI_FAIL_OTHER: return "The connection failed. Try again, or restart the router.";
    default: return "";
    }
}

static inline const char *wifi_fail_code(wifi_fail_t f)   // stable id for the web UI
{
    switch (f) {
    case WIFI_FAIL_WRONG_PASSWORD: return "password";
    case WIFI_FAIL_NOT_FOUND: return "not_found";
    case WIFI_FAIL_SECURITY: return "security";
    case WIFI_FAIL_AUTH: return "refused";
    case WIFI_FAIL_WEAK_SIGNAL: return "signal";
    case WIFI_FAIL_NO_IP: return "dhcp";
    case WIFI_FAIL_OTHER: return "other";
    default: return "";
    }
}
