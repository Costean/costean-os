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
    <div><div class="mute small">Last restart reason</div><b class="mono {i.lastResetUnexpected ? 'crit' : ''}" data-testid="reset-reason">{i.lastResetReason ?? i.resetReason ?? '—'}{i.lastResetPhase ? ` · while ${i.lastResetPhase}` : ''}</b></div>
    <div><div class="mute small">Boots / quick resets</div><b class="mono" data-testid="boot-count">{i.bootCount ?? '—'} / {i.quickResets ?? 0}{i.safeMode ? ' · SAFE MODE' : ''}</b></div>
    <div><div class="mute small">Wi-Fi</div><b class="mono" data-testid="wifi-diag">{i.wifiStatus || '—'}{i.wifiFailReason ? ` · last error ${i.wifiFailReason}` : ''}</b></div>
    <div><div class="mute small">Uptime</div><b class="mono">{i.uptimeSeconds != null ? Math.floor(i.uptimeSeconds / 60) + ' min' : '—'}</b></div>
    <div><div class="mute small">Settings storage</div><b class="mono {i.nvsWriteErrors ? 'crit' : ''}" data-testid="nvs-health">{i.nvsWriteErrors ? `${i.nvsWriteErrors} write errors` : 'ok'}{i.nvsFreeEntries != null ? ` · ${i.nvsFreeEntries} free entries` : ''}</b></div>
  </div>
  <div class="row" style="margin-top:12px"><button onclick={load}>Load logs</button></div>
  {#if i.lastResetUnexpected && i.lastResetWifi}<div class="mute small" style="margin-top:8px">Wi-Fi status when it happened: <span class="mono">{i.lastResetWifi}</span></div>{/if}
  {#if logs}<pre class="mono small" style="white-space:pre-wrap;max-height:420px;overflow:auto">{logs}</pre>{/if}
</div>
