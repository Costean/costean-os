/* Host test: boot-loop guard (safe mode after repeated quick crashes) and the Wi-Fi failure classification. */
#include <stdio.h>
#include <string.h>
#include "boot_guard_logic.h"
#include "wifi_fail.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void crash_loop_enters_safe_mode(void)
{
    bg_state_t s = {0};
    bg_on_boot(&s, BG_RST_POWERON, BG_PHASE_NONE, 3);              /* first power-up */
    CHECK(!s.safe && s.quick == 0 && !s.last_unexpected && s.boots == 1);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_GOT_IP, 3);              /* crash right after the IP (the reported loop) */
    CHECK(!s.safe && s.quick == 1 && s.last_unexpected && s.last_phase == BG_PHASE_GOT_IP);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_DHCP, 3);
    CHECK(!s.safe && s.quick == 2);
    bg_on_boot(&s, BG_RST_TASK_WDT, BG_PHASE_ASIC_INIT, 3);
    CHECK(s.safe && s.safe_entered && s.quick == 3);
    /* safe mode is sticky across later boots, even clean ones */
    bg_on_boot(&s, BG_RST_POWERON, BG_PHASE_NONE, 3);
    CHECK(s.safe && !s.safe_entered && s.quick == 0);
    bg_leave_safe(&s);
    CHECK(!s.safe && s.quick == 0);
    bg_on_boot(&s, BG_RST_SW, BG_PHASE_RESTART_REQ, 3);
    CHECK(!s.safe && !s.last_unexpected);
    CHECK(s.boots == 6);
}

static void brownout_loop(void)
{
    bg_state_t s = {0};
    for (int i = 0; i < 3; i++) bg_on_boot(&s, BG_RST_BROWNOUT, BG_PHASE_NONE, 3);   /* RTC record lost: still counted */
    CHECK(s.safe && s.quick == 3);
}

static void normal_restarts_never_count(void)
{
    bg_state_t s = {0};
    for (int i = 0; i < 10; i++) bg_on_boot(&s, BG_RST_SW, BG_PHASE_RESTART_REQ, 3);  /* settings saved, restart */
    CHECK(!s.safe && s.quick == 0);
    for (int i = 0; i < 10; i++) bg_on_boot(&s, BG_RST_POWERON, BG_PHASE_NONE, 3);   /* unplugging it */
    CHECK(!s.safe && s.quick == 0);
    for (int i = 0; i < 5; i++) bg_on_boot(&s, BG_RST_USB, BG_PHASE_NONE, 3);        /* USB flashing */
    CHECK(!s.safe);
    /* an esp_restart() nobody asked for (module gave up) counts */
    bg_on_boot(&s, BG_RST_SW, BG_PHASE_RUNNING, 3);
    CHECK(s.last_unexpected && s.quick == 1);
    /* a clean power-on in between breaks the chain */
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_RUNNING, 3);
    bg_on_boot(&s, BG_RST_POWERON, BG_PHASE_NONE, 3);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_RUNNING, 3);
    CHECK(!s.safe && s.quick == 1);
}

static void stable_runs_reset_the_chain(void)
{
    bg_state_t s = {0};
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_DHCP, 3);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_DHCP, 3);
    CHECK(s.quick == 2);
    bg_on_stable(&s);                                                /* ran 2 minutes fine */
    CHECK(s.quick == 0);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_STABLE, 3);                /* crash after hours: counted once */
    CHECK(s.quick == 1 && !s.safe);
    bg_on_boot(&s, BG_RST_PANIC, BG_PHASE_STABLE, 3);
    CHECK(s.quick == 1 && !s.safe);
}

static void texts(void)
{
    for (int r = 0; r <= 16; r++) {
        CHECK(bg_reason_text(r) && bg_reason_text(r)[0]);
        CHECK(strlen(bg_reason_short(r)) <= 7);
    }
    for (int p = 0; p <= BG_PHASE_COUNT; p++) {
        CHECK(bg_phase_text(p) && bg_phase_text(p)[0]);
        CHECK(strlen(bg_phase_short(p)) <= 6);
    }
    CHECK(!strcmp(bg_reason_text(BG_RST_PANIC), "Crash (panic)"));
    CHECK(!strcmp(bg_phase_text(BG_PHASE_DHCP), "waiting for an IP address"));
    bg_state_t s = {0};
    bg_on_boot(&s, 99, 77, 3);                                       /* garbage in: no crash, phase unknown */
    CHECK(s.last_phase == BG_PHASE_NONE && !s.last_unexpected);
}

static void wifi_failures(void)
{
    CHECK(wifi_fail_classify(15) == WIFI_FAIL_WRONG_PASSWORD);
    CHECK(wifi_fail_classify(204) == WIFI_FAIL_WRONG_PASSWORD);
    CHECK(wifi_fail_classify(201) == WIFI_FAIL_NOT_FOUND);
    CHECK(wifi_fail_classify(210) == WIFI_FAIL_SECURITY);
    CHECK(wifi_fail_classify(211) == WIFI_FAIL_SECURITY);
    CHECK(wifi_fail_classify(202) == WIFI_FAIL_AUTH);
    CHECK(wifi_fail_classify(200) == WIFI_FAIL_WEAK_SIGNAL);
    CHECK(wifi_fail_classify(0) == WIFI_FAIL_NONE);
    CHECK(wifi_fail_classify(8) == WIFI_FAIL_OTHER);
    const char *codes[8];
    for (int f = WIFI_FAIL_WRONG_PASSWORD; f <= WIFI_FAIL_OTHER; f++) {
        codes[f] = wifi_fail_code((wifi_fail_t) f);
        CHECK(codes[f][0] && wifi_fail_text((wifi_fail_t) f)[0] && wifi_fail_hint((wifi_fail_t) f)[0]);
        CHECK(strlen(wifi_fail_text((wifi_fail_t) f)) < 32);            /* fits the OLED status line well */
        for (int g = WIFI_FAIL_WRONG_PASSWORD; g < f; g++) CHECK(strcmp(codes[f], codes[g]) != 0);
    }
    CHECK(!strcmp(wifi_fail_text(WIFI_FAIL_NONE), "") && !strcmp(wifi_fail_code(WIFI_FAIL_NONE), ""));
    CHECK(!strcmp(wifi_fail_code(WIFI_FAIL_WRONG_PASSWORD), "password"));
}

int main(void)
{
    crash_loop_enters_safe_mode();
    brownout_loop();
    normal_restarts_never_count();
    stable_runs_reset_the_chain();
    texts();
    wifi_failures();
    printf("boot_guard: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
