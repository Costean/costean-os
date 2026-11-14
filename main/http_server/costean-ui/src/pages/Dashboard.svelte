<script>
  import CosteanStrip from '../components/CosteanStrip.svelte';
  import NeonGauge from '../components/NeonGauge.svelte';
  import Chart from '../components/Chart.svelte';
  import { app, runAction } from '../lib/store.svelte.js';
  import { api, fmtDiff, fmtUptime } from '../lib/api.js';
  import { shortHost } from '../lib/costean.js';
  const i = $derived(app.info || {});
  const gh = $derived(Number(i.hashRate) || 0);
  const expected = $derived(Number(i.expectedHashrate) || (i.frequency && i.smallCoreCount ? i.frequency * i.smallCoreCount / 1000 : 0));
  const eff = $derived(gh > 0 && i.power ? i.power / (gh / 1000) : 0);
  const acc = $derived(Number(i.sharesAccepted) || 0), rej = $derived(Number(i.sharesRejected) || 0);
  const rr = $derived(acc + rej ? rej * 100 / (acc + rej) : 0);
  const rec = $derived(app.trend.slice(-180));
  const tc = v => v == null ? 'var(--mute)' : v >= 70 ? 'var(--crit)' : v >= 63 ? 'var(--warn)' : v >= 55 ? 'var(--ice)' : 'var(--acc)';
  const vc = v => v == null ? 'var(--mute)' : v >= 90 ? 'var(--crit)' : v >= 80 ? 'var(--warn)' : 'var(--acc2)';
  const status = $derived(!app.info ? '' : i.overheat_mode ? 'OVERHEAT MODE' : (i.vrTemp >= 90) ? 'VR HOT — CHECK AIRFLOW' : (i.uptimeSeconds < 300) ? 'RAMPING UP' : (expected && gh >= 0.94 * expected) ? 'AT TARGET' : 'BELOW TARGET');
  const pool = $derived(i.pools?.find(p => p.id === (i.isUsingFallbackStratum ? i.secondaryPoolIndex : i.primaryPoolIndex)) || i.pools?.[0]);
</script>
{#if app.info}
{#if i.overheat_mode}<div class="card crit">⚠ Overheat protection tripped. Cooling is forced to 100 % and Costean OS switched to the Manual profile with auto-tune off.</div>{/if}
<section class="hero">
  <div class="card gauge" data-testid="gauge">
    <div class="eyebrow">Measured hashrate</div>
    <NeonGauge value={gh} max={Math.max(expected * 1.15, gh || 1)} text={gh >= 1000 ? (gh / 1000).toFixed(2) : gh.toFixed(0)} unit={gh >= 1000 ? 'TH/s' : 'GH/s'} sub={'UP ' + fmtUptime(i.uptimeSeconds).toUpperCase()} />
    <div class="row status"><span class="dot {status === 'AT TARGET' ? 'ok' : 'warn'}"></span><b class={status === 'AT TARGET' ? 'ok' : 'warn'} data-testid="hash-status">{status}</b><span class="sp"></span><span class="mute small">{expected ? (gh / expected * 100).toFixed(1) + '% of ' + (expected / 1000).toFixed(2) + ' TH/s expected' : ''}</span></div>
  </div>
  <div class="vit">
    <div class="card stat"><div class="lbl">Chip</div><div class="v" style:color={tc(i.temp)}>{i.temp?.toFixed(1) ?? '—'}<small>°C</small></div></div>
    <div class="card stat c-blue"><div class="lbl">VR</div><div class="v" style:color={vc(i.vrTemp)}>{i.vrTemp?.toFixed(1) ?? '—'}<small>°C</small></div></div>
    <div class="card stat c-ice"><div class="lbl">Fan</div><div class="v">{i.fanspeed ?? '—'}<small>%</small></div><div class="mute small">{i.fanrpm ?? 0} rpm</div></div>
    <div class="card stat c-blue"><div class="lbl">Power</div><div class="v" style:color="var(--acc2)">{i.power?.toFixed(1) ?? '—'}<small>W</small></div><div class="mute small">{eff ? eff.toFixed(1) + ' J/TH' : ''} · {Number(i.voltage || 0) > 100 ? (i.voltage / 1000).toFixed(2) : Number(i.voltage || 0).toFixed(2)} V in</div></div>
    <div class="card stat"><div class="lbl">Core volt</div><div class="v">{i.coreVoltageActual != null ? Math.round(i.coreVoltageActual) : "—"}<small>mV</small></div><div class="mute small">requested {i.coreVoltage ?? '—'} mV</div></div>
    <div class="card stat c-cyan"><div class="lbl">Applied / set MHz</div><div class="v">{i.actualFrequency != null ? Math.round(i.actualFrequency) : '—'}<small>/ {i.frequency ?? '—'}</small></div><div class="mute small">errors {Number(i.errorPercentage || 0).toFixed(2)}%</div></div>
  </div>
</section>
<CosteanStrip />
<section class="grid g2">
  <div class="card c-ice"><h2>Hash trend</h2><Chart height={190} unit="GH/s" series={[{ name: 'Hashrate', color: '#d9733a', points: rec.map(x => ({ t: x.t, v: x.gh })) }]} /></div>
  <div class="card c-blue"><h2>Temperatures</h2><Chart height={190} unit="°C" series={[{ name: 'Chip', color: '#f59e0b', points: rec.map(x => ({ t: x.t, v: x.chip })) }, { name: 'VR', color: '#3fd8c2', points: rec.map(x => ({ t: x.t, v: x.vr })) }]} /></div>
</section>
<section class="strip card c-ok">
  <div><span class="eyebrow">Best</span><b class="num" style:color="var(--eblue)">{fmtDiff(i.bestDiff)}</b><span class="mute small">session {fmtDiff(i.bestSessionDiff)}</span></div>
  <div><span class="eyebrow">A / R</span><b class="num"><span class="ok">{acc}</span> / {rej}</b><span class="mute small">{rr.toFixed(2)}% rej</span></div>
  <div><span class="eyebrow">Pool</span><b class="mono small">{pool ? shortHost(pool.stratumURL) + ':' + pool.stratumPort : '—'}</b><span class="mute small">{i.poolSplitActive ? 'split active' : i.isUsingFallbackStratum ? 'on fallback pool' : 'primary pool'}{i.poolDifficulty ? ' · diff ' + fmtDiff(i.poolDifficulty) : ''}</span></div>
  <div><span class="eyebrow">IP address</span><b class="num">{i.ipv4 || '—'}</b></div>
</section>
<div class="card row">
  <button class="pri" onclick={() => runAction('Restart miner', () => api.restart(), { danger: true })}>Restart miner…</button>
  <a class="btn" href="#/block">Block Found</a><a class="btn" href="#/tuning">Profiles</a><a class="btn" href="#/split">Pool Split</a>
  <span class="sp"></span><span class="mute small mono">{i.firmwareName || 'Costean OS'} {i.version || ''}</span>
</div>
{:else}<div class="card mute">Connecting to miner…</div>{/if}
<style>
.hero{display:grid;grid-template-columns:minmax(300px,380px) 1fr;gap:14px}.gauge{display:grid;gap:6px}.status{border-top:1px solid var(--line);padding-top:10px}
.vit{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
.strip{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.strip>div{display:grid;gap:2px}.strip b{font-size:20px}
@media (max-width:1100px){.hero{grid-template-columns:1fr}.vit{grid-template-columns:repeat(2,1fr)}.strip{grid-template-columns:repeat(2,1fr)}}
</style>
