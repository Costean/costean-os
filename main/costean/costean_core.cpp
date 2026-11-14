#include "costean_core.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "costean_hw.h"
#include "board602.h"
#include "rollup.h"
#include "scheduler.h"
#include "tuning.h"

using namespace costean;

static const char *TAG = "costean";
static const char *NVS_NS = "costean";
#define TICK_MS 5000

namespace {

struct Cfg {
    Profile profile = Profile::CUSTOM;   // "Manual": the saved frequency / voltage (stock behaviour)
    Limits limits;
    AutoTune autotune;                   // off by default
    Schedule schedule;
    char tz[32] = "America/New_York";
};

costean_hw_t s_hw{};
SemaphoreHandle_t s_mx = nullptr;
portMUX_TYPE s_opMux = portMUX_INITIALIZER_UNLOCKED;
Cfg s_cfg;
bool s_cfgDirty = false;
bool s_cfgLoaded = false;
Presets s_presets;
Governor s_gov;
Rollup s_roll;
ScheduleResult s_sched;
Profile s_active = Profile::CUSTOM;
OpPoint s_target{}, s_applied{};
bool s_override = false;            // read by the power task (100 ms loop)
int s_ovMhz = 0, s_ovMv = 0;
Telemetry s_tel;
bool s_failover = false;
bool s_started = false;
bool s_overheatSeen = false;
volatile bool s_saveReq = false;
SemaphoreHandle_t s_saved = nullptr;
int64_t s_lastHistSave = 0;
uint64_t s_snapAcc = 0, s_snapRej = 0;
int64_t s_snapT = 0;
float s_rejPct = 0;

struct Lock {
    Lock() { if (s_mx) xSemaphoreTakeRecursive(s_mx, portMAX_DELAY); }
    ~Lock() { if (s_mx) xSemaphoreGiveRecursive(s_mx); }
};

int64_t utcNow() { time_t t = time(nullptr); return t > 1700000000 ? (int64_t) t : 0; }
uint64_t msNow() { return (uint64_t) (esp_timer_get_time() / 1000); }
const Zone &zone() { const Zone *z = zoneById(s_cfg.tz); return z ? *z : defaultZone(); }

void defaultSchedule(Schedule &s)
{
    s = Schedule();
    s.enabled = true;
    s.count = 1;
    Window &w = s.windows[0];
    w.enabled = true; w.days = 0x7f; w.start = 22 * 60; w.end = 7 * 60;
    w.profile = Profile::NONE; w.lcd = LcdMode::DIM;
    strcpy(w.name, "Night screen");
}

const char *lcdName(LcdMode m) { return m == LcdMode::DIM ? "dim" : m == LcdMode::OFF ? "off" : "normal"; }
LcdMode lcdFrom(const char *s) { return s && !strcmp(s, "dim") ? LcdMode::DIM : s && !strcmp(s, "off") ? LcdMode::OFF : LcdMode::NORMAL; }

// ---------- JSON <-> config (cJSON) ----------
void addPoint(cJSON *o, const char *k, OpPoint p)
{
    cJSON *x = cJSON_AddObjectToObject(o, k);
    cJSON_AddNumberToObject(x, "mhz", p.mhz);
    cJSON_AddNumberToObject(x, "mv", p.mv);
}

cJSON *cfgToJson(const Cfg &c)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "profile", profileName(c.profile));
    cJSON *l = cJSON_AddObjectToObject(o, "limits");
    cJSON_AddNumberToObject(l, "chipTemp", c.limits.chipTempC);
    cJSON_AddNumberToObject(l, "vrTemp", c.limits.vrTempC);
    cJSON_AddNumberToObject(l, "power", c.limits.powerW);
    cJSON *a = cJSON_AddObjectToObject(o, "autotune");
    cJSON_AddBoolToObject(a, "enabled", c.autotune.enabled);
    cJSON_AddNumberToObject(a, "maxSteps", c.autotune.maxStepsUp);
    cJSON_AddNumberToObject(a, "maxErrorPct", c.autotune.maxErrorPct);
    cJSON_AddNumberToObject(a, "maxRejectPct", c.autotune.maxRejectPct);
    cJSON_AddNumberToObject(a, "holdMin", (int) (c.autotune.holdMs / 60000));
    cJSON *s = cJSON_AddObjectToObject(o, "schedule");
    cJSON_AddBoolToObject(s, "enabled", c.schedule.enabled);
    cJSON_AddStringToObject(s, "tz", c.tz);
    cJSON *w = cJSON_AddArrayToObject(s, "windows");
    for (int i = 0; i < c.schedule.count; i++) {
        const Window &x = c.schedule.windows[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "on", x.enabled);
        cJSON_AddNumberToObject(j, "days", x.days);
        cJSON_AddNumberToObject(j, "start", x.start);
        cJSON_AddNumberToObject(j, "end", x.end);
        cJSON_AddStringToObject(j, "profile", profileName(x.profile));
        cJSON_AddStringToObject(j, "lcd", lcdName(x.lcd));
        cJSON_AddStringToObject(j, "name", x.name);
        cJSON_AddItemToArray(w, j);
    }
    return o;
}

