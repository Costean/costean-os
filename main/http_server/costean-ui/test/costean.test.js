import { describe, it, expect } from 'vitest';
import * as C from '../src/lib/costean.js';
describe('schedule helpers', () => {
  it('formats and parses times', () => { expect(C.hhmm(0)).toBe('00:00'); expect(C.hhmm(1439)).toBe('23:59'); expect(C.parseHHMM('7:05')).toBe(425); expect(C.parseHHMM('24:00')).toBe(null); expect(C.parseHHMM('x')).toBe(null); });
  it('labels day masks', () => { expect(C.daysLabel(0x7f)).toBe('Every day'); expect(C.daysLabel(0x3e)).toBe('Weekdays'); expect(C.daysLabel(0x41)).toBe('Weekends'); expect(C.daysLabel(0b10)).toBe('Mon'); expect(C.toggleDay(0x7f, 0)).toBe(0x7e); });
  it('validates like the firmware', () => {
    const w = C.blankWindow(); expect(C.validateWindow(w)).toBe(null);
    expect(C.validateWindow({ ...w, days: 0 })).toMatch(/day/);
    expect(C.validateWindow({ ...w, profile: 'none', lcd: 'normal' })).toMatch(/nothing/);
    expect(C.validateWindow({ ...w, start: 1440 })).toMatch(/times/);
    expect(C.validateSchedule(Array(9).fill(w))).toMatch(/8/);
    expect(C.validateWindow({ ...w, start: 600, end: 600 })).toBe(null); expect(C.windowMinutes({ start: 600, end: 600 })).toBe(1440);
  });
  it('overnight windows belong to their start day', () => {
    const w = { on: true, days: 1 << 5, start: 22 * 60, end: 7 * 60 }; // Friday night
    expect(C.activeAt(w, 5, 23 * 60)).toBe(true); expect(C.activeAt(w, 6, 6 * 60)).toBe(true);
    expect(C.activeAt(w, 6, 23 * 60)).toBe(false); expect(C.activeAt(w, 5, 6 * 60)).toBe(false);
    expect(C.activeAt({ ...w, on: false }, 5, 23 * 60)).toBe(false);
  });
  it('week grid: lowest power profile and darkest screen win', () => {
    const g = C.weekGrid([{ on: true, days: 0x7f, start: 0, end: 360, profile: 'turbo', lcd: 'dim' }, { on: true, days: 0x7f, start: 120, end: 240, profile: 'eco', lcd: 'off' }]);
    expect(g[1][0]).toEqual({ profile: 'turbo', lcd: 'dim' }); expect(g[1][2]).toEqual({ profile: 'eco', lcd: 'off' }); expect(g[1][12]).toBe(null);
  });
});
describe('pool split', () => {
  it('largest remainder sums to 100', () => { expect(C.largestRemainder([33.3, 33.3, 33.4])).toEqual([33, 33, 34]); expect(C.largestRemainder([100 / 3, 100 / 3, 100 / 3]).reduce((a, b) => a + b)).toBe(100); });
  it('failover moves a dead pool share to the others in proportion', () => {
    expect(C.effectiveSplit([70, 30], [true, true])).toEqual([70, 30]);
    expect(C.effectiveSplit([70, 30], [false, true])).toEqual([0, 100]);
    expect(C.effectiveSplit([50, 30, 20, 0], [true, false, true, true])).toEqual([71, 0, 29, 0]);
    expect(C.effectiveSplit([100, 0], [false, true])).toEqual([0, 100]);
    expect(C.effectiveSplit([60, 40], [false, false])).toEqual([0, 0]);
  });
  it('builds Bitaxe pool rows from info.pools + poolSplitStats', () => {
    const info = { pools: [
      { id: 0, stratumURL: 'stratum+tcp://a.example', stratumPort: 3333, splitPercent: 70 },
      { id: 1, stratumURL: 'b.example', stratumPort: 3334, splitPercent: 30 },
      { id: 2, stratumURL: 'c.example', stratumPort: 3333, splitPercent: 0 }],
      poolSplitStats: [{ id: 0, connected: true, healthy: true, effectivePercent: 100, sharesAccepted: 99, sharesRejected: 1, jobShare: 0.99, creditedGhs: 1100 },
        { id: 1, connected: false, healthy: false, effectivePercent: 0 }, { id: 2, connected: true, healthy: true, backup: true, effectivePercent: 0 }] };
    const r = C.poolRows(info);
    expect(r.map(x => x.host)).toEqual(['a.example', 'b.example', 'c.example']);
    expect(r[0]).toMatchObject({ weight: 70, effective: 100, healthy: true, accepted: 99, credited: 1100, backup: false });
    expect(r[0].rej).toBeCloseTo(1);
    expect(r[1].healthy).toBe(false);
    expect(r[2].backup).toBe(true);
  });
  it('validates split like the firmware', () => {
    expect(C.validateSplit([70, 30, 0], true)).toBeNull();
    expect(C.validateSplit([25, 25, 25, 25], true)).toBeNull();
    expect(C.validateSplit([100, 0], true)).toMatch(/two pools/);
    expect(C.validateSplit([60, 30], true)).toMatch(/90/);
    expect(C.validateSplit([20, 20, 20, 20, 20], true)).toMatch(/at most 4/);
    expect(C.validateSplit([100, 0], false)).toBeNull();
    expect(C.evenSplit(3)).toEqual([34, 33, 33]);
  });
  it('sends whole pool records with the masked password', () => {
    const info = { pools: [{ id: 0, stratumURL: 'a.example', stratumPort: 3333, stratumUser: 'u', stratumTLS: 1, stratumPassword: 'x', splitPercent: 100 }, { id: 3, stratumURL: 'b.example', stratumPort: 1, stratumUser: 'u', splitPercent: 0 }] };
    const b = C.splitBody(info, [60, 40], true);
    expect(b.poolSplitEnabled).toBe(1);
    expect(b.pools).toEqual([{ id: 0, stratumURL: 'a.example', stratumPort: 3333, stratumUser: 'u', stratumTLS: 1, stratumPassword: '*****', splitPercent: 60 },
      { id: 3, stratumURL: 'b.example', stratumPort: 1, stratumUser: 'u', stratumPassword: '*****', splitPercent: 40 }]);
  });
});
describe('rollup history', () => {
  it('decodes points, gaps and flags', () => {
    const { points, gaps } = C.decodeRollup({ slotSec: 900, start: 1000, ghs: [11000, null, 9000], watts: [180, null, 150], chipTemp: [60, null, 58], vrTemp: [64, null, 62], jth: [16, null, 17], flags: [1, 0, 6] });
    expect(points.length).toBe(3); expect(gaps).toBe(1); expect(points[1].t).toBe(1000e3 + 900e3); expect(points[0].ths).toBe(11); expect(points[0].throttled).toBe(true); expect(points[2].failover && points[2].scheduled).toBe(true);
    const s = C.rollupSummary(points); expect(s.hours).toBe(0.5); expect(s.avgThs).toBe(10); expect(s.kwh).toBeCloseTo(0.0825); expect(s.throttledH).toBe(0.25);
  });
  it('handles empty input', () => { expect(C.decodeRollup(null).points).toEqual([]); expect(C.rollupSummary([])).toBe(null); });
  it('governor text', () => {
    expect(C.governorText({ governor: { mining: true, state: 'throttled', reason: 'chip 69C', throttleSteps: 2 }, envelope: { stepMhz: 25 } }).text).toMatch(/50 MHz/);
    expect(C.governorText({ governor: { mining: false } }).level).toBe('mute');
  });
});

