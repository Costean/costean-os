import { describe, it, expect } from 'vitest';
import { modeOf, STOCK_FREQ, STOCK_VOLT } from '../src/lib/model.js';
import { fmtHash, fmtDiff, fmtUptime } from '../src/lib/api.js';

describe('modeOf', () => {
  it('flags overheat first', () => {
    expect(modeOf({ overheat_mode: 1, poolSplitActive: 1 }).text).toBe('Overheat');
  });
  it('flags split mining', () => {
    expect(modeOf({ poolSplitActive: 1 }).text).toBe('Split mining');
  });
  it('reports plain mining when idle', () => {
    expect(modeOf({}).text).toBe('Mining');
  });
});

describe('formatters', () => {
  it('formats hashrate', () => {
    expect(fmtHash(1500)).toBe('1.50 TH/s');
    expect(fmtHash(520)).toMatch(/520\.0 GH\/s/);
  });
  it('formats difficulty', () => {
    expect(fmtDiff(1234)).toBe('1.23K');
  });
  it('formats uptime', () => {
    expect(fmtUptime(90000)).toMatch(/1d/);
  });
});

describe('stock limits', () => {
  it('keeps 525/1150 as stock', () => {
    expect(STOCK_FREQ).toBe(525);
    expect(STOCK_VOLT).toBe(1150);
  });
});