bool isInt(const cJSON *v) { return cJSON_IsNumber(v) && fabs(v->valuedouble - (double) (int) v->valuedouble) < 1e-9; }
const cJSON *get(const cJSON *o, const char *k) { return o ? cJSON_GetObjectItemCaseSensitive(o, k) : nullptr; }

// Strict: returns an error or nullptr, only touches the fields present.
const char *cfgFromJson(Cfg &c, const cJSON *o)
{
    if (!cJSON_IsObject(o)) return "expected a JSON object";
    const cJSON *v = get(o, "profile");
    if (v) {
        if (!cJSON_IsString(v)) return "profile must be a string";
        Profile p = profileFromName(v->valuestring);
        if (p == Profile::NONE) return "unknown profile";
        c.profile = p;
    }
    const cJSON *l = get(o, "limits");
    if (l) {
        if (!cJSON_IsObject(l)) return "limits must be an object";
        int chip = c.limits.chipTempC, vr = c.limits.vrTempC, pw = c.limits.powerW;
        if ((v = get(l, "chipTemp"))) { if (!isInt(v)) return "chip limit must be a whole number"; chip = v->valueint; }
        if ((v = get(l, "vrTemp"))) { if (!isInt(v)) return "regulator limit must be a whole number"; vr = v->valueint; }
        if ((v = get(l, "power"))) { if (!isInt(v)) return "power limit must be a whole number"; pw = v->valueint; }
        const char *err = checkLimits602(chip, vr, pw);
        if (err) return err;
        c.limits.chipTempC = chip; c.limits.vrTempC = vr; c.limits.powerW = pw;
    }
    const cJSON *a = get(o, "autotune");
    if (a) {
        if (!cJSON_IsObject(a)) return "autotune must be an object";
        if ((v = get(a, "enabled"))) { if (!cJSON_IsBool(v)) return "autotune.enabled must be true/false"; c.autotune.enabled = cJSON_IsTrue(v); }
        if ((v = get(a, "maxSteps"))) { if (!isInt(v) || v->valueint < 1 || v->valueint > 4) return "auto-tune steps must be 1-4"; c.autotune.maxStepsUp = v->valueint; }
        if ((v = get(a, "maxErrorPct"))) { if (!cJSON_IsNumber(v) || v->valuedouble < 0.2 || v->valuedouble > 3) return "error ceiling must be 0.2-3 %"; c.autotune.maxErrorPct = (float) v->valuedouble; }
        if ((v = get(a, "maxRejectPct"))) { if (!cJSON_IsNumber(v) || v->valuedouble < 0.2 || v->valuedouble > 5) return "reject ceiling must be 0.2-5 %"; c.autotune.maxRejectPct = (float) v->valuedouble; }
        if ((v = get(a, "holdMin"))) { if (!isInt(v) || v->valueint < 5 || v->valueint > 120) return "dwell must be 5-120 min"; c.autotune.holdMs = (uint32_t) v->valueint * 60000u; }
    }
    const cJSON *s = get(o, "schedule");
    if (s) {
        if (!cJSON_IsObject(s)) return "schedule must be an object";
        if ((v = get(s, "enabled"))) { if (!cJSON_IsBool(v)) return "schedule.enabled must be true/false"; c.schedule.enabled = cJSON_IsTrue(v); }
        if ((v = get(s, "tz"))) {
            if (!cJSON_IsString(v) || !zoneById(v->valuestring)) return "unknown time zone";
            snprintf(c.tz, sizeof c.tz, "%s", v->valuestring);
        }
        const cJSON *arr = get(s, "windows");
        if (arr) {
            if (!cJSON_IsArray(arr)) return "windows must be an array";
            if (cJSON_GetArraySize(arr) > Schedule::kMaxWindows) return "at most 8 windows";
            Schedule ns = c.schedule;
            ns.count = 0;
            const cJSON *j;
            cJSON_ArrayForEach(j, arr) {
                if (!cJSON_IsObject(j)) return "window must be an object";
                Window w;
                const cJSON *x;
                w.enabled = (x = get(j, "on")) ? cJSON_IsTrue(x) : true;
                if ((x = get(j, "days"))) { if (!isInt(x) || x->valueint < 0 || x->valueint > 0x7f) return "days must be 0-127"; w.days = (uint8_t) x->valueint; }
                if ((x = get(j, "start"))) { if (!isInt(x) || x->valueint < 0 || x->valueint > 1439) return "start must be 0-1439"; w.start = (uint16_t) x->valueint; }
                if ((x = get(j, "end"))) { if (!isInt(x) || x->valueint < 0 || x->valueint > 1440) return "end must be 0-1440"; w.end = (uint16_t) x->valueint; }
                w.profile = Profile::NONE;
                if ((x = get(j, "profile")) && cJSON_IsString(x) && strcmp(x->valuestring, "none")) {
                    w.profile = profileFromName(x->valuestring);
                    if (w.profile == Profile::NONE) return "unknown profile in window";
                }
                w.lcd = lcdFrom((x = get(j, "lcd")) && cJSON_IsString(x) ? x->valuestring : "normal");
                if ((x = get(j, "name")) && cJSON_IsString(x)) snprintf(w.name, sizeof w.name, "%s", x->valuestring);
                const char *err = Schedule::validate(w);
                if (err) return err;
                ns.windows[ns.count++] = w;
            }
            c.schedule = ns;
        }
    }
    return nullptr;
}

