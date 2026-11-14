<script>
  // Manual frequency / core voltage. Full range 400-1000 MHz and 1000-1300 mV: pick a common value or
  // type your own. The form is filled once and not overwritten by the live refresh.
  import { app, refresh, toast, applyCostean, reloadCostean } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  import { MANUAL, FREQ_CHOICES, VOLT_CHOICES, checkManual, isHigh, estWatts, estThs } from '../lib/tuning.js';
  let freq = $state(null), volt = $state(null);
  let toManual = $state(true);
  let err = $state(''), saved = $state(null), saving = $state(false);
  $effect(() => { if (app.info && freq == null) { freq = Math.round(Number(app.info.frequency) || 525); volt = Number(app.info.coreVoltage) || 1150; } });
  const r = $derived(app.costean?.status?.manualRange ? { minMhz: app.costean.status.manualRange[0], maxMhz: app.costean.status.manualRange[1], minMv: app.costean.status.manualRange[2], maxMv: app.costean.status.manualRange[3] } : MANUAL);
  const cs = $derived(app.costean?.status);
  const lim = $derived(cs?.effectiveLimits);
  const problem = $derived(freq == null ? null : checkManual(freq, volt, r));
  const high = $derived(freq != null && isHigh(freq, volt));
  const watts = $derived(freq ? estWatts(freq, volt) : 0);
  async function apply() {
    err = ''; saved = null;
    if (problem) { err = problem; return; }
    saving = true;
    try {
      await api.patch({ frequency: Number(freq), coreVoltage: Number(volt) });
      if (toManual && cs && cs.activeProfile !== 'custom' && cs.profileSource !== 'schedule') await api.patchCostean({ profile: 'custom' }).catch(() => {});
      await refresh();
      const ok = Math.round(Number(app.info.frequency)) === Math.round(Number(freq)) && Number(app.info.coreVoltage) === Number(volt);
      if (!ok) { err = 'The miner did not keep these values. Try again.'; return; }
      saved = { mhz: Number(freq), mv: Number(volt) };
      toast('Saved. The miner moves to the new clock within a few seconds.');
      reloadCostean();
    } catch (x) { err = x.message || 'Save failed'; } finally { saving = false; }
  }
</script>
<div class="card c-cyan" data-testid="manual-tuning">
  <h2>Mining &amp; Tuning · Manual</h2>
  <p class="mute small">Stock for the Gamma 602 is 525 MHz at 1150 mV. Pick a common value or type your own: {r.minMhz}–{r.maxMhz} MHz and {r.minMv}–{r.maxMv} mV. Your values are saved on the miner and never overwritten by a profile, schedule or auto-tune.</p>
  {#if freq != null}
  <div class="tg">
    <div class="fbox">
      <label class="f"><span>Frequency (MHz)</span><input data-testid="freq" type="number" inputmode="numeric" min={r.minMhz} max={r.maxMhz} step="1" bind:value={freq} /></label>
      <div class="chips">{#each FREQ_CHOICES as f}<button class:on={Number(freq) === f} onclick={() => freq = f}>{f}</button>{/each}</div>
    </div>
    <div class="fbox">
      <label class="f"><span>Core voltage (mV)</span><input data-testid="volt" type="number" inputmode="numeric" min={r.minMv} max={r.maxMv} step="5" bind:value={volt} /></label>
      <div class="chips">{#each VOLT_CHOICES as v}<button class:on={Number(volt) === v} onclick={() => volt = v}>{v}</button>{/each}</div>
    </div>
  </div>
  <div class="est mono small" data-testid="estimate">≈ {estThs(freq)} TH/s · roughly {watts} W input (estimate){#if lim} · your power limit {lim.power} W on a {lim.psu} W supply{/if}</div>
  {#if high}<div class="hi" data-testid="oc-warning">⚠ Above 700 MHz or 1200 mV needs strong cooling (good heatsink and fan) and a power supply that can deliver it. The chip, regulator and power limits still throttle the clock if they are reached.</div>{/if}
  {#if lim && watts > lim.power}<div class="warn small" data-testid="power-note">The estimate is above your {lim.power} W power limit, so the clock may be stepped down. Raise the limit or pick your power supply on <a href="#/tuning">Profiles &amp; Limits</a>.</div>{/if}
  {#if cs && cs.activeProfile !== 'custom'}<label class="chk small"><input type="checkbox" bind:checked={toManual} /> Switch to the Manual profile so these values run now (currently {cs.activeProfile}{cs.profileSource === 'schedule' ? ', set by a schedule' : ''})</label>{/if}
  {#if problem}<div class="crit small">{problem}</div>{/if}
  {#if err}<div class="crit" data-testid="tune-err">{err}</div>{/if}
  {#if saved}<div class="savedbox" data-testid="tune-saved"><b>✓ Saved on the miner:</b> <span class="mono">{saved.mhz} MHz / {saved.mv} mV</span></div>{/if}
  <div class="row"><button class="pri" onclick={apply} disabled={saving || !!problem} data-testid="tune-save">{saving ? 'Saving…' : 'Save tuning'}</button><span class="mute small">Applies within a few seconds, no restart needed.</span></div>
  {/if}
</div>
{#if cs}
<div class="card c-ok" data-testid="profile-note">
  <h2>Costean profile</h2>
  <p class="small">These are the values of the <b>Manual</b> profile (the default). {#if cs.activeProfile !== 'custom'}Right now the <b>{cs.activeProfile}</b> profile{cs.profileSource === 'schedule' ? ' (set by a schedule)' : ''} is running at {cs.applied?.mhz} MHz / {cs.applied?.mv} mV.{:else}Running now: {cs.applied?.mhz} MHz / {cs.applied?.mv} mV{cs.governor?.state === 'throttled' ? ` (throttled: ${cs.governor.reason})` : ''}.{/if}</p>
</div>
{/if}
<style>
.tg{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}.fbox{display:grid;gap:8px}
.fbox input{font:800 22px var(--mono);padding:10px 12px}
.chips{display:flex;flex-wrap:wrap;gap:5px}.chips button{padding:5px 9px;font:600 12px var(--mono);border-radius:8px}.chips button.on{background:var(--acc);color:#1a0f08;border-color:var(--acc)}
.est{color:var(--acc2)}
.hi{border:1px solid var(--warn);background:color-mix(in srgb,var(--warn) 12%,transparent);border-radius:10px;padding:9px 12px;font-size:13px}
.savedbox{border:1px solid color-mix(in srgb,var(--ok) 50%,transparent);background:color-mix(in srgb,var(--ok) 10%,transparent);border-radius:10px;padding:9px 12px}.savedbox b{color:var(--ok)}
</style>
