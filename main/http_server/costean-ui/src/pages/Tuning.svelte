<script>
  // Profiles, protective limits and the auto-tune searcher. Every write goes through the confirm dialog.
  import { app, applyCostean } from '../lib/store.svelte.js';
  import { profileChoices, profileLabel, governorText, aboveStock, validateProfileEdit, profilesPatch, EDITABLE, PROFILE_RANGE, TUNER_GOALS, tunerPhase } from '../lib/costean.js';
  import { clone } from '../lib/form.js';
  import { PSU_PRESETS, PSU_RANGE, powerForPsu } from '../lib/tuning.js';
  let st = $derived(app.costean?.status), cfg = $derived(app.costean?.config);
  let g = $derived(governorText(st));
  let tu = $derived(st?.tuner);
  let tp = $derived(tunerPhase(st));
  let lim = $state(null), at = $state(null);
  $effect(() => { if (cfg && !lim) { lim = clone(cfg.limits); lim.psu ??= 30; at = clone({ maxSteps: 2, holdMin: 15, maxErrorPct: 1, maxRejectPct: 1, goal: 'efficiency', powerCap: 22, ...cfg.autotune }); } });
  let i = $derived(app.info || {});
  let rg = $derived(st?.limitRanges || { chipTemp: [45, 73], vrTemp: [50, 103], power: [10, 30], psu: PSU_RANGE });
  let rgEdit = $derived(lim ? { ...rg, power: [10, Number(lim.psu) || 30] } : rg);
  let psuCustom = $state(false);
  function pickPsu(w) { const n = Math.round(Number(w)); if (!lim || !Number.isFinite(n)) return; lim.psu = n; lim.power = powerForPsu(n); }
  $effect(() => { if (lim && !PSU_PRESETS.includes(Number(lim.psu))) psuCustom = true; });
  let choices = $derived(profileChoices(st));
  async function pick(id) {
    const op = st?.presets?.[id], hot = id !== 'custom' && aboveStock(op);
    await applyCostean({ profile: id }, `Switch to ${profileLabel(id)} profile`, hot ? { danger: true, phrase: 'TURBO', warn: `${profileLabel(id)} runs above the board maker's stock point (${op.mhz} MHz / ${op.mv} mV). The limits below still throttle it, and the stock overheat shutdown stays on.` } : {});
  }
  let rows = $state(null), profMsg = $state(null);
  const rowsFrom = s => EDITABLE.map(id => { const p = (s?.profiles || []).find(x => x.id === id); return p ? { id, name: p.name, defaultName: p.defaultName, mhz: p.mhz, mv: p.mv, enabled: p.enabled, edited: p.edited, def: p.default } : null; }).filter(Boolean);
  $effect(() => { if (st?.profiles && !rows) rows = rowsFrom(st); });
  const rowErr = r => validateProfileEdit({ ...r, mhz: Number(r.mhz), mv: Number(r.mv) });
  let profPatch = $derived(rows && st ? profilesPatch(rows.map(r => ({ ...r, mhz: Number(r.mhz), mv: Number(r.mv) })), st) : null);
  let profErr = $derived(rows ? rows.map(r => rowErr(r) && `${r.name || r.defaultName}: ${rowErr(r)}`).find(Boolean) : null);
  async function saveProfiles() {
    if (!profPatch || profErr) return;
    const hot = Object.entries(profPatch.profiles).filter(([, d]) => d.mhz && aboveStock(d));
    const sent = JSON.parse(JSON.stringify(profPatch.profiles));
    const ok = await applyCostean(profPatch, 'Save profiles', hot.length ? { danger: true, phrase: 'TURBO', warn: `Above the stock point (525 MHz / 1150 mV): ${hot.map(([id, d]) => `${rows.find(r => r.id === id)?.name || id} ${d.mhz} MHz / ${d.mv} mV`).join(', ')}. Higher clock and voltage mean more heat and power; the protective limits still throttle it.` } : {});
    if (!ok) return;
    const back = app.costean?.status?.profiles || [];
    const bad = Object.entries(sent).filter(([id, d]) => { const b = back.find(x => x.id === id); return !b || (d.mhz && (b.mhz !== d.mhz || b.mv !== d.mv)) || ('enabled' in d && b.enabled !== d.enabled); });
    profMsg = bad.length ? { ok: false, text: 'The miner did not keep: ' + bad.map(([id]) => id).join(', ') } : { ok: true, text: 'Saved on the miner and read back. Kept after a restart.' };
    rows = null;
  }
  async function resetProfile(id) {
    if (await applyCostean({ profiles: { [id]: { reset: true } } }, `Reset ${profileLabel(id)} to its default`)) { rows = null; profMsg = { ok: true, text: `${profileLabel(id)} is back to its default.` }; }
  }
  async function resetAll() {
    if (await applyCostean({ profilesReset: true }, 'Reset all profiles to the defaults', { warn: 'Eco, Stock and Turbo go back to 425/1100, 525/1150 and 575/1180, the names go back, and Custom 1 / Custom 2 are turned off.' })) { rows = null; profMsg = { ok: true, text: 'All profiles are back to the defaults.' }; }
  }
  async function saveLimits() { if (await applyCostean({ limits: lim }, 'Save protective limits')) lim = null; }
  async function saveTune(enable) {
    const p = { autotune: { ...at, enabled: enable ?? at.enabled } };
    const on = p.autotune.enabled && !cfg.autotune.enabled;
    if (await applyCostean(p, on ? 'Turn on auto-tune' : 'Save auto-tune settings', on ? { danger: true, phrase: 'AUTO-TUNE', warn: 'Auto-tune searches from the active profile. Best efficiency never raises voltage: it climbs while J/TH falls, then trims voltage down. A failed step is not retried. The protective limits still throttle first.' } : {})) at = null;
  }
  const bar = (v, l) => Math.min(100, Math.max(0, (v || 0) / (l || 1) * 100));
  const goalMeta = $derived(TUNER_GOALS.find(x => x.id === (at?.goal || tu?.goal || 'efficiency')) || TUNER_GOALS[0]);