// ---------- NVS (only from the Costean task: it has an internal-RAM stack) ----------
void nvsLoad()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = 0;
    if (nvs_get_blob(h, "cfg", nullptr, &len) == ESP_OK && len > 0 && len < 4096) {
        char *buf = (char *) malloc(len + 1);
        if (buf && nvs_get_blob(h, "cfg", buf, &len) == ESP_OK) {
            buf[len] = 0;
            cJSON *d = cJSON_Parse(buf);
            if (d) {
                Cfg c = s_cfg;
                const char *err = cfgFromJson(c, d);
                if (!err) { s_cfg = c; s_cfgLoaded = true; } else ESP_LOGW(TAG, "saved config ignored: %s", err);
                cJSON_Delete(d);
            }
        }
        free(buf);
    }
    len = Rollup::kBlobSize;
    uint8_t *hb = (uint8_t *) malloc(len);
    if (hb && nvs_get_blob(h, "hist", hb, &len) == ESP_OK) {
        if (s_roll.restore(hb, len)) ESP_LOGI(TAG, "history restored: %d points", s_roll.size());
        else ESP_LOGW(TAG, "saved history invalid, starting empty");
    }
    free(hb);
    nvs_close(h);
}

void nvsSaveCfg()
{
    char *buf;
    {
        Lock l;
        cJSON *d = cfgToJson(s_cfg);
        buf = cJSON_PrintUnformatted(d);
        cJSON_Delete(d);
        s_cfgDirty = false;
    }
    if (!buf) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t e = nvs_set_blob(h, "cfg", buf, strlen(buf));
        if (e == ESP_OK) nvs_commit(h); else ESP_LOGE(TAG, "saving config failed: %s", esp_err_to_name(e));
        nvs_close(h);
    }
    cJSON_free(buf);
}

