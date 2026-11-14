// Costean OS (Bitaxe Gamma 602) screenshots, smoke checks and a demo click-through against the DEMO mock (never a real miner).
import { chromium } from '/node_modules/playwright-core/index.mjs';
import { spawn } from 'node:child_process'; import fs from 'node:fs';
const OUT = process.env.SCREENS || 'screens'; fs.mkdirSync(OUT, { recursive: true });
let fail = 0; const ok = (c, m) => { console.log(c ? '  ✓' : '  ✗', m); if (!c) fail++; };
const txt = (p, sel) => p.textContent(sel).catch(() => '');
async function withMock(scenario, port, fn) {
  const srv = spawn('node', ['e2e/mock-server.mjs'], { stdio: 'ignore', env: { ...process.env, PORT: String(port), SCENARIO: scenario } });
  await new Promise(r => setTimeout(r, 800)); const URL = `http://127.0.0.1:${port}/`;
  const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', args: ['--no-sandbox'] });
  try { await fn(browser, URL); } finally { await browser.close(); srv.kill(); }
}
async function run(scenario, port, shots, { mobile = false } = {}) {
  await withMock(scenario, port, async (browser, URL) => {
    const ctx = await browser.newContext(mobile ? { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 } : { viewport: { width: 1440, height: 960 }, deviceScaleFactor: 1 });
    const page = await ctx.newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
    await page.goto(URL); await page.waitForSelector('[data-testid=demo]'); await page.waitForTimeout(4500);
    for (const [name, hash, check] of shots) {
      await page.goto(URL + '#/' + hash); await page.waitForTimeout(2200);
      if (check) ok(await check(page), `${scenario}: ${name}`);
      await page.screenshot({ path: `${OUT}/${name}.png`, fullPage: true });
    }
    ok(errs.length === 0, `${scenario}: no page errors ${errs.join(' | ')}`);
    const w = await (await fetch(URL + '__writes')).json(); ok(w.length === 0, `${scenario}: browsing sent no writes`);
    await ctx.close();
  });
}
await run('normal', 8097, [
  ['web-01-dashboard', 'dashboard', async p => (await txt(p, '[data-testid=gauge]')).includes('TH/s') && (await p.locator('[data-testid=costean-strip]').count()) === 1 && (await txt(p, '[data-testid=costean-strip]')).includes('Manual') && !(await txt(p, 'body')).includes('NaN')],
  ['web-02-profiles-limits', 'tuning', async p => (await p.locator('[data-testid=presets] .preset').count()) === 4 && (await txt(p, '[data-testid=preset-eco]')).includes('425') && (await txt(p, '[data-testid=preset-turbo]')).includes('575') && (await txt(p, '[data-testid=autotune]')).includes('off (default)') && (await txt(p, '[data-testid=preset-custom]')).includes('selected')],
  ['web-03-pool-split', 'split', async p => (await p.locator('[data-testid^=split-pool-]').count()) === 3 && (await txt(p, '[data-testid=split-pool-2]')).includes('backup') && (await txt(p, '[data-testid=split-card]')).includes('total 100%')],
  ['web-04-schedules', 'schedules', async p => (await p.locator('.win').count()) === 4 && (await p.locator('.week .cell').count()) === 168 && !(await p.locator('[data-testid=sched-err]').count())],
  ['web-05-history', 'history', async p => (await txt(p, '[data-testid=hist-summary]')).includes('TH/s') && /1\.0\d|0\.9\d/.test(await txt(p, '[data-testid=hist-summary]'))],
  ['web-06-mining-manual', 'mining', async p => (await txt(p, '[data-testid=profile-note]')).includes('Manual')],
  ['web-07-block-found', 'block', async p => (await txt(p, '[data-testid=best-diff]')).includes('G')],
  ['web-08-thermal', 'thermal', null], ['web-09-pools', 'pools', null], ['web-10-alerts', 'alerts', async p => (await txt(p, 'body')).includes('Discord')],
  ['web-11-system', 'system', null], ['web-12-diagnostics', 'diagnostics', null], ['web-13-update', 'update', null],
]);
await run('failover', 8095, [['web-03b-pool-split-failover', 'split', async p => (await txt(p, '[data-testid=failover-badge]')).includes('failover') && (await txt(p, '[data-testid=split-pool-1]')).includes('down') && (await txt(p, '[data-testid=split-pool-0]')).includes('100%')],
  ['web-01c-dashboard-failover', 'dashboard', async p => (await txt(p, '[data-testid=costean-strip]')).includes('failover')]]);
