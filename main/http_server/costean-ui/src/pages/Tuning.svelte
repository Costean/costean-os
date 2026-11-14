<script>
  // Profiles, protective limits and the careful auto-tune. Every write goes through the confirm dialog.
  import { app, applyCostean } from '../lib/store.svelte.js';
  import { PROFILES, profileLabel, governorText } from '../lib/costean.js';
  import { clone } from '../lib/form.js';
  let st = $derived(app.costean?.status), cfg = $derived(app.costean?.config);
  let g = $derived(governorText(st));
  let lim = $state(null), at = $state(null);
  $effect(() => { if (cfg && !lim) { lim = clone(cfg.limits); at = clone(cfg.autotune); } });
  let i = $derived(app.info || {});
  let rg = $derived(st?.limitRanges || { chipTemp: [45, 73], vrTemp: [50, 103], power: [10, 35] });
  async function pick(id) {
    const turbo = id === 'turbo';
    await applyCostean({ profile: id }, `Switch to ${profileLabel(id)} profile`, turbo ? { danger: true, phrase: 'TURBO', warn: 'Turbo runs above the board maker\'s stock point. The limits below still throttle it, and the stock overheat shutdown stays on.' } : {});
  }
  async function saveLimits() { if (await applyCostean({ limits: lim }, 'Save protective limits')) lim = null; }
  async function saveTune(enable) {
    const p = { autotune: { ...at, enabled: enable ?? at.enabled } };
    const on = p.autotune.enabled && !cfg.autotune.enabled;
    if (await applyCostean(p, on ? 'Turn on auto-tune' : 'Save auto-tune settings', on ? { danger: true, phrase: 'AUTO-TUNE', warn: 'Auto-tune raises the clock one 25 MHz step at a time while there is temperature headroom, never raises voltage, and backs off on errors, rejects or heat. A failed step is not retried.' } : {})) at = null;
  }
  const bar = (v, l) => Math.min(100, Math.max(0, (v || 0) / (l || 1) * 100));