void nvsSaveHist()
{
    uint8_t *buf = (uint8_t *) malloc(Rollup::kBlobSize);
    if (!buf) return;
    size_t n;
    { Lock l; n = s_roll.save(buf, Rollup::kBlobSize); }
    nvs_handle_t h;
    if (n && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t e = nvs_set_blob(h, "hist", buf, n);
        if (e == ESP_OK) nvs_commit(h); else ESP_LOGE(TAG, "saving history failed: %s", esp_err_to_name(e));
        nvs_close(h);
    }
    free(buf);
}

// ---------- board ----------
void setupBoard()
{
    Envelope e;
    Limits defaults;
    costean_hw_read(&s_hw);
    setupBoard602(s_hw.stock_mhz, s_hw.stock_mv, s_hw.user_mhz, s_hw.user_mv, e, s_presets, defaults);
    if (!s_cfgLoaded) { s_cfg.limits = defaults; }
    s_gov.configure(e, effectiveLimits602(s_cfg.limits), s_cfg.autotune);
}

// ---------- SNTP: schedules need wall-clock time ----------
void startSntp()
{
    if (esp_sntp_enabled()) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
}

// ---------- the 5 s loop ----------
void readTelemetry()
{
    costean_hw_read(&s_hw);
    Telemetry t;
    t.chipTempC = s_hw.chip_c;
    t.vrTempC = s_hw.vr_c;
    t.powerW = s_hw.power_w;
    t.errorPct = s_hw.error_pct;
    t.mining = s_hw.mining;
    // pool reject % over ~10 min windows
    uint64_t a = s_hw.accepted, r = s_hw.rejected;
    int64_t now = (int64_t) msNow();
    if (s_snapT == 0 || a < s_snapAcc || r < s_snapRej) { s_snapT = now; s_snapAcc = a; s_snapRej = r; }
    else if (now - s_snapT >= 600000) {
        uint64_t da = a - s_snapAcc, dr = r - s_snapRej;
        s_rejPct = da + dr >= 20 ? 100.0f * dr / (float) (da + dr) : 0;
        s_snapT = now; s_snapAcc = a; s_snapRej = r;
    }
    t.rejectPct = s_rejPct;
    s_tel = t;
}

