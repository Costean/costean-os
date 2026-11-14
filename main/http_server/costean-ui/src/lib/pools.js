// Costean OS pool form helpers (pure, unit tested). Field names match bitaxeorg ESP-Miner v2.15.4
// PATCH /api/system { pools: [{ id, stratumURL, stratumPort, stratumUser, stratumPassword, ... }] }.

export const SLOTS = 4;

/** "stratum+tcp://host:3333" | "host:3333" | "host" -> { host, port, tls } (port/tls null when not given). */
export function parseUrl(input) {
  let s = String(input ?? '').trim();
  let tls = null;
  const m = /^([a-z0-9+.-]+):\/\//i.exec(s);
  if (m) { const sch = m[1].toLowerCase(); tls = sch.includes('ssl') || sch.includes('tls') ? 1 : 0; s = s.slice(m[0].length); }
  s = s.replace(/\/+$/, '');
  let port = null;
  const i = s.lastIndexOf(':');
  if (i > 0 && /^\d+$/.test(s.slice(i + 1))) { port = Number(s.slice(i + 1)); s = s.slice(0, i); }
  return { host: s, port, tls };
}

/** "bc1q...xyz.rig1" -> { address: "bc1q...xyz", worker: "rig1" }. Bitcoin addresses never contain a dot. */
export function splitUser(user) {
  const u = String(user ?? '');
  const i = u.indexOf('.');
  return i < 0 ? { address: u, worker: '' } : { address: u.slice(0, i), worker: u.slice(i + 1) };
}
export function joinUser(address, worker) {
  const a = String(address ?? '').trim(), w = String(worker ?? '').trim();
  return a && w ? `${a}.${w}` : a;
}

/** Form rows (one per slot) from /api/system/info. */
export function formFromInfo(info) {
  const src = Array.isArray(info?.pools) ? info.pools : [];
  return Array.from({ length: SLOTS }, (_, id) => {
    const p = src.find(x => x.id === id);
    const { address, worker } = splitUser(p?.stratumUser);
    return { id, url: p?.stratumURL || '', port: p?.stratumPort || 3333, address, worker, password: '',
      protocol: p?.stratumProtocol || 'SV1', splitPercent: p?.splitPercent ?? 0, existed: !!p?.stratumURL };
  });
}

export const isFilled = r => !!(String(r.url || '').trim() || String(r.address || '').trim());

/** Returns an error string or null. */
export function validateForm(rows, { primary = 0 } = {}) {
  for (const r of rows) {
    if (!isFilled(r)) { if (r.id === primary) return `Pool ${r.id + 1} is the main pool: enter its URL and your address.`; continue; }
    const { host, port } = parseUrl(r.url);
    if (!host) return `Pool ${r.id + 1}: enter the pool URL.`;
    if (/\s/.test(host)) return `Pool ${r.id + 1}: the URL can't contain spaces.`;
    const pt = port ?? Number(r.port);
    if (!Number.isInteger(pt) || pt < 1 || pt > 65535) return `Pool ${r.id + 1}: port must be 1-65535.`;
    if (!String(r.address || '').trim()) return `Pool ${r.id + 1}: enter your wallet address or pool username.`;
    if (joinUser(r.address, r.worker).length > 255) return `Pool ${r.id + 1}: address + worker is too long.`;
    if (/\s/.test(joinUser(r.address, r.worker))) return `Pool ${r.id + 1}: address and worker can't contain spaces.`;
  }
  return null;
}

/** PATCH body. Sends only filled slots, each as the whole stored record with the edited fields
 *  (TLS, difficulty, certificate... are sent back unchanged so they are not reset). */
export function buildPoolsBody(info, rows, splitOn) {
  const src = Array.isArray(info?.pools) ? info.pools : [];
  const pools = rows.filter(isFilled).map(r => {
    const orig = src.find(x => x.id === r.id) || {};
    const { host, port, tls } = parseUrl(r.url);
    const o = { ...orig, id: r.id, stratumURL: host, stratumPort: port ?? (Number(r.port) || 3333), stratumUser: joinUser(r.address, r.worker),
      stratumPassword: r.password ? r.password : (orig.stratumURL ? '*****' : 'x'), stratumProtocol: r.protocol || 'SV1', splitPercent: Number(r.splitPercent) || 0 };
    if (tls != null) o.stratumTLS = tls;
    return o;
  });
  return { poolSplitEnabled: !!splitOn, pools };
}
/** Slots the user emptied that held a pool before (cleared with DELETE /api/system/pools/{id}). */
export const clearedSlots = rows => rows.filter(r => r.existed && !isFilled(r)).map(r => r.id);

/** Compare what was sent with what the miner reports back. [] = everything stuck. */
export function verifyPools(info, sent) {
  const src = Array.isArray(info?.pools) ? info.pools : [];
  const bad = [];
  for (const s of sent) {
    const p = src.find(x => x.id === s.id);
    if (!p || p.stratumURL !== s.stratumURL || Number(p.stratumPort) !== Number(s.stratumPort) || p.stratumUser !== s.stratumUser) bad.push(s.id);
  }
  return bad;
}
