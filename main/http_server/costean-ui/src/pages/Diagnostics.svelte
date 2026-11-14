<script>
  import { app, toast } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  let logs = $state('');
  async function load() {
    try { logs = await api.logs(); } catch (e) { toast(e.message, 'err'); }
  }
  const i = $derived(app.info || {});
</script>
<div class="card">
  <h2>Diagnostics</h2>
  <div class="grid g3">
    <div><div class="mute small">Free heap</div><b class="mono">{i.freeHeap ?? '—'}</b></div>
    <div><div class="mute small">Core voltage (actual)</div><b class="mono">{i.coreVoltageActual ?? i.coreVoltage ?? '—'} mV</b></div>
    <div><div class="mute small">Overclock enabled</div><b class="mono">{i.overclockEnabled ? 'yes' : 'no'}</b></div>
  </div>
  <div class="row" style="margin-top:12px"><button onclick={load}>Load logs</button></div>
  {#if logs}<pre class="mono small" style="white-space:pre-wrap;max-height:420px;overflow:auto">{logs}</pre>{/if}
</div>
