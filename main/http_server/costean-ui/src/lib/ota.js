// Costean OS: recognise an update file in the browser before it is sent, and check after the
// reboot that the miner really runs the new firmware. Mirrors main/costean/ota_check.c.
export const SLOT = 4 * 1024 * 1024;   // ota_0 / ota_1
export const WWW = 3 * 1024 * 1024;    // www partition
const DESC = 32;

function cstr(b, off, len) {
  let s = '';
  for (let i = 0; i < len && b[off + i]; i++) s += String.fromCharCode(b[off + i]);
  return s;
}

/** head: Uint8Array with the first 288+ bytes; size: file size; name: file name.
 *  Returns { kind: 'firmware' | 'www' | 'factory' | 'bad', version?, error?, target? } */
export function detectUpdate(head, size, name = '') {
  if (!size) return { kind: 'bad', error: 'The file is empty.' };
  const h = head || new Uint8Array(0);
  if (h[0] === 0xE9) {
    const magic = h.length >= DESC + 4 ? (h[DESC] | (h[DESC + 1] << 8) | (h[DESC + 2] << 16) | (h[DESC + 3] << 24)) >>> 0 : 0;
    if (magic !== 0xABCD5432 || size > SLOT)
      return { kind: 'factory', error: 'This is a factory image (it includes the bootloader). It cannot be installed here: flash it at 0x0 over USB, or upload esp-miner.bin instead.' };
    const chip = h[12] | (h[13] << 8);
    if (chip !== 9) return { kind: 'bad', error: 'This firmware is for a different chip (not ESP32-S3).' };
    const project = cstr(h, DESC + 48, 32);
    if (project !== 'esp-miner') return { kind: 'bad', error: `This firmware is "${project || 'unknown'}", not ESP-Miner / Costean OS.` };
    if (size < 65536) return { kind: 'bad', error: 'This file is too small to be firmware.' };
    return { kind: 'firmware', version: cstr(h, DESC + 16, 32), target: '/api/system/OTA' };
  }
  if (size > WWW) return { kind: 'bad', error: 'This file is not firmware and too big to be www.bin.' };
  if (size < 4096) return { kind: 'bad', error: 'This file is too small to be an update.' };
  if (name && !/\.bin$/i.test(name)) return { kind: 'bad', error: 'Pick esp-miner.bin (firmware) or www.bin (web UI).' };
  return { kind: 'www', target: '/api/system/OTAWWW' };
}

export async function readHead(file) {
  return new Uint8Array(await file.slice(0, 288).arrayBuffer());
}

/** After an upload: compare what the miner reports with what was installed. */
export function verifyResult(pending, info) {
  if (!pending || !info) return null;
  const now = info.version || '';
  if (pending.kind === 'firmware') {
    if (now === pending.expected) return { ok: true, text: `Updated: the miner now runs ${now}${info.runningPartition ? ` (slot ${info.runningPartition})` : ''}.` };
    return { ok: false, text: `The update did NOT take: the miner still runs ${now || 'an unknown version'}, not ${pending.expected}.` };
  }
  return { ok: true, text: `Web UI written. The firmware is still ${now}.` };
}
