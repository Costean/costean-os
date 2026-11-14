<script>
  import { app, save, toast } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  let enabled = $state(false);
  let webhook = $state('');
  let block = $state(true);
  let best = $state(true);
  let offline = $state(true);
  let overheat = $state(true);
  let statusMin = $state(0);
  $effect(() => {
    if (app.info) {
      enabled = !!app.info.discordEnabled;
      block = app.info.discordBlockFound !== false;
      best = app.info.discordBestDiff !== false;
      offline = app.info.discordOffline !== false;
      overheat = app.info.discordOverheat !== false;
      statusMin = Number(app.info.discordStatusMinutes) || 0;
    }
  });
  const preview = $derived(app.info?.discordWebhookPreview || '');
  async function apply() {
    const body = {
      discordEnabled: enabled,
      discordBlockFound: block,
      discordBestDiff: best,
      discordOffline: offline,
      discordOverheat: overheat,
      discordStatusMinutes: Number(statusMin) || 0,
    };
    if (webhook.trim()) body.discordWebhook = webhook.trim();
    await save(body);
    webhook = '';
  }
  async function test() {
    try { await api.discordTest(); toast('Test message queued'); }
    catch (e) { toast(e.message || 'Test failed', 'err'); }
  }
</script>
<div class="card">
  <h2>Discord alerts</h2>
  <p class="mute">The webhook is stored in NVS and is never returned in full by the API. Only a masked preview is shown. Alerts run in firmware over TLS (esp-tls certificate bundle).</p>
  <label class="chk"><input type="checkbox" bind:checked={enabled} /> Enable Discord alerts</label>
  <label class="f"><span>Webhook URL</span><input type="password" bind:value={webhook} placeholder={preview || 'https://discord.com/api/webhooks/…'} autocomplete="off" /></label>
  {#if preview}<div class="mute small mono">Stored webhook: {preview}</div>{/if}
  <label class="chk"><input type="checkbox" bind:checked={block} /> Block found</label>
  <label class="chk"><input type="checkbox" bind:checked={best} /> New best difficulty</label>
  <label class="chk"><input type="checkbox" bind:checked={offline} /> Offline / pools unavailable</label>
  <label class="chk"><input type="checkbox" bind:checked={overheat} /> Overheat</label>
  <label class="f"><span>Periodic status (minutes, 0 = off)</span><input type="number" min="0" max="1440" bind:value={statusMin} /></label>
  <div class="row"><button class="pri" onclick={apply}>Save alerts</button><button onclick={test}>Send test</button></div>
</div>
