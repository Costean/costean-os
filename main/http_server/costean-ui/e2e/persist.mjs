// Costean OS 1.2.0-costean.3: settings stick. Type -> live refresh doesn't undo it -> Save -> read back ->
// "reboot" (mock restarted on the same emulated NVS file) -> still there. DEMO mock only, never a real miner.
import { chromium } from '/node_modules/playwright-core/index.mjs';
import { spawn } from 'node:child_process'; import fs from 'node:fs';
const OUT = process.env.SCREENS || 'screens-persist'; fs.mkdirSync(OUT, { recursive: true });
const NVS = '/tmp/costean-mock-nvs.json'; fs.rmSync(NVS, { force: true });
let fail = 0; const ok = (c, m) => { console.log(c ? '  ✓' : '  ✗', m); if (!c) fail++; };
const PORT = 8291, URL = `http://127.0.0.1:${PORT}/`;
const boot = async () => { const p = spawn('node', ['e2e/mock-server.mjs'], { stdio: 'ignore', env: { ...process.env, PORT: String(PORT), SCENARIO: 'fresh', NVS_FILE: NVS } }); await new Promise(r => setTimeout(r, 900)); return p; };
const val = (pg, id) => pg.inputValue(`[data-testid=${id}]`);
const ADDR = 'bc1qDEMOuserWALLETaddr0000000000000000000';
let srv = await boot();
const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', args: ['--no-sandbox'] });
const desk = { viewport: { width: 1440, height: 960 } }, phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 3, isMobile: true, hasTouch: true };
try {
  let page = await (await browser.newContext(desk)).newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
  // ---- pools
  await page.goto(URL + '#/pools'); await page.waitForSelector('[data-testid=pool-0-url]'); await page.waitForTimeout(1200);
  await page.fill('[data-testid=pool-0-url]', 'stratum+tcp://demo-pool.example.org:4334');
  await page.fill('[data-testid=pool-0-address]', ADDR);
  await page.fill('[data-testid=pool-0-worker]', 'gamma602');
  await page.waitForTimeout(6500);   // three live refreshes
  ok(await val(page, 'pool-0-url') === 'stratum+tcp://demo-pool.example.org:4334' && await val(page, 'pool-0-address') === ADDR && await val(page, 'pool-0-worker') === 'gamma602', 'pools: typed values are not reset by the 2 s live refresh');
  await page.click('[data-testid=pool-save]'); await page.waitForSelector('[data-testid=pool-saved]', { timeout: 5000 });
  ok((await page.textContent('[data-testid=pool-saved]')).includes(`demo-pool.example.org:4334 · ${ADDR}.gamma602`), 'pools: saved and read back from the miner');
  const w = await (await fetch(URL + '__writes')).json(); const pb = JSON.parse(w.find(x => x.p === '/api/system').body);
  ok(pb.pools.length === 2 && pb.pools.every(p => p.stratumURL && p.stratumUser) && pb.pools[1].stratumURL === 'solo.ckpool.org', 'pools: only filled slots are sent, fallback sent back unchanged');
  await page.screenshot({ path: `${OUT}/c3-01-pools-saved.png`, fullPage: true });
  // ---- manual tuning 950 / 1250
  await page.goto(URL + '#/mining'); await page.waitForSelector('[data-testid=freq]'); await page.waitForTimeout(1200);
  await page.fill('[data-testid=freq]', '950'); await page.fill('[data-testid=volt]', '1250'); await page.waitForTimeout(4500);
  ok(await val(page, 'freq') === '950' && await val(page, 'volt') === '1250', 'tuning: custom 950 / 1250 stays while the page refreshes');
  ok(await page.locator('[data-testid=oc-warning]').count() === 1, 'tuning: high-clock warning shown above 700 MHz / 1200 mV');
  ok(!(await page.isDisabled('[data-testid=tune-save]')), 'tuning: 950 / 1250 is allowed (no 525 / 1150 cap)');
  await page.screenshot({ path: `${OUT}/c3-02-tuning-950-1250-entered.png`, fullPage: true });
  await page.click('[data-testid=tune-save]'); await page.waitForSelector('[data-testid=tune-saved]', { timeout: 5000 });
  await page.screenshot({ path: `${OUT}/c3-03-tuning-saved.png`, fullPage: true });
  await page.fill('[data-testid=freq]', '1100'); ok(await page.isDisabled('[data-testid=tune-save]'), 'tuning: 1100 MHz (outside 400-1000) is refused');
  await page.fill('[data-testid=freq]', '950');
  // ---- PSU 50 W -> 45 W limit
  await page.goto(URL + '#/tuning'); await page.waitForSelector('[data-testid=psu-50]'); await page.waitForTimeout(800);
  await page.click('[data-testid=psu-50]');
  ok(await val(page, 'lim-power') === '45', 'PSU: 50 W supply sets a 45 W power limit');
  ok(await page.getAttribute('[data-testid=lim-power]', 'max') === '50', 'PSU: power slider goes up to the 50 W rating (no 35 W cap)');
  await page.waitForTimeout(600); await page.screenshot({ path: `${OUT}/c3-04-psu-50w.png`, fullPage: true });
  await page.click('[data-testid=lim-save]'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(900);
  // ---- custom PSU 65 W, power 60 W (rejected above 65)
  const r1 = await fetch(URL + 'api/costean', { method: 'PATCH', body: JSON.stringify({ limits: { psu: 50, power: 51 } }) }); ok(r1.status === 400, 'PSU: power above the supply rating is refused');
  ok(errs.length === 0, 'no page errors ' + errs.join(' | '));
  // ---- old-UI style payload with empty slots (the costean.1/.2 bug) is accepted now
  const r2 = await fetch(URL + 'api/system', { method: 'PATCH', body: JSON.stringify({ pools: [{ id: 0, stratumURL: 'demo-pool.example.org', stratumPort: 4334, stratumUser: `${ADDR}.gamma602`, stratumPassword: '*****' }, { id: 3, stratumURL: '', stratumUser: '', stratumPort: 3333 }] }) });
  ok(r2.status === 200, 'firmware rule: an empty pool slot no longer rejects the whole save');
  // ---- reboot
  srv.kill(); await new Promise(r => setTimeout(r, 300)); srv = await boot();
  page = await (await browser.newContext(phone)).newPage();
  await page.goto(URL + '#/pools'); await page.waitForSelector('[data-testid=pool-0-url]'); await page.waitForTimeout(1200);
  ok(await val(page, 'pool-0-url') === 'demo-pool.example.org' && await val(page, 'pool-0-port') === '4334' && await val(page, 'pool-0-address') === ADDR && await val(page, 'pool-0-worker') === 'gamma602', 'after reboot: pool URL, port, address and worker kept');
  await page.screenshot({ path: `${OUT}/c3-05-pools-after-reboot-mobile.png`, fullPage: true });
  await page.goto(URL + '#/mining'); await page.waitForSelector('[data-testid=freq]'); await page.waitForTimeout(1000);
  ok(await val(page, 'freq') === '950' && await val(page, 'volt') === '1250', 'after reboot: 950 MHz / 1250 mV kept');
  await page.screenshot({ path: `${OUT}/c3-06-tuning-after-reboot-mobile.png`, fullPage: true });
  await page.goto(URL + '#/tuning'); await page.waitForSelector('[data-testid=lim-power]'); await page.waitForTimeout(800);
  ok(await val(page, 'lim-power') === '45' && (await page.getAttribute('[data-testid=psu-50]', 'class')).includes('on'), 'after reboot: 50 W supply and 45 W limit kept');
  await page.screenshot({ path: `${OUT}/c3-07-psu-after-reboot-mobile.png`, fullPage: true });
} finally { await browser.close(); srv.kill(); }
console.log(fail ? `${fail} check(s) failed` : 'all persistence checks passed'); process.exit(fail ? 1 : 0);
