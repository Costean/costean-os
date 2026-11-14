<script>
  // 48 hours of 15-minute points kept on the miner. They survive a reboot.
  import { app, loadRollup } from '../lib/store.svelte.js';
  import { decodeRollup, rollupSummary } from '../lib/costean.js';
  import Chart from '../components/Chart.svelte';
  let h = $derived(decodeRollup(app.rollup)), pts = $derived(h.points), sum = $derived(rollupSummary(pts));
  let span = $state(48);
  let view = $derived(pts.slice(-span * 4));
  const S = (k, name, color) => ({ name, color, points: view.map(p => ({ t: p.t, v: p[k] })) });
  const events = $derived(view.filter(p => p.throttled || p.failover || p.scheduled));
  const strip = k => view.map(p => p[k]);
</script>
<h1>History</h1><div class="mute">Long history kept on the miner: one point every 15 minutes for 48 hours. Saved to flash every 30 minutes and before a restart, so it survives a reboot.</div>
<div class="row"><div class="seg">{#each [6, 12, 24, 48] as s}<button class:on={span === s} onclick={() => span = s}>{s} h</button>{/each}</div><span class="sp"></span>
  <span class="mute small">{sum ? `${sum.hours.toFixed(1)} h recorded · ${h.gaps} gaps (miner off or not mining)` : ''}</span><button onclick={loadRollup}>Refresh</button></div>
{#if sum}
<section class="grid g4" data-testid="hist-summary">
  <div class="card stat c-cyan"><div class="lbl">Avg hashrate</div><div class="v">{sum.avgThs.toFixed(2)}<small>TH/s</small></div></div>
  <div class="card stat c-blue"><div class="lbl">Avg power</div><div class="v">{sum.avgW.toFixed(1)}<small>W</small></div><div class="mute small">{sum.kwh.toFixed(2)} kWh in {sum.hours.toFixed(0)} h</div></div>
  <div class="card stat c-ice"><div class="lbl">Efficiency</div><div class="v">{sum.avgJth.toFixed(1)}<small>J/TH</small></div></div>
  <div class="card stat c-gold"><div class="lbl">Hottest chip</div><div class="v">{sum.maxChip.toFixed(1)}<small>°C</small></div><div class="mute small">throttled {sum.throttledH.toFixed(2)} h · failover {sum.failoverH.toFixed(2)} h</div></div>
</section>
<div class="card c-cyan" data-testid="hist-hash"><h2>Hashrate</h2><Chart height={200} unit="TH/s" days series={[S('ths', 'TH/s', '#ee8a4c')]} />
  <div class="flags" title="Events per 15 minutes">
    <div class="fl"><span class="mute">Throttle</span><div class="bars">{#each strip('throttled') as f}<i class:on={f} class="t"></i>{/each}</div></div>
    <div class="fl"><span class="mute">Failover</span><div class="bars">{#each strip('failover') as f}<i class:on={f} class="f"></i>{/each}</div></div>
    <div class="fl"><span class="mute">Sched</span><div class="bars">{#each strip('scheduled') as f}<i class:on={f} class="s"></i>{/each}</div></div>
  </div></div>
<div class="grid g2">
  <div class="card c-blue"><h2>Power</h2><Chart height={170} unit="W" days series={[S('w', 'Watts', '#3fd8c2')]} /></div>
  <div class="card c-gold"><h2>Temperatures</h2><Chart height={170} unit="°C" days series={[S('chip', 'Hottest chip', '#ffb547'), S('vr', 'Regulator', '#7fe6d6')]} /></div>
  <div class="card c-ice"><h2>Efficiency</h2><Chart height={170} unit="J/TH" days series={[S('jth', 'J/TH', '#f6cb6e')]} /></div>
  <div class="card"><h2>Events</h2>{#if events.length}<div class="mute small">{events.length} of {view.length} points had a throttle, failover or schedule window. The bars under the hashrate chart line up with time.</div>{:else}<div class="mute small">No throttling, failover or schedule windows in this range.</div>{/if}</div>
</div>
{:else}<div class="card mute">No long history yet. The first point appears 15 minutes after mining starts with network time.</div>{/if}
<style>.flags{display:grid;gap:3px;margin:6px 10px 0 44px}.fl{position:relative}.fl span{position:absolute;right:100%;margin-right:5px;top:-3px;font-size:9px;white-space:nowrap}
.bars{display:flex;gap:1px;height:8px}.bars i{flex:1;background:#14161b;border-radius:1px}.bars i.on.t{background:var(--warn)}.bars i.on.f{background:var(--crit)}.bars i.on.s{background:var(--acc2)}</style>