</script>
<h1>Profiles & Limits</h1><div class="mute">Pick how hard your Bitaxe Gamma 601/602 works. Manual (your own Mining & Tuning values) is the default. Costean OS throttles the clock before a limit is crossed instead of shutting down.</div>
{#if st && cfg}
<section class="card c-cyan live" data-testid="gov-card">
  <div class="cell"><div class="eyebrow">Running</div><div class="big">{profileLabel(st.activeProfile)}</div><div class="mute small">{st.profileSource === 'schedule' ? 'set by a schedule window' : 'your choice'}</div></div>
  <div class="cell"><div class="eyebrow">Target → applied</div><div class="big mono">{st.applied.mhz}<small> MHz</small></div><div class="mute small mono">target {st.target.mhz} MHz / {st.target.mv} mV · applied {st.applied.mv} mV</div></div>
  <div class="cell grow"><div class="eyebrow">Governor</div><div class="row"><span class="dot {g.level}"></span><b class={g.level}>{g.text}</b></div>
    <div class="mute small">chip errors {st.governor.errorPct?.toFixed(2)}% · rejects {st.governor.rejectPct?.toFixed(2)}% · auto-tune {cfg.autotune.enabled ? `on` : 'off'}</div></div>
</section>

{#if tu?.enabled}
<section class="card c-gold tuner-strip" data-testid="tuner-strip">
  <div class="cell">
    <div class="eyebrow">Auto-tune</div>
    <div class="row"><span class="tag {tp.level === 'warn' ? 'warn' : 'ok'}" data-testid="tuner-pill">{tp.pill}</span><span class="mute small">{goalMeta.label}</span></div>
    <div class="mute small">{tp.text}</div>
  </div>
  <div class="cell"><div class="eyebrow">Trial point</div><div class="big mono">{tu.trialMhz}<small> MHz</small></div><div class="mute small mono">{tu.trialMv} mV</div></div>
  <div class="cell"><div class="eyebrow">Live efficiency</div><div class="big mono" style="color:var(--acc2)">{tu.jth?.toFixed(1)}<small> J/TH</small></div><div class="mute small">{tu.ghs ? (tu.ghs / 1000).toFixed(2) + ' TH/s' : ''}</div></div>
  <div class="cell grow">
    <div class="row"><div class="eyebrow">Progress</div><span class="sp"></span><span class="mono small">{tu.progress ?? 0}%</span></div>
    <div class="tbar" data-testid="tuner-progress"><i style:width="{(tu.progress ?? 0)}%"></i></div>
    <div class="mute small">{#if tu.best}Best {tu.best.mhz} MHz @ {tu.best.mv} mV · {(tu.best.ghs / 1000).toFixed(2)} TH/s · {tu.best.jth?.toFixed(1)} J/TH{:else}No stable point yet{/if}</div>
  </div>
</section>
<p class="tuner-note mute small" data-testid="tuner-note">{tu.note}</p>
{/if}

<div class="grid g2" data-testid="tuner-panel">
  <div class="card c-gold fm" data-testid="limits">
    <div class="row"><h2>Protective limits</h2><span class="sp"></span><span class="tag ok">throttle, not crash</span></div>
    {#if lim}
    <div class="psu" data-testid="psu">
      <div class="row small"><b>Power supply</b><span class="sp"></span><span class="mute">the power limit can go up to its rating</span></div>
      <div class="seg wrap">{#each PSU_PRESETS as w}<button class:on={!psuCustom && Number(lim.psu) === w} onclick={() => { psuCustom = false; pickPsu(w); }} data-testid="psu-{w}">{w} W</button>{/each}<button class:on={psuCustom} onclick={() => psuCustom = true} data-testid="psu-custom">Custom</button></div>
      {#if psuCustom}<label class="f"><span>Supply rating (W)</span><input type="number" min={rg.psu?.[0] ?? 10} max={rg.psu?.[1] ?? 150} value={lim.psu} onchange={(e) => pickPsu(e.currentTarget.value)} data-testid="psu-input" /></label>{/if}
      <div class="mute small">Picking a supply sets the power limit to 90 % of it ({powerForPsu(Number(lim.psu) || 30)} W on {lim.psu} W). You can move it up to the full {lim.psu} W below. Check the watts printed on your power brick; a 5 V supply's watts = volts × amps.</div>
    </div>
    <div class="lim-tiles" data-testid="limit-tiles">
    {#each [['chipTemp', 'Chip', '°C', i.temp], ['vrTemp', 'Voltage regulator', '°C', i.vrTemp], ['power', 'Board power', 'W', i.power]] as [k, l, u, now]}
      {@const mn = rgEdit[k][0]}{@const mx = rgEdit[k][1]}
      <div class="lim"><div class="row small"><b>{l}</b><span class="sp"></span><span class="mute mono">now {now?.toFixed(1) ?? '—'} {u} · effective {st.effectiveLimits[k]} {u}</span></div>
        <div class="row"><input type="range" min={mn} max={mx} bind:value={lim[k]} data-testid="lim-{k}" /><span class="mono val">{lim[k]} {u}</span></div>
        <div class="meter"><i style:width="{bar(now, st.effectiveLimits[k])}%" class:hot={now >= st.effectiveLimits[k] - 3}></i></div></div>
    {/each}
    </div>
    <div class="mute small">Over a limit the clock drops 25 MHz every 20 s down to {st.envelope.minMhz} MHz, then climbs back 25 MHz every 2 min once there is room. The stock overheat protection (chip {st.stockCutoffs?.chipTemp ?? 75} °C, VR {st.stockCutoffs?.vrTemp ?? 105} °C) stays on as the last line of defence; Costean limits are capped below it. If it ever trips, Costean OS switches to Manual and turns auto-tune off.</div>
    <div class="row"><span class="sp"></span><button onclick={() => lim = clone(cfg.limits)}>Revert</button><button class="pri" onclick={saveLimits} data-testid="lim-save">Review & save</button></div>
    {/if}
  </div>
  <div class="card c-blue fm" data-testid="autotune">
    <div class="row"><h2>Auto-tune</h2><span class="sp"></span><span class="tag {cfg.autotune.enabled ? 'ok' : ''}">{cfg.autotune.enabled ? 'on' : 'off (default)'}</span></div>
    <div class="small">Searches from the <b>active profile</b> on this miner. Best efficiency never raises voltage: it climbs while J/TH falls, then trims voltage down and keeps a 10 mV margin. The protective limits still throttle first.</div>
    {#if at}
    <div class="f" style="margin-top:8px">
      <span style="font-weight:600">Goal</span>
      <div class="seg wrap" data-testid="tuner-goal">
        {#each TUNER_GOALS as g}<button type="button" class:on={at.goal === g.id} onclick={() => at.goal = g.id} data-testid="goal-{g.id}">{g.label}</button>{/each}
      </div>
      <div class="mute small">{goalMeta.blurb}</div>
    </div>
    {#if at.goal === 'power'}
    <label class="f" data-testid="power-cap"><span>Power cap (W, 5 V input)</span>
      <input type="number" min="10" max={Number(lim?.psu) || 30} bind:value={at.powerCap} />
      <span class="mute">Cannot exceed the {lim?.psu || 30} W supply. The search stops 5 % early.</span>
    </label>
    {/if}
    <div class="mute small">25 MHz steps, 2-minute settle + 5-minute measurement, then a 10-minute recheck of the lock. Voltage is trimmed in 10 mV steps with a 10 mV margin. Best efficiency never raises voltage.</div>
    <div class="r2">
      <label class="f"><span>Back off above chip errors %</span><input type="number" step="0.1" min="0.2" max="3" bind:value={at.maxErrorPct} /></label>
      <label class="f"><span>Back off above rejects %</span><input type="number" step="0.1" min="0.2" max="5" bind:value={at.maxRejectPct} /></label>
    </div>
    {#if tu?.steps?.length}
    <div class="slog" data-testid="step-log">
      <div class="eyebrow">Step log</div>
      <table>
        <thead><tr><th>Point</th><th>J/TH</th><th>Result</th></tr></thead>
        <tbody>
          {#each tu.steps as s}
            <tr><td class="mono">{s.mhz} / {s.mv}</td><td class="mono">{s.jth?.toFixed(1)}</td><td>{s.result}</td></tr>
          {/each}
        </tbody>
      </table>
    </div>
    {/if}
    <div class="row">
      <button onclick={() => applyCostean({ autotune: { restart: true } }, 'Forget the locked point and start a new search')} data-testid="tune-restart">Restart search</button>
      <button onclick={() => applyCostean({ autotune: { reset: true } }, 'Forget auto-tune results and start over')}>Reset results</button>
      <span class="sp"></span>
      {#if cfg.autotune.enabled}<button onclick={() => saveTune(false)} data-testid="tune-off">Turn off</button><button class="pri" onclick={() => saveTune()}>Save</button>
      {:else}<button onclick={() => saveTune(false)}>Save</button><button class="pri" onclick={() => saveTune(true)} data-testid="tune-on">Turn on…</button>{/if}
    </div>
    {/if}
  </div>
</div>

<section class="grid g4" data-testid="presets">
  {#each choices as p}
    {@const op = st.presets[p.id]}
    <button class="card preset" class:on={cfg.profile === p.id} onclick={() => pick(p.id)} disabled={cfg.profile === p.id} data-testid="preset-{p.id}">
      <div class="row"><b>{p.label}</b><span class="sp"></span>{#if cfg.profile === p.id}<span class="tag acc">selected</span>{:else if p.id !== 'custom' && aboveStock(op)}<span class="tag">above stock</span>{/if}{#if p.edited && p.id !== 'custom'}<span class="tag teal">edited</span>{/if}</div>
      <div class="pt mono">{op?.mhz}<small> MHz</small> · {op?.mv}<small> mV</small></div>
      <div class="mute small">{#if p.edited && p.id !== 'custom'}{@const d = st.profiles?.find(x => x.id === p.id)?.default}Your values{d ? ` (default ${d.mhz} / ${d.mv})` : ''}. Change them under Edit profiles.{:else}{p.blurb}{/if}</div>
    </button>
  {/each}
</section>
<section class="card c-gold fm" data-testid="profile-editor">
  <div class="row"><h2>Edit profiles</h2><span class="sp"></span><span class="mute small">{PROFILE_RANGE.mhz[0]}–{PROFILE_RANGE.mhz[1]} MHz · {PROFILE_RANGE.mv[0]}–{PROFILE_RANGE.mv[1]} mV</span></div>
  <div class="small mute">Set each profile to what you want, rename it, and turn on up to two extra profiles. Saved on the miner (kept after restarts); schedules and auto-tune use these values. Manual is set on Mining &amp; Tuning.</div>
  {#if rows}
  <div class="ptab">
    <div class="ph mute small"><span>Profile</span><span>Name</span><span>MHz</span><span>mV</span><span></span></div>
    {#each rows as r (r.id)}
      {@const e = rowErr(r)}
      <div class="pr" class:off={r.id.startsWith('user') && !r.enabled} data-testid="prof-{r.id}">
        <span class="pid">{#if r.id.startsWith('user')}<label class="chk"><input type="checkbox" bind:checked={r.enabled} data-testid="prof-{r.id}-on" /> {r.defaultName}</label>{:else}<b>{r.defaultName}</b>{/if}
          <i class="mute small">default {r.def?.mhz}/{r.def?.mv}</i></span>
        <input maxlength="12" placeholder={r.defaultName} bind:value={r.name} data-testid="prof-{r.id}-name" />
        <input type="number" min="400" max="1000" step="5" bind:value={r.mhz} class:bad={e && /frequency/.test(e)} data-testid="prof-{r.id}-mhz" />
        <input type="number" min="1000" max="1300" step="5" bind:value={r.mv} class:bad={e && /voltage/.test(e)} data-testid="prof-{r.id}-mv" />
        <span class="pa">{#if aboveStock({ mhz: Number(r.mhz), mv: Number(r.mv) })}<span class="tag warn">above stock</span>{/if}{#if r.edited}<button class="small" onclick={() => resetProfile(r.id)} data-testid="prof-{r.id}-reset">Default</button>{/if}</span>
      </div>
    {/each}
  </div>
  {#if profErr}<div class="crit small" data-testid="prof-error">{profErr}</div>{/if}
  {#if profMsg}<div class="small {profMsg.ok ? 'ok' : 'crit'}" role="status" data-testid="prof-msg">{profMsg.ok ? '✓' : '✗'} {profMsg.text}</div>{/if}
  <div class="row"><button onclick={resetAll} data-testid="prof-reset-all">Reset all to defaults</button><span class="sp"></span><button onclick={() => { rows = rowsFrom(st); profMsg = null; }}>Revert</button><button class="pri" disabled={!profPatch || !!profErr} onclick={saveProfiles} data-testid="prof-save">Review &amp; save</button></div>
  {/if}
</section>
<details class="card why" data-testid="why-602"><summary><b>Why these default values on a Gamma 601/602?</b> <span class="mute small">one BM1370, 2040 small cores, stock 525 MHz / 1150 mV</span></summary>
  <ul class="small">
    <li><b>Eco 425 MHz / 1100 mV</b>: about 19 % less clock and 50 mV less core voltage than stock. Expect roughly 0.85 TH/s at about 13–14 W, cooler and quieter, usually the best J/TH.</li>
    <li><b>Stock 525 MHz / 1150 mV</b>: exactly the Bitaxe factory point, about 1.07 TH/s at roughly 16–18 W. Auto-tune searches start from the active profile, usually this one.</li>
    <li><b>Turbo 575 MHz / 1180 mV</b>: one modest 50 MHz step and +30 mV, about 1.17 TH/s at roughly 19–21 W. It stays under the default power limit, and the limits still throttle it.</li>
    <li><b>Manual</b> (default): whatever you set on Mining &amp; Tuning, so nothing changes after the update until you pick another profile.</li>
    <li>Best efficiency measures this chip and usually lands near or below Eco on its own, by trimming voltage at the best clock rather than using a fixed 425/1100.</li>
    <li>Limits default to chip 68 °C, VR 90 °C and 27 W (90 % of the stock 5 V / 6 A = 30 W brick). The throttle floor is 400 MHz; a search never goes above 1000 MHz or 1300 mV.</li>
  </ul>
  <div class="mute small">Hashrate and power are estimates from the BM1370 core count; every chip and cooler differs.</div>
</details>
{:else}<div class="card mute">Loading Costean OS status…</div>{/if}
<style>.ptab{display:grid;gap:6px}.ph,.pr{display:grid;grid-template-columns:minmax(150px,1.3fr) minmax(110px,1fr) 90px 90px minmax(120px,auto);gap:8px;align-items:center}
.pr.off input:not([type=checkbox]){opacity:.45}.pid{display:grid;gap:1px}.pa{display:flex;gap:6px;align-items:center;justify-content:end}input.bad{border-color:var(--crit)}
@media (max-width:700px){.ph{display:none}.pr{grid-template-columns:1fr 1fr;padding-bottom:8px;border-bottom:1px solid var(--line)}.pid{grid-column:1/-1}}
.live{display:flex;gap:28px;flex-wrap:wrap;align-items:end}.cell{display:grid;gap:3px}.grow{flex:1;min-width:260px}.big small{font:600 13px var(--ui);color:var(--mute)}
.preset{text-align:left;display:grid;gap:6px;padding:14px 16px;border-radius:14px;cursor:pointer}.preset.on{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc) inset}.preset:disabled{opacity:1;cursor:default}
.preset b{font-size:16px}.pt{font-size:20px;font-weight:800}.pt small{font-size:11px;color:var(--mute)}
.fm{display:grid;gap:12px;align-content:start}.r2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.lim{display:grid;gap:4px}.psu{display:grid;gap:6px;padding-bottom:6px;border-bottom:1px solid var(--line)}.seg.wrap{flex-wrap:wrap}.lim input{flex:1;accent-color:var(--acc)}.val{min-width:60px;text-align:right}
.meter{height:5px;border-radius:3px;background:#0e1013;overflow:hidden}.meter i{display:block;height:100%;background:var(--acc2)}.meter i.hot{background:var(--warn)}.why summary{cursor:pointer;display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}.why ul{margin:10px 0 6px;padding-left:18px;display:grid;gap:4px}
.tuner-strip{display:flex;gap:24px;flex-wrap:wrap;align-items:end}
.tbar{height:6px;border-radius:4px;background:#0e1013;overflow:hidden;border:1px solid var(--line)}.tbar i{display:block;height:100%;background:linear-gradient(90deg,var(--acc),var(--acc2))}
.tuner-note{margin:-6px 4px 0}
.slog{display:grid;gap:6px}.slog table{font-size:12px}.slog td,.slog th{padding:5px 6px}
@media (max-width:700px){.r2{grid-template-columns:1fr}.tuner-strip{gap:14px}}</style>
