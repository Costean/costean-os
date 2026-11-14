import { api } from './api.js';
import { ask } from './confirm.svelte.js';

export const app = $state({
  page: (typeof location !== 'undefined' && location.hash.slice(2).split('?')[0]) || 'dashboard',
  info: null,
  asic: null,
  scoreboard: [],
  online: false,
  lastOk: 0,
  demo: false,
  toast: null,
  costean: null,     // { status, config } from GET /api/costean
  rollup: null,      // 48 h history from GET /api/costean/history
  wwwVersion: null,
  trend: [],         // in-browser 15 min trend of the live data
  oc: typeof location !== 'undefined' && (location.search.includes('oc') || location.hash.includes('oc')),
});

let timer, histTimer;
export function toast(msg, kind = 'ok') {
  app.toast = { msg, kind };
  setTimeout(() => { if (app.toast?.msg === msg) app.toast = null; }, 4000);
}

export async function refresh() {
  try {
    const [info, asic, board] = await Promise.all([
      api.info(),
      api.asic().catch(() => null),
      api.scoreboard().catch(() => []),
    ]);
    app.info = info;
    app.asic = asic;
    app.scoreboard = Array.isArray(board) ? board : [];
    app.online = true;
    app.lastOk = Date.now();
    app.demo = !!(info && info.demo);
    api.costean().then(c => app.costean = c).catch(() => { });
    const pt = { t: Date.now(), gh: Number(info.hashRate) || 0, chip: Number(info.temp) || null, vr: Number(info.vrTemp) || null, w: Number(info.power) || null };
    app.trend = [...app.trend.slice(-449), pt];
  } catch (e) {
    app.online = false;
  }
}

export async function loadRollup() { try { app.rollup = await api.costeanHistory(); } catch { } }

export function start() {
  refresh();
  loadRollup();
  api.wwwVersion().then(v => app.wwwVersion = v);
  clearInterval(timer); clearInterval(histTimer);
  timer = setInterval(refresh, 2000);
  histTimer = setInterval(loadRollup, 60000);
}

export async function save(body, restartNeeded = false) {
  try {
    await api.patch(body);
    toast(restartNeeded ? 'Saved. Restart the miner to apply.' : 'Saved');
    await refresh();
    return true;
  } catch (e) {
    toast(e.message || 'Save failed', 'err');
    return false;
  }
}

/** Write path for Costean OS profile / limits / auto-tune / schedule settings (PATCH /api/costean). */
export async function applyCostean(patch, label, { danger = false, phrase = '', warn = '' } = {}) {
  const r = await ask(label || 'Apply Costean settings', { detail: JSON.stringify(patch, null, 2), danger, phrase, warn });
  if (!r.ok) return false;
  try { await api.patchCostean(patch); toast('Saved. Applies within 5 seconds.'); app.costean = await api.costean(); return true; }
  catch (e) { toast('Not saved: ' + e.message, 'err'); return false; }
}

/** Confirmed PATCH /api/system (used by the pool split page). */
export async function applySystem(body, label, detail) {
  const r = await ask(label, { detail: detail ?? JSON.stringify(redact(body), null, 2) });
  if (!r.ok) return false;
  return save(body);
}

export async function runAction(label, fn, { danger = false } = {}) {
  const r = await ask(label, { danger }); if (!r.ok) return false;
  try { await fn(); toast(label + ': done'); return true; } catch (e) { toast(label + ' failed: ' + e.message, 'err'); return false; }
}
export const redact = o => JSON.parse(JSON.stringify(o, (k, v) => /pass|token|webhook/i.test(k) && v ? '••••••' : v));
