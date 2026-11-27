// Mock Bitaxe Gamma 602 (bitaxeorg ESP-Miner v2.15.4 API shape + Costean OS /api/costean) serving clearly-labelled DEMO data.
// Never talks to a real device. SCENARIO=normal|failover|throttle|block selects the demo state.
import http from 'node:http'; import fs from 'node:fs'; import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { detectUpdate } from '../src/lib/ota.js';
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = +process.env.PORT || 8099, ROOT = path.resolve(process.env.DIST || path.join(__dirname, '../dist/axe-os'));
let FW = process.env.FW_VERSION || '1.2.0-costean.7';
// OTA_NOAPPLY=1 simulates an update that uploads but does not take (the costean.4 report), to preview the failure message
const OTA_NOAPPLY = process.env.OTA_NOAPPLY === '1'; let downUntil = 0, slot = 'ota_0';
const SC = process.env.SCENARIO || 'normal', BLOCK = SC === 'block', FAILOVER = SC === 'failover', THROTTLE = SC === 'throttle', SETUP = SC === 'setup' || SC === 'setupfail' || SC === 'joining' || SC === 'crashed';
let SAFE = SC === 'safemode';
export const writes = [];
// NVS emulation: NVS_FILE keeps settings across a mock "reboot" (kill + start again with the same file).
const NVS_FILE = process.env.NVS_FILE || '';
const loadNvs = () => { try { return NVS_FILE && fs.existsSync(NVS_FILE) ? JSON.parse(fs.readFileSync(NVS_FILE, 'utf8')) : null; } catch { return null; } };
const saveNvs = () => { if (NVS_FILE) fs.writeFileSync(NVS_FILE, JSON.stringify({ S, pools, CFG, passwords, FW, slot })); };
const passwords = {};
const t0 = Date.now(), rnd = (a, b) => a + Math.random() * (b - a);
const S = { primaryPoolIndex: 0, secondaryPoolIndex: process.env.SCENARIO === 'fresh' ? 1 : 2, frequency: 525, coreVoltage: 1150, hostname: 'bitaxe-demo', ssid: 'DEMO-WIFI', poolSplitEnabled: process.env.SCENARIO === 'fresh' ? 0 : 1, blockFound: BLOCK ? 1 : 0, showNewBlock: BLOCK,
  discordEnabled: 1, discordConfigured: 1, discordWebhookPreview: 'https://discord.com/api/webhooks/12…89/********', discordBlockFound: 1, discordBestDiff: 1, discordOffline: 1, discordOverheat: 1, discordStatusMinutes: 60, autofanspeed: 1, manualFanSpeed: 70, overclockEnabled: 0, invertscreen: 0 };
