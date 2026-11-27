<script>
  import { app, save, toast, refresh } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  let hostname = $state('');
  let ssid = $state('');
  let pass = $state('');
  let loaded = false;   // fill the form once; the 2 s live refresh must not overwrite what you type
  $effect(() => {
    if (app.info && !loaded) {
      loaded = true;
      hostname = app.info.hostname || hostname;
      ssid = app.info.ssid || ssid;
    }
  });
  const i = $derived(app.info || {});
  // Miner screen colors: applied by the firmware within a few seconds, no restart
  const inverted = $derived(Number(i.invertscreen) === 1);
  let colorBusy = $state(false);
  async function setColors(inv) {
    if (colorBusy || inv === inverted) return;
    colorBusy = true;
    try {
      await api.patch({ invertscreen: inv ? 1 : 0 });
      await refresh();
      if ((Number(app.info?.invertscreen) === 1) === inv) toast(inv ? 'Screen colors: Inverted (applies in a few seconds)' : 'Screen colors: Normal (applies in a few seconds)');
      else toast('The miner did not keep the screen setting', 'err');
    } catch (e) { toast('Not saved: ' + e.message, 'err'); }
    finally { colorBusy = false; }
  }
  async function apply() {
    const body = { hostname };
    if (ssid) body.ssid = ssid;
    if (pass) body.wifiPass = pass;
    if (await save(body, true)) { pass = ''; }
  }
</script>
<div class="grid g2">
  <div class="card">
    <h2>Device</h2>
    <div class="row"><span>Firmware</span><span class="sp"></span><b class="mono">{i.firmwareName || 'Costean OS'} {i.version || ''}</b></div>
    <div class="row"><span>Board</span><span class="sp"></span><b class="mono">{i.boardVersion || '601/602'}</b></div>
    <div class="row"><span>ASIC</span><span class="sp"></span><b class="mono">{i.ASICModel || 'BM1370'}</b></div>
    <div class="row"><span>MAC</span><span class="sp"></span><b class="mono">{i.macAddr || i.mac || '—'}</b></div>
    <label class="f"><span>Hostname</span><input bind:value={hostname} /></label>
  </div>
  <div class="card">
    <h2>Network</h2>
    <div class="row"><span>IPv4</span><span class="sp"></span><b class="mono">{i.ipv4 || i.ipAddress || '—'}</b></div>
    <div class="row"><span>Wi-Fi RSSI</span><span class="sp"></span><b class="mono">{i.wifiRSSI ?? '—'} dBm</b></div>
    <a class="btn wifi-pick" href="#/wifi" data-testid="choose-wifi">Choose Wi-Fi network…</a>
    <label class="f"><span>SSID</span><input bind:value={ssid} /></label>
    <label class="f"><span>Wi-Fi password</span><input type="password" bind:value={pass} placeholder="unchanged if blank" /></label>
  </div>
</div>
<div class="card" data-testid="screen-card">
  <h2>Miner screen</h2>
  <div class="row"><span>Screen colors</span><span class="sp"></span>
    <div class="seg" role="radiogroup" aria-label="Screen colors">
      <button role="radio" aria-checked={!inverted} class:on={!inverted} disabled={colorBusy} onclick={() => setColors(false)} data-testid="screen-normal">Normal</button>
      <button role="radio" aria-checked={inverted} class:on={inverted} disabled={colorBusy} onclick={() => setColors(true)} data-testid="screen-inverted">Inverted</button>
    </div>
  </div>
  <p class="small mute">Normal = light text on a black screen (recommended). Inverted = dark text on a lit screen. Saved on the miner and applied within a few seconds, no restart.</p>
</div>
<button class="pri" onclick={apply}>Save system</button>
<style>.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden}.seg button{border:0;border-radius:0;padding:6px 14px;background:transparent}.seg button.on{background:var(--acc);color:#1a0f06;font-weight:700}
.wifi-pick{display:inline-block;text-decoration:none;justify-self:start;margin:6px 0}</style>
