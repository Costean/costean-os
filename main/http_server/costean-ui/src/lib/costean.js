// Costean OS 1.2 helpers: profiles, schedules, pool split and rollup history. Pure functions, unit tested.
export const PROFILES = [
  { id: 'eco', label: 'Eco', blurb: 'Lower clock and voltage. Quieter, cooler, best J/TH.' },
  { id: 'stock', label: 'Stock', blurb: 'The board maker\'s default operating point.' },
  { id: 'turbo', label: 'Turbo', blurb: 'A modest step above stock. Still inside the safety limits.' },
  { id: 'custom', label: 'Manual', blurb: 'Your own frequency and voltage from Mining & Tuning (the default).' },
];
export const profileLabel = id => PROFILES.find(p => p.id === id)?.label || (id === 'none' ? 'No change' : id || '-');
export const LCD_MODES = [['normal', 'Normal'], ['dim', 'Dim'], ['off', 'Screen off']];
export const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
export const ALL_DAYS = 0x7f, WEEKDAYS = 0x3e, WEEKEND = 0x41;

export const hhmm = m => `${String(Math.floor(m / 60) % 24).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;
export function parseHHMM(s) {
  const m = /^\s*(\d{1,2}):(\d{2})\s*$/.exec(s || ''); if (!m) return null;
  const h = +m[1], mi = +m[2]; return h < 24 && mi < 60 ? h * 60 + mi : null;
}
export function daysLabel(mask) {
  mask &= 0x7f;
  if (mask === ALL_DAYS) return 'Every day'; if (mask === WEEKDAYS) return 'Weekdays'; if (mask === WEEKEND) return 'Weekends'; if (!mask) return 'Never';
  return DAYS.filter((_, i) => mask & (1 << i)).join(', ');
}
export const toggleDay = (mask, i) => (mask ^ (1 << i)) & 0x7f;
export const windowMinutes = w => (w.end > w.start ? w.end - w.start : 1440 - w.start + w.end);
export const allDay = w => w.start === w.end;
export const crossesMidnight = w => w.end <= w.start;

/** Mirrors firmware Schedule::validate. Returns an error string or null. */
export function validateWindow(w) {
  if (!Number.isInteger(w.start) || !Number.isInteger(w.end) || w.start < 0 || w.start >= 1440 || w.end < 0 || w.end >= 1440) return 'times must be 00:00-23:59';
  if (!(w.days & 0x7f)) return 'pick at least one day';
  if ((!w.profile || w.profile === 'none') && (!w.lcd || w.lcd === 'normal')) return 'window changes nothing: pick a profile or a screen mode';
  if ((w.name || '').length > 19) return 'name is at most 19 characters';
  return null;
}
export function validateSchedule(windows) {
  if (windows.length > 8) return 'at most 8 windows';
  for (let i = 0; i < windows.length; i++) { const e = validateWindow(windows[i]); if (e) return `Window ${i + 1}: ${e}`; }
  return null;
}
/** Is the window active at local weekday (0=Sun) and minute? Overnight windows belong to the day they start. */
export function activeAt(w, wd, min) {
  if (!w.on) return false; const d = b => !!(w.days & (1 << ((b + 7) % 7)));
  if (w.end > w.start) return d(wd) && min >= w.start && min < w.end;
  return (d(wd) && min >= w.start) || (d(wd - 1) && min < w.end);
}
/** 7x24 coverage grid for the week preview: cell = {profile, lcd} of the winning window or null. */
const RANK = { eco: 1, stock: 2, custom: 3, turbo: 4 }, LRANK = { off: 2, dim: 1, normal: 0 };
export function weekGrid(windows) {
  const g = [];
  for (let d = 0; d < 7; d++) { const row = []; for (let h = 0; h < 24; h++) {
    let p = null, l = null; for (const w of windows) if (activeAt(w, d, h * 60 + 30)) {
      if (w.profile && w.profile !== 'none' && (p == null || RANK[w.profile] < RANK[p])) p = w.profile;
      if (w.lcd && w.lcd !== 'normal' && (l == null || LRANK[w.lcd] > LRANK[l])) l = w.lcd;
    } row.push(p || l ? { profile: p, lcd: l } : null); } g.push(row); }
  return g;
}
export const blankWindow = () => ({ on: true, days: ALL_DAYS, start: 23 * 60, end: 6 * 60, profile: 'eco', lcd: 'dim', name: 'Quiet night' });

/** Mirrors firmware PoolSplit effective weights: dead pools give their share to healthy ones in proportion. */
export function effectiveSplit(weights, healthy) {
  const n = weights.length; const live = weights.map((w, i) => healthy[i] ? w : 0); const sum = live.reduce((a, b) => a + b, 0);
  if (sum > 0) return largestRemainder(live.map(w => w * 100 / sum));
  const backups = healthy.map((h, i) => h ? i : -1).filter(i => i >= 0);
  if (!backups.length) return Array(n).fill(0);
  const e = Array(n).fill(0); backups.forEach(i => e[i] = 100 / backups.length); return largestRemainder(e);
}
export function largestRemainder(v) {
  const f = v.map(Math.floor); let rest = 100 - f.reduce((a, b) => a + b, 0);
  if (v.every(x => x === 0)) return f;
  const order = v.map((x, i) => [x - Math.floor(x), i]).sort((a, b) => b[0] - a[0] || a[1] - b[1]);
  for (let k = 0; rest > 0 && k < order.length; k++, rest--) f[order[k][1]]++;
  return f;
}
export const rejectPct = (acc, rej) => (acc + rej) ? rej * 100 / (acc + rej) : 0;
export const shortHost = u => String(u || '').replace(/^[a-z+]+:\/\//i, '').replace(/[:/].*$/, '');
/** Pool rows for the split page from Bitaxe /api/system/info (pools[] + poolSplitStats[]). Up to 4 weighted pools, 0 % pools are backups. */
export function poolRows(info) {
  const pools = (info?.pools || []).filter(p => p.stratumURL);
  const stats = info?.poolSplitStats || [];
  return pools.map((p, i) => {
    const s = stats.find(x => x.id === p.id) || {};
    const acc = s.sharesAccepted || 0, rej = s.sharesRejected || 0;
    return {
      i, id: p.id, host: shortHost(p.stratumURL), url: p.stratumURL, port: p.stratumPort, user: p.stratumUser, configured: true,
      weight: p.splitPercent || 0, backup: s.backup ?? !(p.splitPercent > 0),
      connected: !!s.connected, healthy: s.healthy ?? !!s.connected, inSplit: s.id != null,
      effective: s.effectivePercent ?? null, jobShare: s.jobShare ?? null, credited: s.creditedGhs ?? null,
      accepted: acc, rejected: rej, rej: rejectPct(acc, rej), diff: s.difficulty,
    };
  });
}
/** Mirrors firmware validation: when enabled, 2-4 pools with weight > 0 that sum to exactly 100. */
export function validateSplit(weights, enabled) {
  if (!enabled) return null;
  const w = weights.filter(x => x > 0);
  if (w.length < 2) return 'give at least two pools a share above 0 %';
  if (w.length > 4) return 'at most 4 pools can share the hashrate (others stay at 0 % as backups)';
  if (weights.some(x => !Number.isInteger(x) || x < 0 || x > 100)) return 'shares are whole numbers from 0 to 100';
  const sum = w.reduce((a, b) => a + b, 0);
  return sum === 100 ? null : `shares add up to ${sum} %, they must add up to 100 %`;
}
/** Even split across n weighted pools (largest remainder, so it always sums to 100). */
export const evenSplit = n => largestRemainder(Array(n).fill(100 / n));
/** PATCH /api/system body for the split page. Each pool object is sent whole (as read from the miner,
 *  password masked as ***** so the firmware keeps the stored one) because the firmware rewrites the full record. */
export function splitBody(info, weights, enabled) {
  const pools = (info?.pools || []).filter(p => p.stratumURL).map((p, i) => ({ ...p, stratumPassword: '*****', splitPercent: weights[i] ?? 0 }));
  return { poolSplitEnabled: enabled ? 1 : 0, pools };
}

/** Rollup history (15-minute points that survive a reboot) -> chart series. */
export function decodeRollup(h) {
  if (!h || !Array.isArray(h.ghs)) return { points: [], gaps: 0 };
  const step = (h.slotSec || 900) * 1000, t0 = (h.start || 0) * 1000; let gaps = 0;
  const points = h.ghs.map((g, i) => { if (g == null) gaps++; const fl = h.flags?.[i] || 0;
    return { t: t0 + i * step, ths: g == null ? null : g / 1000, w: h.watts?.[i] ?? null, chip: h.chipTemp?.[i] ?? null, vr: h.vrTemp?.[i] ?? null, jth: h.jth?.[i] ?? null,
      throttled: !!(fl & 1), failover: !!(fl & 2), scheduled: !!(fl & 4), tuned: !!(fl & 8) }; });
  return { points, gaps };
}
export function rollupSummary(points) {
  const ok = points.filter(p => p.ths != null); if (!ok.length) return null;
  const avg = k => ok.reduce((a, p) => a + (p[k] ?? 0), 0) / ok.filter(p => p[k] != null).length || 0;
  return { hours: ok.length / 4, avgThs: avg('ths'), avgW: avg('w'), avgJth: avg('jth'), maxChip: Math.max(...ok.map(p => p.chip ?? 0)),
    kwh: ok.reduce((a, p) => a + (p.w || 0) * 0.25, 0) / 1000, throttledH: ok.filter(p => p.throttled).length / 4, failoverH: ok.filter(p => p.failover).length / 4 };
}
/** Plain-language governor state. */
export function governorText(s) {
  if (!s) return { level: 'mute', text: 'Waiting for the miner' };
  const g = s.governor || {};
  if (!g.mining) return { level: 'mute', text: 'Not mining right now' };
  switch (g.state) {
    case 'throttled': return { level: 'warn', text: `Throttling: ${g.reason || 'over a limit'}. Clock lowered ${g.throttleSteps * (s.envelope?.stepMhz || 25)} MHz.` };
    case 'backed-off': return { level: 'warn', text: `Auto-tune backed off: ${g.reason || 'errors or heat'}. It will not retry that step.` };
    case 'tuning': return { level: 'ok', text: `Auto-tune testing +${g.tuneSteps * (s.envelope?.stepMhz || 25)} MHz.` };
    default: return { level: 'ok', text: g.reason ? g.reason[0].toUpperCase() + g.reason.slice(1) : 'Holding target. Headroom OK.' };
  }
}
