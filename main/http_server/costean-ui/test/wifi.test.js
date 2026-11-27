import { describe, it, expect } from 'vitest';
import { cleanNetworks, bars, passwordProblem, ssidProblem, isOpen, joinState, resetNotice } from '../src/lib/wifi.js';

describe('cleanNetworks', () => {
  it('dedupes by name keeping the strongest, drops hidden, sorts strongest first', () => {
    const out = cleanNetworks([
      { ssid: 'Home', rssi: -70, authmode: 3 }, { ssid: '', rssi: -30, authmode: 3 }, { ssid: 'Cafe', rssi: -45, authmode: 0 },
      { ssid: 'Home', rssi: -50, authmode: 3 }, { ssid: '   ', rssi: -40, authmode: 3 }, { ssid: 'Far', rssi: -90, authmode: 7 }]);
    expect(out.map(n => n.ssid)).toEqual(['Cafe', 'Home', 'Far']);
    expect(out[1].rssi).toBe(-50);
  });
  it('survives junk', () => { expect(cleanNetworks(null)).toEqual([]); expect(cleanNetworks([null, {}])).toEqual([]); });
});
describe('checks', () => {
  it('bars', () => { expect(bars(-40)).toBe(4); expect(bars(-70)).toBe(2); expect(bars(-95)).toBe(0); expect(bars('x')).toBe(0); });
  it('passwords', () => {
    expect(passwordProblem('', 0)).toBe(''); expect(isOpen(0)).toBe(true);
    expect(passwordProblem('short', 3)).toMatch(/8/); expect(passwordProblem('longenough', 3)).toBe('');
    expect(passwordProblem('x'.repeat(64), 3)).toMatch(/63/); expect(passwordProblem('abcde', 1)).toBe('');
  });
  it('ssid', () => { expect(ssidProblem('')).toBeTruthy(); expect(ssidProblem('a'.repeat(33))).toBeTruthy(); expect(ssidProblem('My Net')).toBe(''); });
});

describe('joinState (costean.6)', () => {
  it('nothing saved is idle', () => {
    expect(joinState({ ssid: '', wifiStatus: 'Setup: choose a Wi-Fi network' }).kind).toBe('idle');
    expect(joinState({}).kind).toBe('idle');
  });
  it('joining and getting an IP are not failures', () => {
    expect(joinState({ ssid: 'Home', wifiStatus: 'Connecting...' }).kind).toBe('joining');
    expect(joinState({ ssid: 'Home', wifiStatus: 'Acquiring IP...' }).kind).toBe('dhcp');
    expect(joinState({ ssid: 'Home', wifiStatus: 'Acquiring IP...', wifiLink: 'dhcp' }).kind).toBe('dhcp');
  });
  it('a classified failure carries the reason and the hint', () => {
    const j = joinState({ ssid: 'Home', wifiStatus: 'Wrong password? (Error 15, retry #2)', wifiFailCode: 'password', wifiFailText: 'Wrong password?', wifiFailHint: 'case-sensitive', wifiFailReason: 15, wifiRetries: 2 });
    expect(j).toMatchObject({ kind: 'failed', code: 'password', text: 'Wrong password?', hint: 'case-sensitive', reason: 15, retries: 2 });
  });
  it('older firmware status text still reads as a failure', () => {
    const j = joinState({ ssid: 'Home', wifiStatus: 'Incorrect password entered (Error 15, retry #3)' });
    expect(j.kind).toBe('failed'); expect(j.code).toBe('password'); expect(j.reason).toBe(15); expect(j.retries).toBe(3);
    expect(joinState({ ssid: 'Home', wifiStatus: 'No AP found (Error 201, retry #1)' }).code).toBe('not_found');
    expect(joinState({ ssid: 'Home', wifiStatus: 'IP Acquire Timeout' }).kind).toBe('failed');
  });
  it('connected wins', () => {
    expect(joinState({ ssid: 'Home', wifiStatus: 'Connected!', wifiLink: 'up' }).kind).toBe('connected');
  });
});

describe('resetNotice (costean.6)', () => {
  it('normal restarts show nothing', () => {
    expect(resetNotice({ lastResetUnexpected: false, safeMode: 0 })).toBeNull();
    expect(resetNotice({})).toBeNull();
  });
  it('a crash is explained, safe mode flagged', () => {
    expect(resetNotice({ lastResetUnexpected: true, lastResetText: 'Crash (panic) while waiting for an IP address', lastResetWifi: 'Acquiring IP...' }))
      .toEqual({ safe: false, text: 'Crash (panic) while waiting for an IP address', wifi: 'Acquiring IP...', count: 0 });
    expect(resetNotice({ safeMode: 1, lastResetUnexpected: true, lastResetReason: 'Task watchdog', quickResets: 3 })).toMatchObject({ safe: true, text: 'Task watchdog', count: 3 });
  });
});
