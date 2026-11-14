// Costean OS 1.2.0-costean.5 checks against the DEMO mock (never a real miner):
// update page (file detection, refusals, verified result after the restart, the "did not take" case),
// screen colors, and editable profiles that survive a mock restart.
import { chromium } from '/node_modules/playwright-core/index.mjs';
import { spawn } from 'node:child_process'; import fs from 'node:fs';
const OUT = process.env.SCREENS || 'screens-c5'; fs.mkdirSync(OUT, { recursive: true });
const BUILD = new URL('../../../../build/', import.meta.url).pathname;
const FW_BIN = BUILD + 'esp-miner.bin', WWW_BIN = BUILD + 'www.bin', FACTORY = process.env.FACTORY_BIN || '';
let fail = 0; const ok = (c, m) => { console.log(c ? '  ✓' : '  ✗', m); if (!c) fail++; };
const txt = (p, sel) => p.textContent(sel).catch(() => '');
const fwVersion = (() => { const b = fs.readFileSync(FW_BIN); return b.subarray(48, 80).toString('latin1').replace(/\0.*$/s, ''); })();
async function mock(port, env = {}) {
  const srv = spawn('node', ['e2e/mock-server.mjs'], { stdio: 'ignore', env: { ...process.env, PORT: String(port), ...env } });
  await new Promise(r => setTimeout(r, 900)); return srv;
}
const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', args: ['--no-sandbox'] });
const desk = { viewport: { width: 1440, height: 960 } }, phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 3, isMobile: true, hasTouch: true };
try {
  // ---------- 1. update: miner on costean.3, upload the new esp-miner.bin, verified after the restart
  let srv = await mock(8301, { FW_VERSION: '1.2.0-costean.3' });
  let URL = 'http://127.0.0.1:8301/';
  let page = await (await browser.newContext(desk)).newPage(); const errs = []; page.on('pageerror', e => errs.push(e.message));
  await page.goto(URL + '#/update'); await page.waitForSelector('[data-testid=running-version]'); await page.waitForTimeout(1500);
  ok((await txt(page, '[data-testid=running-version]')).includes('1.2.0-costean.3'), 'update: running version shown big (costean.3)');
  ok((await txt(page, '[data-testid=side-version]')).includes('1.2.0-costean.3'), 'update: firmware version in the sidebar');
  // www.bin picked: warned that it does not change the firmware
  await page.setInputFiles('[data-testid=update-file]', WWW_BIN); await page.waitForTimeout(300);
  ok((await txt(page, '[data-testid=detected]')).includes('does not change the firmware'), 'update: www.bin is recognised and labelled "does not change the firmware"');
  // factory image refused before upload
  if (FACTORY && fs.existsSync(FACTORY)) {
    await page.setInputFiles('[data-testid=update-file]', FACTORY); await page.waitForTimeout(300);
    ok((await txt(page, '[data-testid=detected]')).includes('factory image') && await page.isDisabled('[data-testid=install]'), 'update: factory image refused in the browser (install disabled)');
    await page.screenshot({ path: `${OUT}/c5-03-update-factory-refused.png`, fullPage: true });
  }
  await page.setInputFiles('[data-testid=update-file]', FW_BIN); await page.waitForTimeout(300);
  ok((await txt(page, '[data-testid=detected]')).includes(fwVersion) && (await txt(page, '[data-testid=install]')).includes(fwVersion), `update: esp-miner.bin recognised as firmware ${fwVersion}`);
  await page.screenshot({ path: `${OUT}/c5-01-update-file-detected.png`, fullPage: true });
  await page.click('[data-testid=install]');
  await page.waitForFunction(() => /Updated:|did NOT take|did not come back/.test(document.querySelector('[data-testid=update-result]')?.textContent || ''), null, { timeout: 60000 });
  const res = await txt(page, '[data-testid=update-result]');
  ok(res.includes('Updated: the miner now runs ' + fwVersion) && res.includes('ota_1'), 'update: success shown only after the miner reports the new version (slot ota_1)');
  ok((await txt(page, '[data-testid=running-version]')).includes(fwVersion), 'update: running version now the new one');
  await page.screenshot({ path: `${OUT}/c5-02-update-verified.png`, fullPage: true });
  // API refusals (what the firmware answers)
  let r = await fetch(URL + 'api/system/OTAWWW', { method: 'POST', body: fs.readFileSync(FW_BIN) });
  ok(r.status === 400 && (await r.json()).message.includes('firmware file'), 'API: esp-miner.bin sent to the www.bin endpoint is refused (400), nothing changed');
  r = await fetch(URL + 'api/system/OTA', { method: 'POST', body: fs.readFileSync(WWW_BIN) });
  ok(r.status === 400, 'API: www.bin sent to the firmware endpoint is refused (400)');
  ok(errs.length === 0, 'update: no page errors ' + errs.join(' | '));
  srv.kill();

  // ---------- 2. an update that uploads but does not take: the page says so
  srv = await mock(8302, { FW_VERSION: '1.2.0-costean.3', OTA_NOAPPLY: '1' }); URL = 'http://127.0.0.1:8302/';
  page = await (await browser.newContext(desk)).newPage();
  await page.goto(URL + '#/update'); await page.waitForSelector('[data-testid=running-version]'); await page.waitForTimeout(1200);
  await page.setInputFiles('[data-testid=update-file]', FW_BIN); await page.waitForTimeout(300); await page.click('[data-testid=install]');
  await page.waitForFunction(() => /Updated:|did NOT take/.test(document.querySelector('[data-testid=update-result]')?.textContent || ''), null, { timeout: 60000 });
  ok((await txt(page, '[data-testid=update-result]')).includes('did NOT take: the miner still runs 1.2.0-costean.3'), 'update: an update that did not apply is reported as failed (not "success")');
  await page.screenshot({ path: `${OUT}/c5-04-update-did-not-take.png`, fullPage: true });
  srv.kill();

  // ---------- 3. screen colors + editable profiles, kept across a mock restart
  const NVS = '/tmp/costean-mock-nvs-c5.json'; fs.rmSync(NVS, { force: true });
  srv = await mock(8303, { NVS_FILE: NVS }); URL = 'http://127.0.0.1:8303/';
  const writes = async () => (await fetch(URL + '__writes')).json();
  page = await (await browser.newContext(desk)).newPage(); const errs2 = []; page.on('pageerror', e => errs2.push(e.message));
  await page.goto(URL + '#/system'); await page.waitForSelector('[data-testid=screen-card]'); await page.waitForTimeout(1200);
  ok((await page.getAttribute('[data-testid=screen-normal]', 'aria-checked')) === 'true', 'screen: default is Normal');
  await page.click('[data-testid=screen-inverted]'); await page.waitForTimeout(800);
  let w = await writes(); ok(JSON.parse(w.at(-1).body).invertscreen === 1 && (await page.getAttribute('[data-testid=screen-inverted]', 'aria-checked')) === 'true', 'screen: Inverted sends {invertscreen:1} and reads it back');
  await page.screenshot({ path: `${OUT}/c5-05-screen-colors-inverted.png`, fullPage: true });
  await page.click('[data-testid=screen-normal]'); await page.waitForTimeout(800);
  ok(JSON.parse((await writes()).at(-1).body).invertscreen === 0, 'screen: back to Normal sends {invertscreen:0}');
  await page.screenshot({ path: `${OUT}/c5-06-screen-colors-normal.png`, fullPage: true });

  await page.goto(URL + '#/tuning'); await page.waitForSelector('[data-testid=profile-editor] [data-testid=prof-eco]'); await page.waitForTimeout(1200);
  ok(await page.isDisabled('[data-testid=prof-save]'), 'profiles: nothing to save before an edit');
  await page.fill('[data-testid=prof-eco-mhz]', '1100');
  ok((await txt(page, '[data-testid=prof-error]')).includes('400-1000') && await page.isDisabled('[data-testid=prof-save]'), 'profiles: 1100 MHz refused (400-1000)');
  await page.fill('[data-testid=prof-eco-mhz]', '600'); await page.fill('[data-testid=prof-eco-mv]', '1200'); await page.fill('[data-testid=prof-eco-name]', 'Daytime');
  await page.fill('[data-testid=prof-turbo-mhz]', '950'); await page.fill('[data-testid=prof-turbo-mv]', '1250');
  await page.check('[data-testid=prof-user1-on]'); await page.fill('[data-testid=prof-user1-name]', 'Night'); await page.fill('[data-testid=prof-user1-mhz]', '700'); await page.fill('[data-testid=prof-user1-mv]', '1180');
  await page.waitForTimeout(4500);   // live refresh must not undo typing
  ok(await page.inputValue('[data-testid=prof-eco-mhz]') === '600' && await page.inputValue('[data-testid=prof-turbo-mhz]') === '950', 'profiles: typed values survive the live refresh');
  await page.screenshot({ path: `${OUT}/c5-07-profiles-editing.png`, fullPage: true });
  await page.click('[data-testid=prof-save]'); await page.waitForSelector('[data-testid=confirm]');
  ok(await page.isDisabled('[data-testid=confirm-ok]'), 'profiles: above-stock values need the TURBO phrase');
  await page.fill('[data-testid=phrase]', 'TURBO'); await page.click('[data-testid=confirm-ok]'); await page.waitForSelector('[data-testid=prof-msg]');
  ok((await txt(page, '[data-testid=prof-msg]')).includes('read back'), 'profiles: saved and read back from the miner');
  w = await writes(); const pb = JSON.parse(w.at(-1).body);
  ok(pb.profiles?.eco?.mhz === 600 && pb.profiles.eco.mv === 1200 && pb.profiles.eco.name === 'Daytime' && pb.profiles.turbo.mhz === 950 && pb.profiles.user1.enabled === true && !pb.profiles.stock, 'profiles: only the changed profiles are sent');
  await page.waitForTimeout(800);
  ok((await txt(page, '[data-testid=preset-eco]')).includes('Daytime') && (await txt(page, '[data-testid=preset-eco]')).includes('600'), 'profiles: the Eco card shows the new name and 600 MHz');
  ok(await page.locator('[data-testid=preset-user1]').count() === 1 && (await txt(page, '[data-testid=preset-user1]')).includes('Night'), 'profiles: Custom 1 "Night" appears as a card once turned on');
  await page.click('[data-testid=preset-eco]'); await page.waitForSelector('[data-testid=confirm]');
  ok(await page.locator('[data-testid=phrase]').count() === 1, 'profiles: switching to an Eco edited above stock asks for the phrase');
  await page.fill('[data-testid=phrase]', 'TURBO'); await page.click('[data-testid=confirm-ok]'); await page.waitForTimeout(900);
  ok((await txt(page, '[data-testid=mode-pill]')).includes('DAYTIME') && (await txt(page, '[data-testid=mode-pill]')).includes('600 MHz'), 'profiles: applying Eco uses the user\'s 600 MHz and name');
  await page.screenshot({ path: `${OUT}/c5-08-profiles-saved.png`, fullPage: true });
  await page.goto(URL + '#/schedules'); await page.waitForSelector('[data-testid=add-window]'); await page.waitForTimeout(1200);
  ok((await page.locator('select >> nth=1').innerHTML().catch(() => '')).includes('Night') || (await page.content()).includes('>Night<'), 'schedules: Custom 1 "Night" can be picked in a window');
  // restart the mock on the same NVS file
  srv.kill(); await new Promise(r => setTimeout(r, 300)); srv = await mock(8303, { NVS_FILE: NVS });
  page = await (await browser.newContext(phone)).newPage();
  await page.goto(URL + '#/tuning'); await page.waitForSelector('[data-testid=prof-eco-mhz]'); await page.waitForTimeout(1200);
  ok(await page.inputValue('[data-testid=prof-eco-mhz]') === '600' && await page.inputValue('[data-testid=prof-eco-name]') === 'Daytime' && await page.inputValue('[data-testid=prof-turbo-mv]') === '1250' && await page.isChecked('[data-testid=prof-user1-on]'), 'after restart: edited profiles, names and Custom 1 kept');
  await page.screenshot({ path: `${OUT}/c5-09-profiles-after-restart-mobile.png`, fullPage: true });
  await page.click('[data-testid=prof-reset-all]'); await page.click('[data-testid=confirm-ok]'); await page.waitForSelector('[data-testid=prof-msg]'); await page.waitForTimeout(600);
  ok(await page.inputValue('[data-testid=prof-eco-mhz]') === '425' && await page.inputValue('[data-testid=prof-turbo-mhz]') === '575' && !(await page.isChecked('[data-testid=prof-user1-on]')), 'reset to defaults: 425 / 575 and Custom 1 off');
  ok((await txt(page, '[data-testid=mode-pill]')).includes('ECO') || (await txt(page, '[data-testid=mode-pill]')).includes('425'), 'reset to defaults: the running Eco profile follows (425 MHz)');
  r = await fetch(URL + 'api/costean', { method: 'PATCH', body: JSON.stringify({ profiles: { eco: { mhz: 1001 } } }) }); ok(r.status === 400, 'API: a profile above 1000 MHz is refused');
  r = await fetch(URL + 'api/costean', { method: 'PATCH', body: JSON.stringify({ profile: 'user2' }) }); ok(r.status === 400, 'API: a turned-off Custom profile cannot be selected');
  ok(errs2.length === 0, 'no page errors ' + errs2.join(' | '));
  srv.kill();
} finally { await browser.close(); }
console.log(fail ? `${fail} check(s) failed` : 'all costean.5 checks passed'); process.exit(fail ? 1 : 0);
