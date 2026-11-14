<script>
  import { app, save } from '../lib/store.svelte.js';
  import { FREQ_PRESETS, VOLT_PRESETS, STOCK_FREQ, STOCK_VOLT } from '../lib/model.js';
  let freq = $state(525);
  let volt = $state(1150);
  let ocNote = $state('');
  $effect(() => {
    if (app.info) {
      freq = Number(app.info.frequency) || STOCK_FREQ;
      volt = Number(app.info.coreVoltage) || STOCK_VOLT;
    }
  });
  const oc = $derived(!!(app.oc || app.info?.overclockEnabled));
  const allowedF = $derived(oc ? FREQ_PRESETS : FREQ_PRESETS.filter(f => f <= STOCK_FREQ));
  const allowedV = $derived(oc ? VOLT_PRESETS : VOLT_PRESETS.filter(v => v <= STOCK_VOLT));
  function pickF(f) { freq = f; }
  function pickV(v) { volt = v; }
  async function apply() {
    if (!oc && (freq > STOCK_FREQ || volt > STOCK_VOLT)) {
      ocNote = 'Overclock values are blocked unless ?oc is in the URL and overclock is enabled.';
      return;
    }
    ocNote = '';
    await save({ frequency: freq, coreVoltage: volt }, true);
  }
</script>
<div class="card">
  <h2>Mining &amp; Tuning</h2>
  <p class="mute">Stock default is {STOCK_FREQ} MHz at {STOCK_VOLT} mV. Schedules cannot exceed these limits unless overclock is explicitly enabled. Values above stock stay gated behind <span class="kbd">?oc</span>.</p>
  {#if oc}<div class="warn">Overclock unlock is active. Raising frequency or voltage can overheat or damage the board. Thermal and power limits still apply.</div>{/if}
  <h3>Frequency</h3>
  <div class="seg">{#each allowedF as f}<button class:on={freq===f} onclick={() => pickF(f)}>{f}</button>{/each}</div>
  <label class="f"><span>MHz</span><input type="number" bind:value={freq} min="400" max={oc?690:STOCK_FREQ} /></label>
  <h3>Core voltage</h3>
  <div class="seg">{#each allowedV as v}<button class:on={volt===v} onclick={() => pickV(v)}>{v}</button>{/each}</div>
  <label class="f"><span>mV</span><input type="number" bind:value={volt} min="1000" max={oc?1250:STOCK_VOLT} /></label>
  {#if ocNote}<div class="crit">{ocNote}</div>{/if}
  <div class="row"><button class="pri" onclick={apply}>Save tuning</button><span class="mute small">A restart is required to apply frequency and voltage.</span></div>
</div>
{#if app.costean?.status}{@const cs = app.costean.status}
<div class="card c-ok" data-testid="profile-note">
  <h2>Costean profile</h2>
  <p class="small">These are the values of the <b>Manual</b> profile (the default). {#if cs.activeProfile !== 'custom'}Right now the <b>{cs.activeProfile}</b> profile{cs.profileSource === 'schedule' ? ' (set by a schedule)' : ''} is running at {cs.applied?.mhz} MHz / {cs.applied?.mv} mV, so changes here take effect when you switch back to Manual on <a href="#/tuning">Profiles &amp; Limits</a>.{:else}Running now: {cs.applied?.mhz} MHz / {cs.applied?.mv} mV.{/if} Your saved values are never overwritten by a profile or schedule.</p>
</div>
{/if}