</script>
<h1>Profiles & Limits</h1><div class="mute">Pick how hard your Bitaxe Gamma 602 works. Manual (your own Mining & Tuning values) is the default. Costean OS throttles the clock before a limit is crossed instead of shutting down.</div>
{#if st && cfg}
<section class="card c-cyan live" data-testid="gov-card">
  <div class="cell"><div class="eyebrow">Running</div><div class="big">{profileLabel(st.activeProfile)}</div><div class="mute small">{st.profileSource === 'schedule' ? 'set by a schedule window' : 'your choice'}</div></div>
  <div class="cell"><div class="eyebrow">Target → applied</div><div class="big mono">{st.applied.mhz}<small> MHz</small></div><div class="mute small mono">target {st.target.mhz} MHz / {st.target.mv} mV · applied {st.applied.mv} mV</div></div>
  <div class="cell grow"><div class="eyebrow">Governor</div><div class="row"><span class="dot {g.level}"></span><b class={g.level}>{g.text}</b></div>
    <div class="mute small">chip errors {st.governor.errorPct?.toFixed(2)}% · rejects {st.governor.rejectPct?.toFixed(2)}% · auto-tune {cfg.autotune.enabled ? `on (+${st.governor.tuneSteps} steps, best stable +${st.governor.bestStableSteps})` : 'off'}</div></div>
</section>
<section class="grid g4" data-testid="presets">
  {#each PROFILES as p}
    {@const op = st.presets[p.id]}
    <button class="card preset" class:on={cfg.profile === p.id} onclick={() => pick(p.id)} disabled={cfg.profile === p.id} data-testid="preset-{p.id}">
      <div class="row"><b>{p.label}</b><span class="sp"></span>{#if cfg.profile === p.id}<span class="tag acc">selected</span>{:else if p.id === 'turbo'}<span class="tag">above stock</span>{/if}</div>
      <div class="pt mono">{op?.mhz}<small> MHz</small> · {op?.mv}<small> mV</small></div>
      <div class="mute small">{p.blurb}</div>
    </button>
  {/each}
</section>
<details class="card why" data-testid="why-602"><summary><b>Why these values on a Gamma 602?</b> <span class="mute small">one BM1370, 2040 small cores, stock 525 MHz / 1150 mV</span></summary>
  <ul class="small">
    <li><b>Eco 425 MHz / 1100 mV</b>: about 19 % less clock and 50 mV less core voltage than stock. Expect roughly 0.85 TH/s at about 13–14 W, cooler and quieter, usually the best J/TH.</li>
    <li><b>Stock 525 MHz / 1150 mV</b>: exactly the Bitaxe factory point, about 1.07 TH/s at roughly 16–18 W.</li>
    <li><b>Turbo 575 MHz / 1180 mV</b>: one modest 50 MHz step and +30 mV, about 1.17 TH/s at roughly 19–21 W. It stays far under the 25 W default power limit, and the limits still throttle it.</li>
    <li><b>Manual</b> (default): whatever you set on Mining &amp; Tuning, so nothing changes after the update until you pick another profile.</li>
    <li>Limits default to chip 68 °C, VR 90 °C and 25 W: below the stock overheat protection (chip 75 °C, VR 105 °C) and well inside a 5 V / 6 A brick. The throttle floor is 400 MHz; auto-tune never goes above 690 MHz or raises voltage.</li>
  </ul>
  <div class="mute small">Hashrate and power are estimates from the BM1370 core count; every chip and cooler differs.</div>
</details>
<div class="grid g2">
  <div class="card c-gold fm" data-testid="limits">
    <div class="row"><h2>Protective limits</h2><span class="sp"></span><span class="tag ok">throttle, not crash</span></div>
    {#if lim}
    {#each [['chipTemp', 'Chip', '°C', i.temp], ['vrTemp', 'Voltage regulator', '°C', i.vrTemp], ['power', 'Board power', 'W', i.power]] as [k, l, u, now]}
      {@const mn = rg[k][0]}{@const mx = rg[k][1]}
      <div class="lim"><div class="row small"><b>{l}</b><span class="sp"></span><span class="mute mono">now {now?.toFixed(1) ?? '—'} {u} · effective {st.effectiveLimits[k]} {u}</span></div>
        <div class="row"><input type="range" min={mn} max={mx} bind:value={lim[k]} /><span class="mono val">{lim[k]} {u}</span></div>
        <div class="meter"><i style:width="{bar(now, st.effectiveLimits[k])}%" class:hot={now >= st.effectiveLimits[k] - 3}></i></div></div>
    {/each}
    <div class="mute small">Over a limit the clock drops 25 MHz every 20 s down to {st.envelope.minMhz} MHz, then climbs back 25 MHz every 2 min once there is room. The stock overheat protection (chip {st.stockCutoffs?.chipTemp ?? 75} °C, VR {st.stockCutoffs?.vrTemp ?? 105} °C) stays on as the last line of defence; Costean limits are capped below it. If it ever trips, Costean OS switches to Manual and turns auto-tune off.</div>
    <div class="row"><span class="sp"></span><button onclick={() => lim = clone(cfg.limits)}>Revert</button><button class="pri" onclick={saveLimits}>Review & save</button></div>
    {/if}
  </div>
  <div class="card c-blue fm" data-testid="autotune">
    <div class="row"><h2>Auto-tune</h2><span class="sp"></span><span class="tag {cfg.autotune.enabled ? 'ok' : ''}">{cfg.autotune.enabled ? 'on' : 'off (default)'}</span></div>
    <div class="small">Carefully finds a little extra clock on <b>your</b> chips. Frequency only, never voltage. It needs 6 °C of headroom, holds each step to prove it is stable, and backs off for good if errors, rejects or heat go up.</div>
    {#if at}
    <div class="r2">
      <label class="f"><span>Max steps up (25 MHz each)</span><input type="number" min="1" max="4" bind:value={at.maxSteps} /></label>
      <label class="f"><span>Hold each step (min)</span><input type="number" min="5" max="120" bind:value={at.holdMin} /></label>
      <label class="f"><span>Back off above chip errors %</span><input type="number" step="0.1" min="0.2" max="3" bind:value={at.maxErrorPct} /></label>
      <label class="f"><span>Back off above rejects %</span><input type="number" step="0.1" min="0.2" max="5" bind:value={at.maxRejectPct} /></label>
    </div>
    <div class="row"><button onclick={() => applyCostean({ autotune: { reset: true } }, 'Forget auto-tune results and start over')}>Reset results</button><span class="sp"></span>
      {#if cfg.autotune.enabled}<button onclick={() => saveTune(false)} data-testid="tune-off">Turn off</button><button class="pri" onclick={() => saveTune()}>Save</button>
      {:else}<button onclick={() => saveTune(false)}>Save</button><button class="pri" onclick={() => saveTune(true)} data-testid="tune-on">Turn on…</button>{/if}</div>
    {/if}
  </div>
</div>
{:else}<div class="card mute">Loading Costean OS status…</div>{/if}
<style>.live{display:flex;gap:28px;flex-wrap:wrap;align-items:end}.cell{display:grid;gap:3px}.grow{flex:1;min-width:260px}.big small{font:600 13px var(--ui);color:var(--mute)}
.preset{text-align:left;display:grid;gap:6px;padding:14px 16px;border-radius:14px;cursor:pointer}.preset.on{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc) inset}.preset:disabled{opacity:1;cursor:default}
.preset b{font-size:16px}.pt{font-size:20px;font-weight:800}.pt small{font-size:11px;color:var(--mute)}
.fm{display:grid;gap:12px;align-content:start}.r2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.lim{display:grid;gap:4px}.lim input{flex:1;accent-color:var(--acc)}.val{min-width:60px;text-align:right}
.meter{height:5px;border-radius:3px;background:#0e1013;overflow:hidden}.meter i{display:block;height:100%;background:var(--acc2)}.meter i.hot{background:var(--warn)}.why summary{cursor:pointer;display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}.why ul{margin:10px 0 6px;padding-left:18px;display:grid;gap:4px}</style>
