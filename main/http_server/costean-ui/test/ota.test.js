import { describe, it, expect } from 'vitest';
import fs from 'node:fs';
import { detectUpdate, verifyResult, SLOT } from '../src/lib/ota.js';
import { errorText } from '../src/lib/api.js';

function appHead(version = '1.2.0-costean.5', project = 'esp-miner', chip = 9) {
  const h = new Uint8Array(288); h[0] = 0xE9; h[12] = chip;
  h.set([0x32, 0x54, 0xCD, 0xAB], 32);
  h.set(new TextEncoder().encode(version), 48); h.set(new TextEncoder().encode(project), 80);
  return h;
}
const BUILD = new URL('../../../../build/', import.meta.url).pathname;
const head = f => new Uint8Array(fs.readFileSync(f).subarray(0, 288));

describe('update file detection (mirrors main/costean/ota_check.c)', () => {
  it('recognises esp-miner.bin and reads the version inside it', () => {
    expect(detectUpdate(appHead(), 2_200_000, 'esp-miner.bin')).toMatchObject({ kind: 'firmware', version: '1.2.0-costean.5', target: '/api/system/OTA' });
  });
  it('refuses the factory image, other chips, other projects and tiny files', () => {
    expect(detectUpdate(appHead(), 15_802_368, 'esp-miner-factory.bin').kind).toBe('factory');
    const boot = appHead(); boot[32] = 0; expect(detectUpdate(boot, 2_000_000).kind).toBe('factory');
    expect(detectUpdate(appHead('x', 'esp-miner', 0), 2_000_000).kind).toBe('bad');
    expect(detectUpdate(appHead('x', 'other'), 2_000_000).kind).toBe('bad');
    expect(detectUpdate(appHead(), 1000).kind).toBe('bad');
    expect(detectUpdate(appHead(), 0).kind).toBe('bad');
  });
  it('treats a non-firmware .bin up to 3 MB as www.bin', () => {
    const w = new Uint8Array(288).fill(0xff);
    expect(detectUpdate(w, 3 * 1024 * 1024, 'www.bin').kind).toBe('www');
    expect(detectUpdate(w, 3 * 1024 * 1024 + 1, 'www.bin').kind).toBe('bad');
    expect(detectUpdate(w, 100_000, 'notes.txt').kind).toBe('bad');
  });
  it.runIf(fs.existsSync(BUILD + 'esp-miner.bin'))('works on the real build outputs', () => {
    const fw = BUILD + 'esp-miner.bin', www = BUILD + 'www.bin';
    const d = detectUpdate(head(fw), fs.statSync(fw).size, 'esp-miner.bin');
    expect(d.kind).toBe('firmware'); expect(d.version).toMatch(/^1\.2\.0-costean\.\d+$/); expect(fs.statSync(fw).size).toBeLessThan(SLOT);
    if (fs.existsSync(www)) expect(detectUpdate(head(www), fs.statSync(www).size, 'www.bin').kind).toBe('www');
  });
});

describe('after-update check', () => {
  const p = { kind: 'firmware', expected: '1.2.0-costean.5', previous: '1.2.0-costean.3' };
  it('reports success only when the miner runs the new version', () => {
    expect(verifyResult(p, { version: '1.2.0-costean.5', runningPartition: 'ota_1' })).toMatchObject({ ok: true });
    const r = verifyResult(p, { version: '1.2.0-costean.3' });
    expect(r.ok).toBe(false); expect(r.text).toContain('did NOT take'); expect(r.text).toContain('1.2.0-costean.3');
  });
  it('a www.bin never claims a firmware change', () => {
    expect(verifyResult({ kind: 'www' }, { version: '1.2.0-costean.3' }).text).toContain('firmware is still 1.2.0-costean.3');
  });
});

describe('error text from the firmware', () => {
  it('shows the message of a JSON error, or the plain text', async () => {
    expect(await errorText(new Response('{"message":"That is a firmware file (esp-miner.bin)."}', { status: 400 }))).toBe('That is a firmware file (esp-miner.bin).');
    expect(await errorText(new Response('Wrong API input', { status: 400 }))).toBe('Wrong API input');
  });
});
