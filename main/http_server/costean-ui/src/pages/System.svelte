<script>
  import { app, save } from '../lib/store.svelte.js';
  let hostname = $state('');
  let ssid = $state('');
  let pass = $state('');
  $effect(() => {
    if (app.info) {
      hostname = app.info.hostname || hostname;
      ssid = app.info.ssid || ssid;
    }
  });
  const i = $derived(app.info || {});
  async function apply() {
    const body = { hostname };
    if (ssid) body.ssid = ssid;
    if (pass) body.wifiPass = pass;
    await save(body, true);
  }
</script>
<div class="grid g2">
  <div class="card">
    <h2>Device</h2>
    <div class="row"><span>Firmware</span><span class="sp"></span><b class="mono">{i.firmwareName || 'Costean OS'} {i.version || ''}</b></div>
    <div class="row"><span>Board</span><span class="sp"></span><b class="mono">{i.boardVersion || '602'}</b></div>
    <div class="row"><span>ASIC</span><span class="sp"></span><b class="mono">{i.ASICModel || 'BM1370'}</b></div>
    <div class="row"><span>MAC</span><span class="sp"></span><b class="mono">{i.macAddr || i.mac || '—'}</b></div>
    <label class="f"><span>Hostname</span><input bind:value={hostname} /></label>
  </div>
  <div class="card">
    <h2>Network</h2>
    <div class="row"><span>IPv4</span><span class="sp"></span><b class="mono">{i.ipv4 || i.ipAddress || '—'}</b></div>
    <div class="row"><span>Wi-Fi RSSI</span><span class="sp"></span><b class="mono">{i.wifiRSSI ?? '—'} dBm</b></div>
    <label class="f"><span>SSID</span><input bind:value={ssid} /></label>
    <label class="f"><span>Wi-Fi password</span><input type="password" bind:value={pass} placeholder="unchanged if blank" /></label>
  </div>
</div>
<button class="pri" onclick={apply}>Save system</button>
