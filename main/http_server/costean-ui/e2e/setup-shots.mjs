// Phone-size previews and checks of the first-run Wi-Fi setup flow against the DEMO mock (never a real miner).
import { chromium } from '/node_modules/playwright-core/index.mjs';
import { spawn } from 'node:child_process'; import fs from 'node:fs';
const OUT = process.env.SCREENS || 'screens-setup'; fs.mkdirSync(OUT, { recursive: true });
let fail = 0; const ok = (c, m) => { console.log(c ? '  ✓' : '  ✗', m); if (!c) fail++; };
async function withMock(scenario, port, fn) {
  const srv = spawn('node', ['e2e/mock-server.mjs'], { stdio: 'ignore', env: { ...process.env, PORT: String(port), SCENARIO: scenario } });
  await new Promise(r => setTimeout(r, 800)); const URL = `http://127.0.0.1:${port}/`;
  const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', args: ['--no-sandbox'] });
  try { await fn(browser, URL); } finally { await browser.close(); srv.kill(); }
}
const PHONES = { iphone: { viewport: { width: 390, height: 844 }, deviceScaleFactor: 3, isMobile: true, hasTouch: true, userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148' },
  android: { viewport: { width: 360, height: 780 }, deviceScaleFactor: 3, isMobile: true, hasTouch: true, userAgent: 'Mozilla/5.0 (Linux; Android 15; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Mobile Safari/537.36' } };
const noOverflow = p => p.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1);

await withMock('setup', 8191, async (browser, URL) => {
  for (const [dev, opt] of Object.entries(PHONES)) {
    const page = await (await browser.newContext(opt)).newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
    const tag = dev === 'iphone' ? '' : '-android';
    await page.goto(URL); await page.waitForSelector('[data-testid=setup]');
    ok(true, `${dev}: setup opens by itself on a fresh miner (no Wi-Fi saved)`);
    await page.waitForTimeout(500); await page.screenshot({ path: `${OUT}/setup-00-scanning${tag}.png` });
    await page.waitForSelector('[data-testid=net]', { timeout: 8000 });
    const names = await page.locator('[data-testid=net] .nm b').allTextContents();
    ok(names.length === 6 && names[0] === 'DEMO-Home' && names.filter(n => n === 'DEMO-Home').length === 1, `${dev}: scan list deduped, hidden dropped, strongest first (${names.join(', ')})`);
    ok(await noOverflow(page), `${dev}: no sideways scrolling`);
    await page.screenshot({ path: `${OUT}/setup-01-network-list${tag}.png` });
    await page.locator('[data-testid=net]').first().tap();
    await page.waitForSelector('[data-testid=password]');
    ok((await page.textContent('[data-testid=chosen]')).includes('DEMO-Home'), `${dev}: tapping a network fills it in and asks for the password`);
    await page.click('[data-testid=connect]'); ok(await page.locator('[data-testid=pw-err]').count() === 1, `${dev}: empty password is refused`);
    await page.fill('[data-testid=password]', 'demo-password-123');
    await page.screenshot({ path: `${OUT}/setup-02-password${tag}.png` });
    const w0 = (await (await fetch(URL + '__writes')).json()).length;
    await page.click('[data-testid=connect]'); await page.waitForSelector('[data-testid=saved]');
    const w = (await (await fetch(URL + '__writes')).json()).slice(w0);
    ok(w.length === 2 && w[0].p === '/api/system' && JSON.stringify(JSON.parse(w[0].body)) === JSON.stringify({ ssid: 'DEMO-Home', wifiPass: 'demo-password-123' }) && w[1].p === '/api/system/restart', `${dev}: Save sends {ssid, wifiPass} then restart`);
    await page.screenshot({ path: `${OUT}/setup-03-saved${tag}.png` });
    ok(errs.length === 0, `${dev}: no page errors ${errs.join(' | ')}`);
    if (dev === 'iphone') {
      // hidden network path + open network path
      const p2 = await (await browser.newContext(opt)).newPage(); await p2.goto(URL); await p2.waitForSelector('[data-testid=manual]');
      await p2.click('[data-testid=manual]'); await p2.fill('[data-testid=manual-ssid]', 'DEMO-Hidden'); await p2.fill('[data-testid=password]', 'hidden-pass-9');
      await p2.screenshot({ path: `${OUT}/setup-04-manual.png` });
      const a = (await (await fetch(URL + '__writes')).json()).length; await p2.click('[data-testid=connect]'); await p2.waitForSelector('[data-testid=saved]');
      const ww = (await (await fetch(URL + '__writes')).json()).slice(a); ok(JSON.parse(ww[0].body).ssid === 'DEMO-Hidden', 'iphone: manual network name is saved');
      const p3 = await (await browser.newContext(opt)).newPage(); await p3.goto(URL); await p3.waitForSelector('[data-testid=net]', { timeout: 8000 });
      await p3.locator('[data-testid=net]', { hasText: 'DEMO-Guest' }).click(); ok(await p3.locator('[data-testid=password]').count() === 0, 'iphone: open network needs no password');
      const b = (await (await fetch(URL + '__writes')).json()).length; await p3.click('[data-testid=connect]'); await p3.waitForSelector('[data-testid=saved]');
      ok(JSON.parse((await (await fetch(URL + '__writes')).json())[b].body).wifiPass === '', 'iphone: open network saves an empty password');
      const p4 = await (await browser.newContext(opt)).newPage(); await p4.goto(URL); await p4.waitForSelector('[data-testid=skip]'); await p4.click('[data-testid=skip]');
      await p4.waitForSelector('[data-testid=gauge]', { timeout: 5000 }).catch(() => {}); ok(await p4.locator('[data-testid=setup]').count() === 0, 'iphone: skip opens the dashboard');
    }
  }
});
await withMock('setupfail', 8192, async (browser, URL) => {
  const page = await (await browser.newContext(PHONES.iphone)).newPage(); await page.goto(URL); await page.waitForSelector('[data-testid=net]', { timeout: 8000 });
  ok((await page.textContent('[data-testid=join-failed]')).includes('Incorrect password'), 'saved network failing: setup reopens and says why');
  await page.screenshot({ path: `${OUT}/setup-05-join-failed.png` });
});
await withMock('normal', 8193, async (browser, URL) => {
  const page = await (await browser.newContext(PHONES.iphone)).newPage(); await page.goto(URL); await page.waitForTimeout(1500);
  ok(await page.locator('[data-testid=setup]').count() === 0, 'connected miner: dashboard, not setup');
  await page.goto(URL + '#/system'); await page.waitForSelector('[data-testid=choose-wifi]'); await page.click('[data-testid=choose-wifi]'); await page.waitForSelector('[data-testid=setup]');
  ok(true, 'connected miner: System & Network → Choose Wi-Fi opens the picker');
});
console.log(fail ? `${fail} check(s) failed` : 'all setup checks passed'); process.exit(fail ? 1 : 0);
