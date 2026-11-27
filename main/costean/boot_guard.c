#include <string.h>
#include <stdio.h>
#include "boot_guard.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boot_guard";
#define BG_NS "costean_bg"
#define BG_MAGIC 0xC05EA606u
#define BG_STABLE_MS 120000

typedef struct {
    uint32_t magic;
    uint32_t phase;
    char wifi[64];
    uint32_t check;
} bg_rtc_t;

static RTC_NOINIT_ATTR bg_rtc_t s_rtc;
static bg_state_t s_st;
static char s_last_wifi[64];
static bool s_inited;

static uint32_t rtc_check(const bg_rtc_t *r)
{
    uint32_t h = 2166136261u ^ r->phase;
    for (size_t i = 0; i < sizeof(r->wifi); i++) h = (h ^ (uint8_t) r->wifi[i]) * 16777619u;
    return h;
}

static void rtc_commit(void)
{
    s_rtc.magic = BG_MAGIC;
    s_rtc.check = rtc_check(&s_rtc);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(BG_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "boots", s_st.boots);
    nvs_set_u8(h, "quick", s_st.quick);
    nvs_set_u8(h, "safe", s_st.safe ? 1 : 0);
    nvs_set_u8(h, "lastrr", s_st.last_reason);
    nvs_set_u8(h, "lastph", s_st.last_phase);
    nvs_set_str(h, "lastwifi", s_last_wifi);
    nvs_commit(h);
    nvs_close(h);
}

void boot_guard_init(void)
{
    if (s_inited) return;
    s_inited = true;
    int reason = (int) esp_reset_reason();
    int prev_phase = BG_PHASE_NONE;
    s_last_wifi[0] = 0;
    if (s_rtc.magic == BG_MAGIC && s_rtc.check == rtc_check(&s_rtc) && s_rtc.phase < BG_PHASE_COUNT) {
        prev_phase = (int) s_rtc.phase;
        memcpy(s_last_wifi, s_rtc.wifi, sizeof(s_last_wifi));
        s_last_wifi[sizeof(s_last_wifi) - 1] = 0;
    }

    nvs_handle_t h;
    if (nvs_open(BG_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v8;
        uint32_t v32;
        if (nvs_get_u32(h, "boots", &v32) == ESP_OK) s_st.boots = v32;
        if (nvs_get_u8(h, "quick", &v8) == ESP_OK) s_st.quick = v8;
        if (nvs_get_u8(h, "safe", &v8) == ESP_OK) s_st.safe = v8 != 0;
        nvs_close(h);
    }

    bg_on_boot(&s_st, reason, prev_phase, BG_SAFE_THRESHOLD);
    nvs_save();

    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.phase = BG_PHASE_BOOT;
    rtc_commit();

    char d[160];
    boot_guard_describe_last(d, sizeof(d));
    ESP_LOGI(TAG, "boot #%lu, previous run ended: %s (reason %d, phase %s)%s%s", (unsigned long) s_st.boots,
             bg_reason_text(reason), reason, bg_phase_text(prev_phase), s_last_wifi[0] ? ", Wi-Fi: " : "", s_last_wifi);
    if (s_st.last_unexpected) ESP_LOGW(TAG, "unexpected reset: %s; %d in a row", d, s_st.quick);
    if (s_st.safe) ESP_LOGW(TAG, "SAFE MODE%s: hotspot and web UI only; mining and extras are off until you leave safe mode",
                            s_st.safe_entered ? " (entered on this boot)" : "");
}

void boot_guard_set_phase(bg_phase_t phase)
{
    if (s_rtc.phase == (uint32_t) phase) return;
    s_rtc.phase = (uint32_t) phase;
    rtc_commit();
}

bg_phase_t boot_guard_phase(void) { return (bg_phase_t) s_rtc.phase; }

void boot_guard_note_wifi(const char *status)
{
    if (!status) return;
    if (strncmp(s_rtc.wifi, status, sizeof(s_rtc.wifi) - 1) == 0) return;
    strncpy(s_rtc.wifi, status, sizeof(s_rtc.wifi) - 1);
    s_rtc.wifi[sizeof(s_rtc.wifi) - 1] = 0;
    rtc_commit();
}

void boot_guard_restart_requested(void)
{
    boot_guard_set_phase(BG_PHASE_RESTART_REQ);
}

static void stable_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(BG_STABLE_MS));
    bg_on_stable(&s_st);
    if (s_rtc.phase == BG_PHASE_RUNNING) boot_guard_set_phase(BG_PHASE_STABLE);
    nvs_handle_t h;
    if (nvs_open(BG_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "quick", 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "running for %d s without a reset: quick-reset counter cleared", BG_STABLE_MS / 1000);
    vTaskDelete(NULL);
}

void boot_guard_start_stable_timer(void)
{
    static bool started;
    if (started) return;
    started = true;
    if (xTaskCreate(stable_task, "boot guard", 6144, NULL, 2, NULL) != pdPASS) ESP_LOGE(TAG, "stable task failed");
}

bool boot_guard_safe_mode(void) { return s_st.safe; }
const bg_state_t *boot_guard_state(void) { return &s_st; }
const char *boot_guard_last_wifi(void) { return s_last_wifi; }

void boot_guard_leave_safe(void)
{
    bg_leave_safe(&s_st);
    nvs_save();
    ESP_LOGI(TAG, "leaving safe mode");
}

void boot_guard_describe_last(char *out, size_t n)
{
    if (!out || !n) return;
    if (!s_st.last_unexpected) { out[0] = 0; return; }
    if (s_st.last_phase != BG_PHASE_NONE)
        snprintf(out, n, "%s while %s", bg_reason_text(s_st.last_reason), bg_phase_text(s_st.last_phase));
    else
        snprintf(out, n, "%s", bg_reason_text(s_st.last_reason));
}
