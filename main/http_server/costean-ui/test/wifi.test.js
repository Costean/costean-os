import { describe, it, expect } from 'vitest';
import { cleanNetworks, bars, passwordProblem, ssidProblem, isOpen } from '../src/lib/wifi.js';

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
