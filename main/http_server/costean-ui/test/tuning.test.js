import { describe, it, expect } from 'vitest';
import { checkManual, isHigh, powerForPsu, PSU_PRESETS, estThs } from '../src/lib/tuning.js';
describe('manual tuning range', () => {
  it('accepts 950 MHz / 1250 mV and the range ends', () => {
    expect(checkManual(950, 1250)).toBeNull();
    expect(checkManual(400, 1000)).toBeNull();
    expect(checkManual(1000, 1300)).toBeNull();
  });
  it('refuses values outside 400-1000 MHz / 1000-1300 mV', () => {
    for (const [f, v] of [[399, 1150], [1001, 1150], [525, 999], [525, 1301], ['', 1150]]) expect(checkManual(f, v)).not.toBeNull();
  });
  it('warns above 700 MHz or 1200 mV only', () => {
    expect(isHigh(525, 1150)).toBe(false); expect(isHigh(700, 1200)).toBe(false);
    expect(isHigh(725, 1150)).toBe(true); expect(isHigh(525, 1225)).toBe(true);
  });
  it('PSU presets give a 90 % power limit', () => {
    expect(PSU_PRESETS.map(powerForPsu)).toEqual([22, 27, 36, 45, 54]);
    expect(powerForPsu(10)).toBe(10);
  });
  it('hashrate estimate', () => expect(estThs(950)).toBeCloseTo(1.94, 2));
});

import { TUNER_GOALS, tunerPhase } from '../src/lib/costean.js';
describe('auto-tune searcher UI helpers', () => {
  it('lists the three goals, defaulting to best efficiency', () => {
    expect(TUNER_GOALS.map(g => g.id)).toEqual(['efficiency', 'hashrate', 'power']);
    expect(TUNER_GOALS[0].id).toBe('efficiency');
  });
  it('maps searcher phases to Lode pills', () => {
    expect(tunerPhase({}).id).toBe('off');
    expect(tunerPhase({ tuner: { enabled: false } }).pill).toBe('OFF');
    expect(tunerPhase({ tuner: { enabled: true, phase: 'searching' } }).pill).toBe('SEARCHING');
    expect(tunerPhase({ tuner: { enabled: true, phase: 'trimming' } }).pill).toBe('TRIMMING');
    expect(tunerPhase({ tuner: { enabled: true, phase: 'locked' } }).pill).toBe('LOCKED');
    expect(tunerPhase({ tuner: { enabled: true, phase: 'watching' } }).pill).toBe('WATCHING');
    expect(tunerPhase({ tuner: { enabled: true, phase: 'backoff' } }).pill).toBe('BACKING OFF');
  });
});