void step()
{
    readTelemetry();
    uint64_t now = msNow();
    int64_t utc = utcNow();
    // Stock overheat safe mode fired (the last line of defence): go back to Manual, auto-tune off.
    bool overheat = s_hw.overheat;
    if (overheat && !s_overheatSeen) {
        Lock l;
        ESP_LOGW(TAG, "stock overheat protection tripped: profile -> Manual, auto-tune off");
        s_cfg.profile = Profile::CUSTOM;
        s_cfg.autotune.enabled = false;
        s_gov.resetTune();
        s_cfgDirty = true;
    }
    s_overheatSeen = overheat;

    OpPoint out;
    int uM = s_hw.user_mhz, uV = s_hw.user_mv;
    {
        Lock l;
        Envelope e; Limits unused;
        setupBoard602(s_hw.stock_mhz, s_hw.stock_mv, uM, uV, e, s_presets, unused);
        s_gov.configure(e, effectiveLimits602(s_cfg.limits), s_cfg.autotune);
        s_sched = s_cfg.schedule.enabled ? s_cfg.schedule.evaluate(utc, zone()) : ScheduleResult();
        s_active = s_sched.profile != Profile::NONE ? s_sched.profile : s_cfg.profile;
        s_target = s_presets.of(s_active);
        out = s_gov.step(s_target, s_tel, now);
    }
    // Apply: when the clock goes down, lower it first and the voltage on the next tick.
    // (Going up, the power task already sets the voltage before the clock.)
    int curF = s_applied.mhz ? s_applied.mhz : uM, curV = s_applied.mv ? s_applied.mv : uV;
    OpPoint apply = out;
    if (out.mhz < curF && out.mv < curV) apply.mv = curV;
    if (apply.mhz != curF || apply.mv != curV) {
        ESP_LOGI(TAG, "operating point %d MHz / %d mV (%s, %s%s%s)", apply.mhz, apply.mv, profileName(s_active),
                 govStateName(s_gov.state()), s_gov.reason()[0] ? ": " : "", s_gov.reason());
    }
    bool isSaved = apply.mhz == uM && apply.mv == uV;
    taskENTER_CRITICAL(&s_opMux);
    s_override = !isSaved && !overheat;
    s_ovMhz = apply.mhz;
    s_ovMv = apply.mv;
    taskEXIT_CRITICAL(&s_opMux);
    s_applied = apply;

    s_failover = s_hw.failover;

    if (utc && s_tel.mining) {
        uint8_t fl = (s_gov.throttleSteps() > 0 ? 1 : 0) | (s_failover ? 2 : 0) | (s_sched.profile != Profile::NONE ? 4 : 0) |
                     (s_gov.tuneSteps() > 0 ? 8 : 0);
        Lock l;
        s_roll.add(utc, s_hw.ghs, s_tel.powerW, s_tel.chipTempC, s_tel.vrTempC, fl);
    }
}

void task(void *)
{
    nvsLoad();
    { Lock l; setupBoard(); }
    s_started = true;
    ESP_LOGI(TAG, "started: profile %s, auto-tune %s, limits %d C / %d C / %d W", profileName(s_cfg.profile),
             s_cfg.autotune.enabled ? "on" : "off", s_cfg.limits.chipTempC, s_cfg.limits.vrTempC, s_cfg.limits.powerW);
    while (1) {
        costean_hw_read(&s_hw);
        if (!s_hw.self_test) step();
        if (s_cfgDirty) nvsSaveCfg();
        int64_t utc = utcNow();
        // save the long history every 30 min (about 1.5 KB) or when a restart is coming
        if (s_saveReq || (utc && utc - s_lastHistSave >= 1800 && s_roll.size() > 0)) {
            if (s_lastHistSave || s_saveReq) nvsSaveHist();   // not right after boot
            s_lastHistSave = utc;
            if (s_saveReq) { s_saveReq = false; if (s_saved) xSemaphoreGive(s_saved); }
        }
        for (int i = 0; i < TICK_MS / 250 && !s_saveReq; i++) vTaskDelay(pdMS_TO_TICKS(250));
    }
}

const char *upperProfile(Profile p)
{
    return p == Profile::ECO ? "ECO" : p == Profile::STOCK ? "STOCK" : p == Profile::TURBO ? "TURBO" : "MANUAL";
}

} // namespace

