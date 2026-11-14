<script>
  import { app, toast } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  let fw = $state(null);
  let www = $state(null);
  let busy = $state(false);
  async function upload(file, isWww) {
    if (!file) return;
    busy = true;
    try {
      await api.ota(file, isWww);
      toast(isWww ? 'www.bin uploaded' : 'Firmware uploaded. The miner will reboot.');
    } catch (e) { toast(e.message || 'Upload failed', 'err'); }
    finally { busy = false; }
  }
</script>
<div class="card">
  <h2>Update &amp; Recovery</h2>
  <p class="mute">OTA keeps your settings. A factory image flashed at address 0x0 will wipe NVS settings and restore the official layout. To go back to stock, flash the official ESP-Miner v2.15.4 release linked in the Costean README.</p>
  <div class="row"><span>Running</span><span class="sp"></span><b class="mono">{app.info?.firmwareName || 'Costean OS'} {app.info?.version || app.info?.axeOSVersion || ''}</b></div>
  <label class="f"><span>Firmware (esp-miner.bin)</span><input type="file" onchange={e => fw = e.target.files[0]} /></label>
  <button class="pri" disabled={busy || !fw} onclick={() => upload(fw, false)}>Upload firmware</button>
  <label class="f"><span>Optional web UI (www.bin)</span><input type="file" onchange={e => www = e.target.files[0]} /></label>
  <button disabled={busy || !www} onclick={() => upload(www, true)}>Upload www.bin</button>
  <p class="small mute">To recover a bricked UI, hold the boot button and use the recovery page, or flash the official factory image with esptool.</p>
</div>