const POOL = (id, url, port, pct) => ({ id, stratumProtocol: 'SV1', stratumURL: url, stratumPort: port, stratumUser: 'bc1qDEMOdemoDEMO.costean', stratumPassword: '*****', stratumSuggestedDifficulty: 0, stratumExtranonceSubscribe: false, stratumTLS: 0, stratumCert: '', stratumDecodeCoinbase: true, stratumV2ChannelType: 'extended', stratumV2AuthorityPubkey: '', stratumV2RequireAuth: false, splitPercent: pct });
const pools = process.env.SCENARIO === 'fresh' ? [POOL(0, 'public-pool.io', 3333, 0), POOL(1, 'solo.ckpool.org', 3333, 0)] : [POOL(0, 'demo-pool.example', 3333, 70), POOL(1, 'solo-demo.example', 3333, 30), POOL(2, 'backup-pool.example', 3333, 0)];
const CFG = { profile: 'custom', profiles: {}, limits: { chipTemp: 68, vrTemp: 90, power: 27, psu: 30 }, autotune: { enabled: false, maxSteps: 2, maxErrorPct: 1, maxRejectPct: 1, holdMin: 15 },
  schedule: { enabled: true, tz: 'America/New_York', windows: [
    { on: true, days: 127, start: 1320, end: 420, profile: 'none', lcd: 'dim', name: 'Night screen' },
    { on: true, days: 62, start: 0, end: 360, profile: 'eco', lcd: 'off', name: 'Quiet night' },
    { on: true, days: 62, start: 540, end: 1020, profile: 'eco', lcd: 'normal', name: 'Workday eco' },
    { on: false, days: 65, start: 600, end: 1080, profile: 'stock', lcd: 'normal', name: 'Weekend' }] } };
{ const n = loadNvs(); if (n) { Object.assign(S, n.S); pools.length = 0; pools.push(...n.pools); Object.assign(CFG, n.CFG); Object.assign(passwords, n.passwords || {}); if (n.FW && !process.env.FW_VERSION) FW = n.FW; if (n.slot) slot = n.slot; } }
// editable profiles, same rules as the firmware (board602.cpp / costean_core.cpp)
const DEF = { eco: { mhz: 425, mv: 1100 }, stock: { mhz: 525, mv: 1150 }, turbo: { mhz: 575, mv: 1180 }, user1: { mhz: 525, mv: 1150 }, user2: { mhz: 525, mv: 1150 } };
const DEFNAME = { eco: 'Eco', stock: 'Stock', turbo: 'Turbo', user1: 'Custom 1', user2: 'Custom 2', custom: 'Manual' };
const EDITABLE = ['eco', 'stock', 'turbo', 'user1', 'user2'];
const ed = id => CFG.profiles?.[id] || {};
const pt = id => (ed(id).mhz ? { mhz: ed(id).mhz, mv: ed(id).mv } : { ...DEF[id] });
const available = id => !id.startsWith('user') || !!ed(id).enabled;
const PRESETS = () => ({ eco: pt('eco'), stock: pt('stock'), turbo: pt('turbo'), user1: pt('user1'), user2: pt('user2'), custom: { mhz: Math.round(S.frequency), mv: S.coreVoltage } });
const profileList = () => ['eco', 'stock', 'turbo', 'custom', 'user1', 'user2'].map(id => ({ id, name: ed(id).name || DEFNAME[id], defaultName: DEFNAME[id], ...PRESETS()[id],
  default: id === 'custom' ? PRESETS().custom : DEF[id], editable: id !== 'custom', edited: !!(ed(id).mhz || ed(id).name), enabled: available(id) }));