// ---- costean.5: editable profiles
import { profileChoices, profileLabel, setProfileNames, validateProfileEdit, profilesPatch, aboveStock } from '../src/lib/costean.js';
describe('editable profiles', () => {
  const status = { profiles: [
    { id: 'eco', name: 'Quiet', defaultName: 'Eco', mhz: 600, mv: 1180, edited: true, enabled: true },
    { id: 'stock', name: 'Stock', defaultName: 'Stock', mhz: 525, mv: 1150, edited: false, enabled: true },
    { id: 'turbo', name: 'Turbo', defaultName: 'Turbo', mhz: 575, mv: 1180, edited: false, enabled: true },
    { id: 'custom', name: 'Manual', defaultName: 'Manual', mhz: 950, mv: 1250, edited: false, enabled: true },
    { id: 'user1', name: 'Night', defaultName: 'Custom 1', mhz: 700, mv: 1200, edited: true, enabled: true },
    { id: 'user2', name: 'Custom 2', defaultName: 'Custom 2', mhz: 525, mv: 1150, edited: false, enabled: false }] };
  it('lists the built-in profiles with their names, then the turned-on Custom ones', () => {
    const c = profileChoices(status);
    expect(c.map(p => p.id)).toEqual(['eco', 'stock', 'turbo', 'custom', 'user1']);
    expect(c[0].label).toBe('Quiet'); expect(c[4].label).toBe('Night');
  });
  it('labels follow the names saved on the miner', () => {
    setProfileNames(status.profiles); expect(profileLabel('eco')).toBe('Quiet'); expect(profileLabel('user1')).toBe('Night');
    setProfileNames([]); expect(profileLabel('eco')).toBe('Eco'); expect(profileLabel('user2')).toBe('Custom 2');
  });
  it('checks the same ranges as the firmware', () => {
    expect(validateProfileEdit({ mhz: 400, mv: 1000, name: '' })).toBeNull();
    expect(validateProfileEdit({ mhz: 1000, mv: 1300, name: 'Max 950' })).toBeNull();
    expect(validateProfileEdit({ mhz: 399, mv: 1150 })).toMatch(/frequency/);
    expect(validateProfileEdit({ mhz: 1001, mv: 1150 })).toMatch(/frequency/);
    expect(validateProfileEdit({ mhz: 600, mv: 1301 })).toMatch(/voltage/);
    expect(validateProfileEdit({ mhz: 600, mv: 1200, name: 'thirteen-char' })).toMatch(/12/);
    expect(validateProfileEdit({ mhz: 600, mv: 1200, name: 'a"b' })).toMatch(/name/);
  });
  it('sends only what changed, and clears a name set back to the default', () => {
    const rows = [
      { id: 'eco', name: 'Eco', mhz: 650, mv: 1180 },
      { id: 'stock', name: 'Stock', mhz: 525, mv: 1150 },
      { id: 'turbo', name: 'Fast', mhz: 575, mv: 1180 },
      { id: 'user2', name: 'Custom 2', mhz: 525, mv: 1150, enabled: true }];
    expect(profilesPatch(rows, status)).toEqual({ profiles: { eco: { mhz: 650, mv: 1180, name: '' }, turbo: { name: 'Fast' }, user2: { enabled: true } } });
    expect(profilesPatch([{ id: 'stock', name: 'Stock', mhz: 525, mv: 1150 }], status)).toBeNull();
  });
  it('anything above 525 MHz / 1150 mV counts as above stock', () => {
    expect(aboveStock({ mhz: 525, mv: 1150 })).toBe(false); expect(aboveStock({ mhz: 600, mv: 1100 })).toBe(true); expect(aboveStock({ mhz: 500, mv: 1200 })).toBe(true);
  });
});