extern "C" {

void costean_init(GlobalState *gs)
{
    if (s_mx) return;
    costean_hw_set_global(gs);
    s_mx = xSemaphoreCreateRecursiveMutex();
    s_saved = xSemaphoreCreateBinary();
    defaultSchedule(s_cfg.schedule);
    startSntp();
    // internal-RAM stack: this task writes NVS (flash), which must not run on a PSRAM stack
    if (xTaskCreate(task, "costean", 6144, nullptr, 4, nullptr) != pdPASS) ESP_LOGE(TAG, "task start failed");
}

void costean_before_restart(void)
{
    if (!s_started || !s_saved) return;
    s_saveReq = true;
    xSemaphoreTake(s_saved, pdMS_TO_TICKS(1500));
}

bool costean_operating_point(float *mhz, uint16_t *mv)
{
    taskENTER_CRITICAL(&s_opMux);
    bool ov = s_override;
    int f = s_ovMhz, v = s_ovMv;
    taskEXIT_CRITICAL(&s_opMux);
    if (!ov || f <= 0 || v <= 0) return false;
    *mhz = (float) f;
    *mv = (uint16_t) v;
    return true;
}

uint8_t costean_screen_mode(void) { return s_cfg.schedule.enabled ? (uint8_t) s_sched.lcd : 0; }

void costean_screen_info(costean_screen_info_t *o)
{
    memset(o, 0, sizeof *o);
    Lock l;
    snprintf(o->profile, sizeof o->profile, "%s", upperProfile(s_active));
    o->throttled = s_gov.throttleSteps() > 0;
    o->scheduled = s_sched.profileWindow >= 0;
    o->tune_steps = s_gov.tuneSteps();
    Limits lim = effectiveLimits602(s_cfg.limits);
    o->lim_chip = lim.chipTempC; o->lim_vr = lim.vrTempC; o->lim_power = lim.powerW;
    o->screen_mode = costean_screen_mode();
}

cJSON *costean_status_json(void)
{
    Lock l;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "board", "602");
    cJSON_AddStringToObject(o, "activeProfile", profileName(s_active));
    cJSON_AddStringToObject(o, "profileSource", s_sched.profile != Profile::NONE ? "schedule" : "user");
    addPoint(o, "target", s_target);
    addPoint(o, "applied", s_applied);
    cJSON *g = cJSON_AddObjectToObject(o, "governor");
    cJSON_AddStringToObject(g, "state", govStateName(s_gov.state()));
    cJSON_AddStringToObject(g, "reason", s_gov.reason());
    cJSON_AddNumberToObject(g, "throttleSteps", s_gov.throttleSteps());
    cJSON_AddNumberToObject(g, "tuneSteps", s_gov.tuneSteps());
    cJSON_AddNumberToObject(g, "bestStableSteps", s_gov.bestStableSteps());
    cJSON_AddNumberToObject(g, "errorPct", s_tel.errorPct);
    cJSON_AddNumberToObject(g, "rejectPct", s_tel.rejectPct);
    cJSON_AddBoolToObject(g, "mining", s_tel.mining);
    Limits lim = effectiveLimits602(s_cfg.limits);
    cJSON *el = cJSON_AddObjectToObject(o, "effectiveLimits");
    cJSON_AddNumberToObject(el, "chipTemp", lim.chipTempC);
    cJSON_AddNumberToObject(el, "vrTemp", lim.vrTempC);
    cJSON_AddNumberToObject(el, "power", lim.powerW);
    cJSON *rg = cJSON_AddObjectToObject(o, "limitRanges");
    int chipR[2] = {Board602::kChipMin, Board602::kChipMax}, vrR[2] = {Board602::kVrMin, Board602::kVrMax},
        pwR[2] = {Board602::kPowerMin, Board602::kPowerMax};
    cJSON_AddItemToObject(rg, "chipTemp", cJSON_CreateIntArray(chipR, 2));
    cJSON_AddItemToObject(rg, "vrTemp", cJSON_CreateIntArray(vrR, 2));
    cJSON_AddItemToObject(rg, "power", cJSON_CreateIntArray(pwR, 2));
    cJSON *cut = cJSON_AddObjectToObject(o, "stockCutoffs");
    cJSON_AddNumberToObject(cut, "chipTemp", Board602::kStockChipCutC);
    cJSON_AddNumberToObject(cut, "vrTemp", Board602::kStockVrCutC);
    cJSON *pr = cJSON_AddObjectToObject(o, "presets");
    for (Profile p : {Profile::ECO, Profile::STOCK, Profile::TURBO, Profile::CUSTOM}) addPoint(pr, profileName(p), s_presets.of(p));
    cJSON *env = cJSON_AddObjectToObject(o, "envelope");
    cJSON_AddNumberToObject(env, "minMhz", s_gov.envelope().minMhz);
    cJSON_AddNumberToObject(env, "maxMhz", s_gov.envelope().maxMhz);
    cJSON_AddNumberToObject(env, "stepMhz", s_gov.envelope().stepMhz);
    cJSON *sc = cJSON_AddObjectToObject(o, "schedule");
    cJSON_AddBoolToObject(sc, "clockValid", utcNow() != 0);
    cJSON_AddNumberToObject(sc, "profileWindow", s_sched.profileWindow);
    cJSON_AddNumberToObject(sc, "lcdWindow", s_sched.lcdWindow);
    cJSON_AddStringToObject(sc, "lcd", lcdName(s_sched.lcd));
    cJSON_AddStringToObject(sc, "profile", profileName(s_sched.profile));
    int64_t utc = utcNow();
    if (utc) {
        const Zone &z = zone();
        cJSON_AddNumberToObject(sc, "localTime", (double) toLocal(utc, z));
        cJSON_AddBoolToObject(sc, "dst", isDst(utc, z));
        if (s_sched.nextChangeLocal) cJSON_AddNumberToObject(sc, "nextChangeLocal", (double) s_sched.nextChangeLocal);
    }
    cJSON *zs = cJSON_AddArrayToObject(o, "zones");
    for (int i = 0; zoneAt(i); i++) cJSON_AddItemToArray(zs, cJSON_CreateString(zoneAt(i)->id));
    cJSON_AddBoolToObject(o, "failover", s_failover);
    costean_hw_split_json(o);
    return o;
}

