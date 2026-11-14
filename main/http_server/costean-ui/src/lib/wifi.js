// Costean OS setup helpers: turn a raw /api/system/wifi/scan answer into a clean list for the network picker.
// authmode values follow ESP-IDF wifi_auth_mode_t (0 = open, 1 = WEP, 2+ = WPA/WPA2/WPA3 variants).

export const isOpen = (authmode) => Number(authmode) === 0;
export const isWep = (authmode) => Number(authmode) === 1;

/** 0..4 signal bars from RSSI in dBm. */
export function bars(rssi) {
  const r = Number(rssi);
  if (!Number.isFinite(r)) return 0;
  if (r >= -55) return 4;
  if (r >= -65) return 3;
  if (r >= -75) return 2;
  if (r >= -85) return 1;
  return 0;
}

export function signalLabel(rssi) {
  return ['Very weak', 'Weak', 'Fair', 'Good', 'Excellent'][bars(rssi)];
}

export function securityLabel(authmode) {
  const a = Number(authmode);
  return ({ 0: 'Open', 1: 'WEP', 2: 'WPA', 3: 'WPA2', 4: 'WPA/WPA2', 5: 'WPA2 Enterprise', 6: 'WPA3', 7: 'WPA2/WPA3' })[a] || 'Secured';
}

/** One row per network name, strongest signal kept, hidden names dropped, strongest first. */
export function cleanNetworks(list) {
  const best = new Map();
  for (const n of Array.isArray(list) ? list : []) {
    const ssid = typeof n?.ssid === 'string' ? n.ssid : '';
    if (!ssid.trim()) continue;
    const rssi = Number(n.rssi);
    const prev = best.get(ssid);
    if (!prev || rssi > prev.rssi) best.set(ssid, { ssid, rssi: Number.isFinite(rssi) ? rssi : -100, authmode: Number(n.authmode) || 0 });
  }
  return [...best.values()].sort((a, b) => b.rssi - a.rssi || a.ssid.localeCompare(b.ssid));
}

/** Returns an error message, or '' when the password can be saved for this network. */
export function passwordProblem(pass, authmode) {
  const p = pass ?? '';
  if (isOpen(authmode)) return '';
  if (isWep(authmode)) return [5, 10, 13, 26].includes(p.length) ? '' : 'WEP keys are 5 or 13 characters (or 10 / 26 hex digits).';
  if (p.length < 8) return 'Wi-Fi passwords are at least 8 characters.';
  if (p.length > 63) return 'Wi-Fi passwords are at most 63 characters.';
  return '';
}

/** Network names are 1 to 32 bytes (UTF-8). */
export function ssidProblem(ssid) {
  const s = ssid ?? '';
  if (!s.trim()) return 'Enter the network name.';
  if (new TextEncoder().encode(s).length > 32) return 'Network names are at most 32 bytes.';
  return '';
}