function patchProfiles(o) {   // returns an error or null; mutates CFG only when everything is valid
  const next = JSON.parse(JSON.stringify(CFG.profiles || {}));
  if (o.profilesReset === true) for (const k of Object.keys(next)) delete next[k];
  for (const [id, j] of Object.entries(o.profiles || {})) {
    if (!EDITABLE.includes(id)) return 'only eco, stock, turbo, user1 and user2 can be edited (Manual is set on Mining & Tuning)';
    let e = j.reset ? {} : { ...(next[id] || {}) };
    if ('mhz' in j || 'mv' in j) { const f = j.mhz ?? e.mhz ?? DEF[id].mhz, v = j.mv ?? e.mv ?? DEF[id].mv;
      if (!Number.isInteger(f) || f < 400 || f > 1000) return 'profile frequency must be 400-1000 MHz';
      if (!Number.isInteger(v) || v < 1000 || v > 1300) return 'profile core voltage must be 1000-1300 mV'; e.mhz = f; e.mv = v; }
    if ('name' in j) { if (typeof j.name !== 'string') return 'profile name must be text';
      if (j.name && (j.name.length > 12 || /["\\]/.test(j.name) || /[^\x20-\x7e]/.test(j.name))) return 'profile name: 1-12 letters, digits or spaces'; if (j.name) e.name = j.name; else delete e.name; }
    if ('enabled' in j) { if (!id.startsWith('user') && !j.enabled) return "Eco, Stock and Turbo can't be turned off"; e.enabled = !!j.enabled; }
    if (Object.keys(e).length) next[id] = e; else delete next[id];
  }
  CFG.profiles = next;
  if (!available(CFG.profile)) CFG.profile = 'custom';
  return null;
}
const prof = () => THROTTLE ? 'stock' : CFG.profile;
const applied = () => THROTTLE ? { mhz: 475, mv: 1150 } : PRESETS()[prof()];
function splitStats(hr) {
  const weighted = pools.filter(p => p.splitPercent > 0), up = id => !(FAILOVER && id === 1);
  const live = weighted.filter(p => up(p.id)), sum = live.reduce((a, p) => a + p.splitPercent, 0) || 1;
  const acc = [() => 294 + Math.floor((Date.now() - t0) / 2600), () => 126 + Math.floor((Date.now() - t0) / 6100), () => 0];
  return pools.map(p => { const eff = up(p.id) && p.splitPercent > 0 ? Math.round(p.splitPercent * 100 / sum) : 0;
    return { id: p.id, configuredPercent: p.splitPercent, effectivePercent: eff, connected: up(p.id), ready: up(p.id), jobsSent: 0, workReceived: 0,
      sharesAccepted: acc[p.id](), sharesRejected: p.id === 2 ? 0 : 1, difficulty: p.id === 1 ? 512 : 1024, stratumURL: p.stratumURL,
      healthy: up(p.id), backup: p.splitPercent === 0, jobShare: eff / 100 + (eff ? rnd(-0.004, 0.004) : 0), creditedGhs: eff ? hr * eff / 100 * rnd(0.85, 1.15) : 0 }; });
}
function info() {
  const a = applied(), hr = (THROTTLE ? 1065 : a.mhz * 2040 / 1000 * 0.985) + rnd(-15, 15), w = +(hr / 1000 * 15.4 + rnd(-0.2, 0.2)).toFixed(1);
  return { demo: true, ...S, hashRate: hr, hashRate_1m: hr * 0.99, hashRate_10m: hr, hashRate_1h: hr, expectedHashrate: a.mhz * 2040 / 1000, errorPercentage: 0.38,
    bestDiff: BLOCK ? 1.31e14 : 1.89e9, bestSessionDiff: 4.2e7, networkDifficulty: 1.27e14, poolDifficulty: 1024,
    coreVoltageActual: a.mv - 2, actualFrequency: a.mhz, temp: THROTTLE ? 68.6 : +(58.2 + rnd(-0.3, 0.3)).toFixed(1), temp2: 0, vrTemp: THROTTLE ? 71.2 : +(47.4 + rnd(-0.3, 0.3)).toFixed(1),
    fanspeed: THROTTLE ? 100 : 62, fanrpm: THROTTLE ? 6200 : 4100, power: THROTTLE ? 24.6 : w, voltage: 5120,
    sharesAccepted: 420 + Math.floor((Date.now() - t0) / 1900), sharesRejected: 3, uptimeSeconds: 90000 + Math.floor((Date.now() - t0) / 1000),
    ipv4: '192.0.2.42', wifiRSSI: -51, macAddr: 'DE:MO:00:00:06:02', version: FW, axeOSVersion: FW, runningPartition: slot, bootPartition: slot, firmwareBuilt: 'Nov 14 2026 12:00:00', firmwareName: 'Costean OS', boardVersion: '602', ASICModel: 'BM1370', asicCount: 1, smallCoreCount: 2040,
    overheat_mode: 0, miningPaused: 0, freeHeap: 120000, isUsingFallbackStratum: 0, primaryPoolIndex: S.primaryPoolIndex, secondaryPoolIndex: S.secondaryPoolIndex, useFallbackStratum: 1,
    poolSplitActive: S.poolSplitEnabled ? 1 : 0, poolSplitError: 'ok', poolSplitFailover: FAILOVER ? 1 : 0, poolSplitRestartNeeded: 0,
    poolSplitStats: S.poolSplitEnabled ? splitStats(hr) : [], pools: [...pools].sort((a, b) => a.id - b.id), resetReason: 'Power-on', nvsWriteErrors: 0, nvsFreeEntries: 412,
    apEnabled: SETUP ? 1 : 0, apSsid: 'Bitaxe_6A2F', wifiStatus: SC === 'setupfail' ? 'Wrong password? (Error 15, retry #2)' : SC === 'joining' ? 'Acquiring IP...' : SC === 'crashed' ? 'Connecting...' : SETUP ? 'Setup: choose a Wi-Fi network' : 'Connected!',
    wifiLink: SC === 'joining' ? 'dhcp' : SC === 'crashed' ? 'joining' : SC === 'setup' ? 'idle' : SC === 'setupfail' ? 'joining' : 'up',
    wifiFailReason: SC === 'setupfail' ? 15 : 0, wifiFailCode: SC === 'setupfail' ? 'password' : '', wifiFailText: SC === 'setupfail' ? 'Wrong password?' : '',
    wifiFailHint: SC === 'setupfail' ? 'The router rejected the password. Passwords are case-sensitive.' : '', wifiRetries: SC === 'setupfail' ? 2 : 0,
    lastResetReason: SAFE || SC === 'crashed' ? 'Crash (panic)' : 'Power on', lastResetCode: SAFE || SC === 'crashed' ? 4 : 1,
    lastResetPhase: SAFE || SC === 'crashed' ? 'finishing the Wi-Fi connection' : '', lastResetUnexpected: SAFE || SC === 'crashed',
    lastResetText: SAFE || SC === 'crashed' ? 'Crash (panic) while finishing the Wi-Fi connection' : '', lastResetWifi: SAFE || SC === 'crashed' ? 'Connected!' : '',
    bootCount: SAFE ? 7 : 3, quickResets: SAFE ? 3 : SC === 'crashed' ? 1 : 0, safeMode: SAFE ? 1 : 0,
    ...(SC === 'setup' ? { ssid: '', ipv4: '', wifiRSSI: 0 } : SC === 'setupfail' || SC === 'joining' || SC === 'crashed' ? { ssid: 'DEMO-Home', ipv4: '' } : {}),
    ...(SAFE ? { hashRate: 0, hashRate_1m: 0, hashRate_10m: 0, hashRate_1h: 0, power: 3.1 } : {}) };
}
function costean() {
  const now = Math.floor(Date.now() / 1000), local = now - 5 * 3600, min = Math.floor((local % 86400) / 60), night = min >= 22 * 60 || min < 7 * 60;
  const nextMin = night ? 7 * 60 : 22 * 60; let next = local - (local % 86400) + nextMin * 60; if (next <= local) next += 86400;
  return { status: { board: '602', activeProfile: prof(), profileSource: 'user', target: PRESETS()[prof()], applied: applied(),
      governor: THROTTLE ? { state: 'throttled', reason: 'chip 68.6C over 68C', throttleSteps: 2, tuneSteps: 0, bestStableSteps: 0, errorPct: 0.41, rejectPct: 0.7, mining: true }
                         : { state: 'hold', reason: 'headroom ok', throttleSteps: 0, tuneSteps: 0, bestStableSteps: 0, errorPct: 0.38, rejectPct: 0.7, mining: true },
      effectiveLimits: { ...CFG.limits }, limitRanges: { chipTemp: [45, 73], vrTemp: [50, 103], power: [10, CFG.limits.psu], psu: [10, 150] }, stockCutoffs: { chipTemp: 75, vrTemp: 105 }, manualRange: [400, 1000, 1000, 1300],
      presets: PRESETS(), profiles: profileList(), envelope: { minMhz: 400, maxMhz: Math.max(1000, Math.round(S.frequency)), stepMhz: 25, maxMv: 1300 },
      schedule: { clockValid: true, profileWindow: -1, lcdWindow: night && CFG.schedule.enabled ? 0 : -1, lcd: night ? 'dim' : 'normal', profile: 'none', localTime: local, dst: false, nextChangeLocal: next },
      zones: ['UTC', 'America/St_Johns', 'America/Halifax', 'America/New_York', 'America/Toronto', 'America/Chicago', 'America/Denver', 'America/Phoenix', 'America/Los_Angeles', 'America/Vancouver', 'Europe/London', 'Europe/Berlin', 'Europe/Helsinki'],
      failover: FAILOVER, split: { enabled: !!S.poolSplitEnabled, active: !!S.poolSplitEnabled, error: 'ok', restartNeeded: false, failover: FAILOVER }, maxPools: 4 }, config: CFG };
}
function rollup() {
  const slot = 900, end = Math.floor(Date.now() / 1000 / slot) * slot, n = 192, start = end - (n - 1) * slot;
  const H = { slotSec: slot, start, ghs: [], watts: [], chipTemp: [], vrTemp: [], jth: [], flags: [] };
  for (let i = 0; i < n; i++) {
    const t = start + i * slot, lm = Math.floor(((t - 5 * 3600) % 86400) / 60), wd = new Date((t - 5 * 3600) * 1000).getUTCDay();
    if (i >= 60 && i < 63) { for (const k of ['ghs', 'watts', 'chipTemp', 'vrTemp', 'jth']) H[k].push(null); H.flags.push(0); continue; }
    const eco = (lm < 360 && wd >= 1 && wd <= 5) || (lm >= 540 && lm < 1020 && wd >= 1 && wd <= 5), thr = i >= 120 && i < 126;
    const g = Math.round((eco ? 855 : thr ? 990 : 1055) + Math.sin(i / 7) * 9 + Math.sin(i * 1.7) * 4);
    const w = +(g / 1000 * (eco ? 14.6 : 15.5) + Math.sin(i / 5) * 0.3).toFixed(1);
    H.ghs.push(g); H.watts.push(w); H.chipTemp.push(+((eco ? 53.5 : thr ? 68.2 : 58.5) + Math.sin(i / 9) * 1.2).toFixed(1)); H.vrTemp.push(+((eco ? 43 : 48) + Math.sin(i / 11)).toFixed(1));
    H.jth.push(+(w / (g / 1000)).toFixed(1)); H.flags.push((thr ? 1 : 0) | (i >= 150 && i < 154 ? 2 : 0) | (eco || lm >= 1320 || lm < 420 ? 4 : 0));
  }
  return H;
}
const scoreboard = [{ difficulty: 1.89e9, job_id: 'ab12', extranonce2: '00000001', ntime: 1760000000, nonce: '00AABBCC', version_bits: '20000000' }, { difficulty: 4.2e7, job_id: 'cd34', extranonce2: '00000002', ntime: 1760000100, nonce: '11223344', version_bits: '20000000' }];
const json = (res, o, code = 200) => { res.writeHead(code, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }); res.end(JSON.stringify(o)); };
const body = req => new Promise(r => { let b = []; req.on('data', c => b.push(c)); req.on('end', () => r(Buffer.concat(b).toString())); });
const TYPES = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.json': 'application/json', '.txt': 'text/plain', '.png': 'image/png' };
const merge = (dst, src) => { for (const [k, v] of Object.entries(src)) { if (v && typeof v === 'object' && !Array.isArray(v) && dst[k] && typeof dst[k] === 'object') merge(dst[k], v); else dst[k] = v; } };
const server = http.createServer(async (req, res) => {
  const u = new URL(req.url, 'http://x'), p = u.pathname, m = req.method;
  if (m === 'OPTIONS') { res.writeHead(204, { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Methods': '*', 'Access-Control-Allow-Headers': '*' }); return res.end(); }
  if (p === '/__writes') return json(res, writes);
  if (p.startsWith('/api/') && Date.now() < downUntil) { req.socket.destroy(); return; }   // "rebooting"
  if ((p === '/api/system/OTA' || p === '/api/system/OTAWWW') && m === 'POST') {
    const chunks = []; for await (const c of req) chunks.push(c); const buf = Buffer.concat(chunks);
    const d = detectUpdate(new Uint8Array(buf.subarray(0, 288)), buf.length, '');
    writes.push({ m, p, body: `<${buf.length} bytes: ${d.kind}${d.version ? ' ' + d.version : ''}>` });
    if (p === '/api/system/OTAWWW') {
      if (buf[0] === 0xE9) return json(res, { message: 'That is a firmware file (esp-miner.bin). Upload it in the Firmware box, not as www.bin. Nothing was changed.' }, 400);
      if (d.kind !== 'www') return json(res, { message: d.error || 'not a www.bin' }, 400);
      downUntil = Date.now() + 4000; return json(res, { message: 'Web UI update complete, rebooting now. The firmware version does not change.' });
    }
    if (d.kind !== 'firmware') return json(res, { message: d.error || 'That is not a firmware file.' }, 400);
    const target = slot === 'ota_0' ? 'ota_1' : 'ota_0';
    json(res, { message: `Firmware ${d.version} written to ${target}, rebooting now.`, version: d.version });
    downUntil = Date.now() + 5000; if (!OTA_NOAPPLY) { FW = d.version; slot = target; } saveNvs(); return;
  }
  if (p.startsWith('/api/')) {
    if (m !== 'GET') {
      const b = await body(req); writes.push({ m, p, q: u.search, body: b.length < 8192 ? b : undefined });
      if (p === '/api/system/restart' && u.searchParams.get('normal') === '1') { SAFE = false; downUntil = Date.now() + 1500; }
      try {
        if (p === '/api/system' && m === 'PATCH') { const o = JSON.parse(b || '{}');
          // same checks as the firmware's check_settings_and_update (an empty slot = URL and user both empty)
          const empty = np => !np.stratumURL && !np.stratumUser;
          if (Array.isArray(o.pools)) for (const np of o.pools) {
            if (empty(np)) { if (np.id === S.primaryPoolIndex) return json(res, 'Wrong API input', 400); continue; }
            if (!np.stratumURL || !np.stratumUser || !Number.isInteger(np.stratumPort) || np.stratumPort < 1 || np.stratumPort > 65535) return json(res, 'Wrong API input', 400); }
          if ('frequency' in o && !(o.frequency >= 1 && o.frequency <= 65535)) return json(res, 'Wrong API input', 400);
          if ('coreVoltage' in o && !(Number.isInteger(o.coreVoltage) && o.coreVoltage >= 1 && o.coreVoltage <= 65535)) return json(res, 'Wrong API input', 400);
          if (Array.isArray(o.pools)) for (const np of o.pools) { if (empty(np)) continue;
            if (np.stratumPassword && np.stratumPassword !== '*****') passwords[np.id] = np.stratumPassword;
            const i = pools.findIndex(x => x.id === np.id); const keep = { ...POOL(np.id, '', 3333, 0), ...np, stratumPassword: '*****' }; if (i >= 0) pools[i] = keep; else pools.push(keep); }
          for (const k of Object.keys(o)) if (k !== 'pools') S[k] = o[k]; saveNvs(); }
        if (p.startsWith('/api/system/pools/') && m === 'DELETE') { const id = +p.split('/').pop(); if (id === S.primaryPoolIndex || id === S.secondaryPoolIndex) return json(res, 'Cannot delete', 400); const i = pools.findIndex(x => x.id === id); if (i >= 0) pools.splice(i, 1); saveNvs(); }
        if (p === '/api/costean' && m === 'PATCH') { const o = JSON.parse(b || '{}'); if (o.autotune?.reset) delete o.autotune.reset;
          if (o.profiles || o.profilesReset) { const before = JSON.stringify(CFG.profiles), bp = CFG.profile; const e = patchProfiles(o);
            if (e) { CFG.profiles = JSON.parse(before); CFG.profile = bp; return json(res, { message: e }, 400); } delete o.profiles; delete o.profilesReset; }
          if (o.profile && !available(o.profile)) return json(res, { message: 'that profile is turned off; turn it on under Edit profiles first' }, 400);
          if (o.schedule?.windows?.some(w => w.profile?.startsWith('user') && !available(w.profile))) return json(res, { message: 'a schedule window uses a Custom profile that is turned off' }, 400);
          if (o.limits) { const l = { ...CFG.limits, ...o.limits }; if (o.limits.psu != null && o.limits.power == null && o.limits.psu !== CFG.limits.psu) l.power = Math.max(10, Math.floor(o.limits.psu * 9 / 10));
            if (!(l.psu >= 10 && l.psu <= 150) || !(l.power >= 10 && l.power <= l.psu) || !(l.chipTemp >= 45 && l.chipTemp <= 73) || !(l.vrTemp >= 50 && l.vrTemp <= 103)) return json(res, 'power limit must be 10 W up to the power supply rating', 400);
            o.limits = l; }
          merge(CFG, o); saveNvs(); }
        if (p === '/api/system/blockFound/dismiss') S.showNewBlock = false;
      } catch { return json(res, { error: 'bad json' }, 400); }
      return json(res, { ok: true });
    }
    if (p === '/api/system/info') return json(res, info());
    if (p === '/api/system/asic') return json(res, { frequencyOptions: [400, 490, 525, 550, 600, 625], voltageOptions: [1000, 1060, 1100, 1150, 1200, 1250], ASICModel: 'BM1370', defaultFrequency: 525, defaultVoltage: 1150 });
    if (p === '/api/system/scoreboard') return json(res, scoreboard);
    if (p === '/api/system/statistics/dashboard') return json(res, { labels: [], datasets: [] });
    if (p === '/api/system/logs') { res.writeHead(200, { 'Content-Type': 'text/plain' }); return res.end(`I (123) Costean OS ${FW} started [DEMO]\nI (900) costean: profile Manual 525 MHz / 1150 mV [DEMO]\n`); }
    if (p === '/api/system/wifi/scan' && SC === 'joining') { writes.push({ m, p }); return json(res, { error: 'scan busy, try again', networks: [] }, 503); }
    if (p === '/api/system/wifi/scan') { await new Promise(r => setTimeout(r, SETUP ? 1600 : 300));
      return json(res, { cached: false, ageMs: 0, networks: SETUP ? [
        { ssid: 'DEMO-Home', rssi: -48, authmode: 3 }, { ssid: 'DEMO-Home', rssi: -63, authmode: 3 }, { ssid: 'DEMO-Home-5G', rssi: -58, authmode: 7 },
        { ssid: 'DEMO-Guest', rssi: -66, authmode: 0 }, { ssid: '', rssi: -60, authmode: 3 }, { ssid: 'DEMO-Neighbour', rssi: -77, authmode: 3 },
        { ssid: 'DEMO-Printer-Direct', rssi: -82, authmode: 4 }, { ssid: 'DEMO-Far-Away', rssi: -89, authmode: 6 }] : [{ ssid: 'DEMO-WIFI', rssi: -52, authmode: 3 }] }); }
    if (p === '/api/costean' || p === '/api/costean/status') return json(res, costean());
    if (p === '/api/costean/history') return json(res, rollup());
    res.writeHead(404); return res.end('not found');
  }
  const f = path.join(ROOT, p === '/' ? 'index.html' : p);
  if (!f.startsWith(ROOT)) { res.writeHead(403); return res.end(); }
  if (fs.existsSync(f + '.gz')) { res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'text/plain', 'Content-Encoding': 'gzip' }); return res.end(fs.readFileSync(f + '.gz')); }
  if (fs.existsSync(f) && fs.statSync(f).isFile()) { res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'text/plain' }); return res.end(fs.readFileSync(f)); }
  res.writeHead(302, { Location: '/' }); res.end();
});
server.listen(PORT, '127.0.0.1', () => console.log(`DEMO mock Bitaxe Gamma 602 (${SC}) on http://127.0.0.1:${PORT} serving ${ROOT}`));
