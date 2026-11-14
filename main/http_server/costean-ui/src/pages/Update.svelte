<script>
  import { onMount } from 'svelte';
  import { app, refresh } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  import { detectUpdate, readHead, verifyResult } from '../lib/ota.js';
  const KEY = 'costean-pending-update';
  let file = $state(null);
  let det = $state(null);
  let busy = $state(false);
  let phase = $state('');          // '', 'upload', 'reboot', 'done', 'fail'
  let result = $state(null);       // { ok, text } stays on screen (not a toast)
  let pending = $state(load());
  const i = $derived(app.info || {});

  function load() { try { return JSON.parse(localStorage.getItem(KEY) || 'null'); } catch { return null; } }
  function keep(p) { pending = p; try { p ? localStorage.setItem(KEY, JSON.stringify(p)) : localStorage.removeItem(KEY); } catch { } }

  async function pick(e) {
    file = e.target.files[0] || null; det = null; result = null; phase = '';
    if (file) det = detectUpdate(await readHead(file), file.size, file.name);
  }

  const sleep = ms => new Promise(r => setTimeout(r, ms));
  /** wait for the reboot, then read what the miner really runs */
  async function verify(p) {
    phase = 'reboot';
    const t0 = Date.now();
    let wentDown = false, info = null;
    while (Date.now() - t0 < 150000) {
      await sleep(3000);
      try {
        info = await api.info();
        // the miner answers again: give it a few seconds after the upload so we do not read the old firmware before it restarts
        if (wentDown || Date.now() - p.at > 25000) break;
      } catch { wentDown = true; }
    }
    if (!info) { phase = 'fail'; result = { ok: false, text: 'The miner did not come back within 2½ minutes. Check its screen: the boot screen shows the version it runs.' }; return; }
    app.info = info;
    result = verifyResult(p, info);
    phase = result.ok ? 'done' : 'fail';
    if (!result.ok) result.text += ' Upload esp-miner.bin again in this box. If it keeps failing, flash the factory image at 0x0 over USB.';
    keep(null);
    refresh();
  }

  async function install() {
    if (!file || !det || (det.kind !== 'firmware' && det.kind !== 'www')) return;
    busy = true; result = null; phase = 'upload';
    const p = { kind: det.kind, expected: det.version || '', previous: i.version || '', at: Date.now() };
    try {
      const r = await api.ota(file, det.kind === 'www');
      if (det.kind === 'firmware' && r?.version && r.version !== p.expected) p.expected = r.version;
      keep(p);
      result = { ok: true, text: (r?.message || 'Uploaded.') + ' Waiting for the miner to restart…' };
      await verify(p);
    } catch (e) {
      phase = 'fail';
      result = { ok: false, text: 'Not installed: ' + (e.message || 'upload failed') + ' The miner keeps running ' + (i.version || 'its current firmware') + '.' };
    } finally { busy = false; }
  }

  onMount(() => { if (pending && Date.now() - pending.at < 600000) verify(pending); else keep(null); });
</script>
<div class="card" data-testid="update-card">
  <h2>Update &amp; Recovery</h2>
  <div class="ver">
    <div><span class="eyebrow">Firmware running now</span><b class="mono big" data-testid="running-version">{i.version || '—'}</b></div>
    <div class="facts small mute">
      <span>Slot <b class="mono">{i.runningPartition || '—'}</b>{#if i.bootPartition && i.bootPartition !== i.runningPartition} · next boot <b class="mono">{i.bootPartition}</b>{/if}</span>
      {#if i.firmwareBuilt}<span>Built <b class="mono">{i.firmwareBuilt}</b></span>{/if}
      <span>Web UI <b class="mono">{app.wwwVersion?.www || '—'}</b></span>
    </div>
  </div>
  <p class="small mute">The miner's screen shows the same version on its boot screen, and as “OS c5” (etc.) on the SHARES page.</p>

  <label class="f"><span>Update file: <b>esp-miner.bin</b> (normal update) or www.bin</span><input type="file" accept=".bin" onchange={pick} data-testid="update-file" /></label>
  {#if det}
    {#if det.kind === 'firmware'}
      <div class="det ok" data-testid="detected">✓ Firmware <b class="mono">{det.version}</b> · {(file.size / 1048576).toFixed(2)} MB{#if det.version === i.version} · <b>this version is already running</b>{/if}</div>
    {:else if det.kind === 'www'}
      <div class="det warn" data-testid="detected">Web UI image (www.bin). This does <b>not</b> change the firmware or the miner's screen. For a normal update pick esp-miner.bin.</div>
    {:else}
      <div class="det crit" data-testid="detected">✗ {det.error}</div>
    {/if}
  {/if}
  <button class="pri" data-testid="install" disabled={busy || !det || (det.kind !== 'firmware' && det.kind !== 'www')} onclick={install}>
    {busy ? (phase === 'reboot' ? 'Waiting for restart…' : 'Uploading…') : det?.kind === 'www' ? 'Install web UI (www.bin)' : det?.version ? `Install firmware ${det.version}` : 'Install'}
  </button>
  {#if phase === 'reboot' && !busy}<p class="small mute">Checking the update after the restart…</p>{/if}
  {#if result}<div class="result {result.ok ? 'ok' : 'crit'}" role="status" data-testid="update-result">{result.text}</div>{/if}

  <p class="small mute">OTA keeps your settings. The factory image (esp-miner-factory-…bin) cannot be uploaded here: flash it at address 0x0 with a USB cable (it wipes the settings). To go back to stock, flash the official ESP-Miner v2.15.4 release.</p>
</div>
<style>
.ver{display:flex;flex-wrap:wrap;gap:10px 24px;align-items:end;margin:4px 0 6px}.ver>div:first-child{display:grid}.big{font-size:22px;color:var(--acc)}
.facts{display:grid;gap:2px}
.det,.result{padding:10px 12px;border-radius:10px;border:1px solid var(--line);margin:6px 0}
.det.ok,.result.ok{border-color:#3fb95066;background:#3fb95012}.det.warn{border-color:#e3b34166;background:#e3b34112}.det.crit,.result.crit{border-color:var(--crit);background:#f8514912}
</style>
