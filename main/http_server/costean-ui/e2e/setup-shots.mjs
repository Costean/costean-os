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
  const jf = await page.textContent('[data-testid=join-failed]');
  ok(jf.includes('Wrong password?') && jf.includes('case-sensitive') && jf.includes('Error 15'), 'saved network failing: setup reopens and says why (wrong password, error 15, hint)');
  await page.screenshot({ path: `${OUT}/setup-05-join-failed.png` });
});
await withMock('joining', 8193, async (browser, URL) => {
  const page = await (await browser.newContext(PHONES.iphone)).newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
  await page.goto(URL); await page.waitForSelector('[data-testid=joining]');
  await page.waitForTimeout(2500);
  ok((await page.textContent('[data-testid=joining]')).includes('getting an IP address'), 'still joining: setup says so instead of "couldn\'t join"');
  ok(await page.locator('[data-testid=join-failed]').count() === 0, 'still joining: no failure card (no reason to re-enter the password)');
  const scans = (await (await fetch(URL + '__writes')).json()).filter(w => w.p === '/api/system/wifi/scan').length;
  ok(scans === 0, `still joining: the page does not scan while the miner is getting its IP (${scans} scans)`);
  ok(await page.locator('[data-testid=rescan]').isDisabled(), 'still joining: Rescan waits');
  ok(errs.length === 0, `joining: no page errors ${errs.join(' | ')}`);
  await page.screenshot({ path: `${OUT}/setup-06-joining.png` });
});
await withMock('crashed', 8194, async (browser, URL) => {
  const page = await (await browser.newContext(PHONES.iphone)).newPage(); await page.goto(URL); await page.waitForSelector('[data-testid=reset-notice]');
  const t = await page.textContent('[data-testid=reset-notice]');
  ok(t.includes('restarted unexpectedly') && t.includes('Crash (panic)') && t.includes('Wi-Fi connection'), 'after a crash: setup shows the reset reason and what it was doing');
  await page.screenshot({ path: `${OUT}/setup-07-reset-notice.png` });
});
await withMock('safemode', 8195, async (browser, URL) => {
  const page = await (await browser.newContext({ viewport: { width: 1280, height: 860 } })).newPage(); await page.goto(URL); await page.waitForSelector('[data-testid=safe-banner]');
  ok((await page.textContent('[data-testid=safe-banner]')).includes('Safe mode'), 'safe mode: dashboard banner explains it');
  await page.screenshot({ path: `${OUT}/safe-mode-dashboard.png` });
  await page.goto(URL + '#/diagnostics'); await page.waitForSelector('[data-testid=boot-count]');
  ok((await page.textContent('[data-testid=reset-reason]')).includes('Crash (panic)') && (await page.textContent('[data-testid=boot-count]')).includes('SAFE MODE'), 'safe mode: diagnostics show the reset reason and boot counters');
  await page.screenshot({ path: `${OUT}/safe-mode-diagnostics.png` });
  await page.goto(URL); await page.waitForSelector('[data-testid=leave-safe-main]');
  const a = (await (await fetch(URL + '__writes')).json()).length; await page.click('[data-testid=leave-safe-main]'); await page.waitForTimeout(400);
  const w = (await (await fetch(URL + '__writes')).json()).slice(a);
  ok(w.some(x => x.p === '/api/system/restart' && x.q === '?normal=1'), 'safe mode: Restart normally sends restart?normal=1');
});
await withMock('normal', 8193, async (browser, URL) => {
  const page = await (await browser.newContext(PHONES.iphone)).newPage(); await page.goto(URL); await page.waitForTimeout(1500);
  ok(await page.locator('[data-testid=setup]').count() === 0, 'connected miner: dashboard, not setup');
  await page.goto(URL + '#/system'); await page.waitForSelector('[data-testid=choose-wifi]'); await page.click('[data-testid=choose-wifi]'); await page.waitForSelector('[data-testid=setup]');
  ok(true, 'connected miner: System & Network → Choose Wi-Fi opens the picker');
});
console.log(fail ? `${fail} check(s) failed` : 'all setup checks passed'); process.exit(fail ? 1 : 0);