cJSON *costean_config_json(void) { Lock l; return cfgToJson(s_cfg); }

cJSON *costean_history_json(void)
{
    Lock l;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "slotSec", Rollup::kSlotSec);
    cJSON_AddNumberToObject(o, "start", s_roll.size() ? (double) s_roll.slotTime(0) : 0);
    cJSON *gh = cJSON_AddArrayToObject(o, "ghs"), *w = cJSON_AddArrayToObject(o, "watts"), *c = cJSON_AddArrayToObject(o, "chipTemp"),
          *v = cJSON_AddArrayToObject(o, "vrTemp"), *e = cJSON_AddArrayToObject(o, "jth"), *f = cJSON_AddArrayToObject(o, "flags");
    for (int i = 0; i < s_roll.size(); i++) {
        const RollupPoint &p = s_roll.at(i);
        if (p.ghs == 0xFFFF) {
            cJSON_AddItemToArray(gh, cJSON_CreateNull()); cJSON_AddItemToArray(w, cJSON_CreateNull());
            cJSON_AddItemToArray(c, cJSON_CreateNull()); cJSON_AddItemToArray(v, cJSON_CreateNull());
            cJSON_AddItemToArray(e, cJSON_CreateNull()); cJSON_AddItemToArray(f, cJSON_CreateNumber(0));
            continue;
        }
        cJSON_AddItemToArray(gh, cJSON_CreateNumber(p.ghs));
        cJSON_AddItemToArray(w, cJSON_CreateNumber(p.watts10 / 10.0));
        cJSON_AddItemToArray(c, cJSON_CreateNumber(p.chip2 / 2.0));
        cJSON_AddItemToArray(v, cJSON_CreateNumber(p.vr2 / 2.0));
        cJSON_AddItemToArray(e, p.eff == 255 ? cJSON_CreateNull() : cJSON_CreateNumber(p.eff));
        cJSON_AddItemToArray(f, cJSON_CreateNumber(p.flags));
    }
    return o;
}

const char *costean_patch_config(const cJSON *body)
{
    Lock l;
    Cfg c = s_cfg;
    const char *err = cfgFromJson(c, body);
    if (err) return err;
    bool tuneWasOn = s_cfg.autotune.enabled;
    s_cfg = c;
    const cJSON *a = get(body, "autotune");
    if (cJSON_IsTrue(get(a, "reset"))) s_gov.resetTune();
    if (tuneWasOn != s_cfg.autotune.enabled) ESP_LOGW(TAG, "auto-tune %s", s_cfg.autotune.enabled ? "ENABLED" : "disabled");
    s_cfgDirty = true;
    return nullptr;
}

} // extern "C"
