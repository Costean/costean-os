const headers = { 'Content-Type': 'application/json' };

async function req(path, opts = {}) {
  const r = await fetch(path, { ...opts, headers: { ...headers, ...(opts.headers || {}) } });
  if (!r.ok) {
    const t = await r.text().catch(() => r.statusText);
    throw new Error(t || `${r.status}`);
  }
  const ct = r.headers.get('content-type') || '';
  if (ct.includes('json')) return r.json();
  return r.text();
}

export const api = {
  info: () => req('/api/system/info'),
  asic: () => req('/api/system/asic'),
  scoreboard: () => req('/api/system/scoreboard'),
  statistics: () => req('/api/system/statistics/dashboard').catch(() => null),
  patch: (body) => req('/api/system', { method: 'PATCH', body: JSON.stringify(body) }),
  putPool: (id, body) => req(`/api/system/pools/${id}`, { method: 'PUT', body: JSON.stringify(body) }),
  deletePool: (id) => req(`/api/system/pools/${id}`, { method: 'DELETE' }),
  restart: () => req('/api/system/restart', { method: 'POST' }),
  pause: () => req('/api/system/pause', { method: 'POST' }),
  resume: () => req('/api/system/resume', { method: 'POST' }),
  dismissBlock: () => req('/api/system/blockFound/dismiss', { method: 'POST' }),
  discordTest: () => req('/api/system/discord/test', { method: 'POST' }),
  costean: () => req('/api/costean'),
  costeanHistory: () => req('/api/costean/history'),
  patchCostean: (body) => req('/api/costean', { method: 'PATCH', body: JSON.stringify(body) }),
  wwwVersion: () => fetch('/version.json', { cache: 'no-store' }).then(r => r.ok ? r.json() : null).catch(() => null),
  wifiScan: () => req('/api/system/wifi/scan'),
  logs: () => fetch('/api/system/logs').then(r => r.text()),
  ota: (file, www = false) => fetch(www ? '/api/system/OTAWWW' : '/api/system/OTA', {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: file,
  }).then(async r => {
    if (!r.ok) throw new Error(await r.text());
    return r.json().catch(() => ({ ok: true }));
  }),
};

export function fmtHash(n) {
  const v = Number(n) || 0;
  if (v >= 1000) return (v / 1000).toFixed(2) + ' TH/s';
  return v.toFixed(1) + ' GH/s';
}

export function fmtDiff(n) {
  const v = Number(n) || 0;
  const units = ['', 'K', 'M', 'G', 'T', 'P', 'E'];
  let i = 0; let x = v;
  while (x >= 1000 && i < units.length - 1) { x /= 1000; i++; }
  return (i === 0 ? x.toFixed(0) : x.toFixed(2)) + units[i];
}

export function fmtUptime(s) {
  s = Number(s) || 0;
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  if (d) return `${d}d ${h}h ${m}m`;
  if (h) return `${h}h ${m}m`;
  return `${m}m`;
}