await run('throttle', 8094, [['web-02b-profiles-throttled', 'tuning', async p => (await txt(p, '[data-testid=gov-card]')).includes('Throttling')], ['web-01b-dashboard-throttled', 'dashboard', async p => (await txt(p, '[data-testid=mode-pill]')).includes('THROTTLED')]]);
await run('block', 8096, [['web-07b-block-found', 'block', async p => (await p.locator('[data-testid=block-banner]').count()) === 1 && (await p.locator('[data-testid=nav-block-count]').count()) === 1]]);
await run('normal', 8093, [['web-m01-dashboard-mobile', 'dashboard', null], ['web-m03-split-mobile', 'split', null], ['web-m02-profiles-mobile', 'tuning', null]], { mobile: true });

// Demo click-through: every write goes through a confirm dialog and sends exactly what the firmware expects.
await withMock('normal', 8092, async (browser, URL) => {
  const page = await (await browser.newContext({ viewport: { width: 1440, height: 960 } })).newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
  const writes = async () => (await fetch(URL + '__writes')).json();
  await page.goto(URL + '#/tuning'); await page.waitForSelector('[data-testid=preset-turbo]'); await page.waitForTimeout(1500);
  await page.click('[data-testid=preset-turbo]'); await page.waitForSelector('[data-testid=confirm]');
  ok(await page.isDisabled('[data-testid=confirm-ok]'), 'click: Turbo needs the typed phrase');
  await page.click('text=Cancel'); ok((await writes()).length === 0, 'click: cancel sends nothing');
  await page.click('[data-testid=preset-turbo]'); await page.fill('[data-testid=phrase]', 'TURBO'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(800);
  let w = await writes(); ok(w.length === 1 && w[0].p === '/api/costean' && JSON.parse(w[0].body).profile === 'turbo', 'click: TURBO sends {profile:"turbo"} to PATCH /api/costean');
  ok((await txt(page, '[data-testid=preset-turbo]')).includes('selected'), 'click: Turbo shows as selected');
  await page.click('[data-testid=tune-on]'); await page.fill('[data-testid=phrase]', 'AUTO-TUNE'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(800);
  w = await writes(); ok(JSON.parse(w.at(-1).body).autotune?.enabled === true, 'click: AUTO-TUNE turns auto-tune on');
  await page.click('[data-testid=preset-stock]'); ok(!(await page.locator('[data-testid=phrase]').count()), 'click: Stock needs no phrase'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(600);
  // split: 70/30/0 -> 50/30/20 via the number inputs
  await page.goto(URL + '#/split'); await page.waitForSelector('[data-testid=split-row-2]'); await page.waitForTimeout(1200);
  await page.fill('[data-testid=split-row-0] input.num', '50'); await page.fill('[data-testid=split-row-2] input.num', '20');
  ok(!(await page.locator('[data-testid=split-error]').count()), 'click: 50/30/20 is valid');
  await page.fill('[data-testid=split-row-2] input.num', '25'); ok((await txt(page, '[data-testid=split-error]')).includes('105'), 'click: 50/30/25 is rejected (105 %)');
  ok(await page.isDisabled('[data-testid=save-split]'), 'click: save disabled while invalid');
  await page.fill('[data-testid=split-row-2] input.num', '20'); await page.click('[data-testid=save-split]'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(800);
  w = await writes(); const sp = JSON.parse(w.at(-1).body);
  ok(w.at(-1).p === '/api/system' && sp.poolSplitEnabled === 1 && sp.pools.map(p => p.splitPercent).join() === '50,30,20', 'click: split sends PATCH /api/system with 50/30/20');
  ok(sp.pools.every(p => p.stratumPassword === '*****' && p.stratumURL && p.stratumUser && p.stratumV2ChannelType), 'click: whole pool records sent, password kept masked');
  // schedule: add a window and save
  await page.goto(URL + '#/schedules'); await page.waitForSelector('[data-testid=add-window]'); await page.waitForTimeout(1200);
  await page.click('[data-testid=add-window]'); await page.click('[data-testid=save-sched]'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(800);
  w = await writes(); ok(JSON.parse(w.at(-1).body).schedule?.windows?.length === 5, 'click: schedule saved with 5 windows');
  await page.screenshot({ path: `${OUT}/web-14-clickthrough-schedules.png`, fullPage: true });
  await page.goto(URL + '#/dashboard'); await page.waitForTimeout(2500);
  ok((await txt(page, '[data-testid=costean-strip]')).includes('Stock'), 'click: dashboard strip follows the profile');
  ok(errs.length === 0, 'click: no page errors ' + errs.join(' | '));
  fs.writeFileSync(`${OUT}/clickthrough-writes.json`, JSON.stringify(await writes(), null, 2));
});
console.log(fail ? `FAIL ${fail}` : 'ALL OK'); process.exit(fail ? 1 : 0);
