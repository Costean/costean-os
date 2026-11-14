import { describe, it, expect } from 'vitest';
import { parseUrl, splitUser, joinUser, formFromInfo, validateForm, buildPoolsBody, verifyPools, clearedSlots } from '../src/lib/pools.js';

const INFO = { pools: [
  { id: 0, stratumProtocol: 'SV1', stratumURL: 'public-pool.io', stratumPort: 3333, stratumUser: 'bc1qold.bitaxe', stratumPassword: '*****', stratumTLS: 0, stratumSuggestedDifficulty: 1000, stratumCert: '', stratumDecodeCoinbase: true, splitPercent: 0 },
  { id: 1, stratumProtocol: 'SV1', stratumURL: 'solo.ckpool.org', stratumPort: 3333, stratumUser: 'bc1qold.bitaxe', stratumPassword: '*****', stratumTLS: 2, stratumSuggestedDifficulty: 512, splitPercent: 0 }] };

describe('pool form', () => {
  it('parses urls', () => {
    expect(parseUrl('stratum+tcp://pool.example.com:4333')).toEqual({ host: 'pool.example.com', port: 4333, tls: 0 });
    expect(parseUrl('stratum+ssl://pool.example.com:443')).toEqual({ host: 'pool.example.com', port: 443, tls: 1 });
    expect(parseUrl(' pool.example.com ')).toEqual({ host: 'pool.example.com', port: null, tls: null });
  });
  it('splits and joins address.worker', () => {
    expect(splitUser('bc1qabc.rig1')).toEqual({ address: 'bc1qabc', worker: 'rig1' });
    expect(splitUser('user')).toEqual({ address: 'user', worker: '' });
    expect(joinUser(' bc1qabc ', ' rig1 ')).toBe('bc1qabc.rig1'); expect(joinUser('bc1qabc', '')).toBe('bc1qabc');
  });
  it('never sends empty slots (the costean.1/.2 bug: one empty slot made the miner reject the whole save)', () => {
    const rows = formFromInfo(INFO);
    rows[0].url = 'stratum+tcp://my-pool.example:3334'; rows[0].address = 'bc1qnew'; rows[0].worker = 'gamma';
    const b = buildPoolsBody(INFO, rows, false);
    expect(b.pools.map(p => p.id)).toEqual([0, 1]);
    expect(b.pools.every(p => p.stratumURL && p.stratumUser)).toBe(true);
    expect(b.pools[0]).toMatchObject({ stratumURL: 'my-pool.example', stratumPort: 3334, stratumUser: 'bc1qnew.gamma', stratumPassword: '*****', stratumTLS: 0, stratumSuggestedDifficulty: 1000 });
    expect(b.pools[1]).toMatchObject({ stratumTLS: 2, stratumSuggestedDifficulty: 512, stratumPassword: '*****' });   // untouched fields kept
  });
  it('new password and new slot', () => {
    const rows = formFromInfo(INFO); rows[2].url = 'third.example:3333'; rows[2].address = 'bc1qthird'; rows[0].password = 'secret';
    const b = buildPoolsBody(INFO, rows, false);
    expect(b.pools.find(p => p.id === 2)).toMatchObject({ stratumURL: 'third.example', stratumPort: 3333, stratumPassword: 'x' });
    expect(b.pools.find(p => p.id === 0).stratumPassword).toBe('secret');
  });
  it('validates', () => {
    const rows = formFromInfo(INFO); expect(validateForm(rows)).toBeNull();
    rows[0].address = ''; expect(validateForm(rows)).toMatch(/address/);
    const r2 = formFromInfo(INFO); r2[0].url = ''; r2[0].address = ''; expect(validateForm(r2)).toMatch(/main pool/);
    const r3 = formFromInfo(INFO); r3[0].url = 'host:99999'; expect(validateForm(r3)).toMatch(/port/);
  });
  it('verifies read-back and cleared slots', () => {
    const rows = formFromInfo(INFO); const b = buildPoolsBody(INFO, rows, false);
    expect(verifyPools(INFO, b.pools)).toEqual([]);
    rows[0].address = 'bc1qother'; expect(verifyPools(INFO, buildPoolsBody(INFO, rows, false).pools)).toEqual([0]);
    const r = formFromInfo(INFO); r[1].url = ''; r[1].address = ''; r[1].worker = ''; expect(clearedSlots(r)).toEqual([1]);
  });
});
