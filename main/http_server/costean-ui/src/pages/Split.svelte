<script>
  // Multi-pool split: weighted job sharing across up to 4 pools with automatic failover.
  import { app, applySystem, runAction } from '../lib/store.svelte.js';
  import { api, fmtDiff } from '../lib/api.js';
  import { poolRows, effectiveSplit, validateSplit, evenSplit, splitBody } from '../lib/costean.js';
  let rows = $derived(poolRows(app.info));
  let st = $derived(app.costean?.status);
  let enabled = $derived(!!app.info?.poolSplitEnabled), active = $derived(!!app.info?.poolSplitActive);
  let w = $state(null), on = $state(null);
  $effect(() => { if (app.info && w == null) { w = poolRows(app.info).map(r => r.weight); on = !!app.info.poolSplitEnabled; } });
  let err = $derived(w ? validateSplit(w, on) : null);
  let sum = $derived(w ? w.reduce((a, b) => a + b, 0) : 0);
  let preview = $derived(w ? effectiveSplit(w, rows.map(r => r.inSplit ? r.healthy : true)) : []);
  let dirty = $derived(w != null && (on !== enabled || w.some((x, i) => x !== rows[i]?.weight)));
  const gh = v => v == null ? '—' : v >= 1000 ? (v / 1000).toFixed(2) + ' TH/s' : v.toFixed(0) + ' GH/s';
  const name = r => r?.host || 'pool';
  function even() { let idx = w.map((x, i) => x > 0 ? i : -1).filter(i => i >= 0); if (idx.length < 2) idx = [0, 1].filter(i => i < w.length); idx = idx.slice(0, 4); const e = evenSplit(idx.length); w = w.map((_, i) => idx.includes(i) ? e[idx.indexOf(i)] : 0); }
  async function save() {
    const body = splitBody(app.info, w, on);
    const label = on ? `Set pool split ${w.map((x, i) => `${name(rows[i]).split('.')[0]} ${x}%`).join(' / ')}` : 'Turn pool split off';
    const detail = `poolSplitEnabled: ${body.poolSplitEnabled}\n` + body.pools.map(p => `pool ${p.id}: ${p.stratumURL}:${p.stratumPort}  splitPercent ${p.splitPercent}`).join('\n') + '\n\nOther pool fields are sent back unchanged. The stored password is kept.';
    if (await applySystem(body, label, detail)) { w = null; }
  }
  const restart = () => runAction('Restart miner now to apply the pool set', () => api.restart(), { danger: true });
</script>
<h1>Pool Split</h1><div class="mute">Share your hashrate between up to 4 pools by percentage. If a pool goes down, its share moves to the others automatically and comes back 30 s after the pool recovers. Pools at 0 % are backups.</div>
{#if app.info && w}
{#if !rows.length}<div class="card c-gold">No pools set up yet. Add them on <a href="#/pools">Pools</a> first.</div>{/if}
{#if enabled && !active}
<div class="card c-gold row" data-testid="split-restart"><div><b>Split is saved but not running.</b> <span class="mute">{app.info.poolSplitError && app.info.poolSplitError !== 'ok' ? app.info.poolSplitError + '. ' : ''}Turning split on or changing which pools take part needs a restart.</span></div><span class="sp"></span><button class="pri" onclick={restart}>Restart…</button></div>
{:else if app.info.poolSplitRestartNeeded}
<div class="card c-gold row" data-testid="split-restart"><div><b>The pool set changed.</b> <span class="mute">Weights already apply live; adding or removing a pool needs a restart.</span></div><span class="sp"></span><button class="pri" onclick={restart}>Restart…</button></div>
{/if}
<section class="card c-cyan fm" data-testid="split-card">
  <div class="row"><h2>Split</h2>
    <label class="sw"><input type="checkbox" bind:checked={on} data-testid="split-toggle" /> <span>{on ? 'On' : 'Off: pool 1 mines, the fallback pool is the backup'}</span></label>
    <span class="sp"></span>
    {#if st?.failover || app.info.poolSplitFailover}<span class="tag warn" data-testid="failover-badge">failover active</span>{:else if active}<span class="tag ok">all pools healthy</span>{/if}</div>
  {#each rows as r, i}
    <div class="wrow" data-testid="split-row-{i}">
      <span class="dot p{i % 4}"></span><span class="mono h">{name(r)}{r.port ? ':' + r.port : ''}</span>
      <input type="range" min="0" max="100" step="1" bind:value={w[i]} disabled={!on} data-testid="split-slider-{i}" />
      <input class="num mono" type="number" min="0" max="100" bind:value={w[i]} disabled={!on} />
      <span class="mono pct">{w[i] ? w[i] + '%' : 'backup'}</span>
    </div>
  {/each}
  <div class="splitbar tall">{#each preview as p, i}<i class="p{i % 4}" style:width="{p}%"></i>{/each}</div>
  <div class="row small"><span class="mono" class:warn={on && sum !== 100}>total {sum}%</span><span class="mute">Effective now (after failover): {preview.map((p, i) => `${name(rows[i]).split('.')[0]} ${p}%`).join(' · ')}</span></div>
  {#if on && err}<div class="small warn" data-testid="split-error">{err}</div>{/if}
  <div class="row"><button disabled={!on} onclick={even}>Even split</button><span class="sp"></span>
    {#if dirty}<button onclick={() => w = null}>Revert</button><button class="pri" disabled={!!err} onclick={save} data-testid="save-split">Review & save</button>{/if}</div>
  <div class="mute small">Changing weights applies live, no restart. Turning split on or off, or adding a pool, needs a restart.</div>
</section>
<section class="grid g2">
  {#each rows as r}
    <div class="card pool c-{['cyan', 'blue', 'gold', 'ice'][r.i % 4]}" data-testid="split-pool-{r.i}">
      <div class="row"><span class="dot p{r.i % 4}"></span><h3 class="mono">{name(r)}{r.port ? ':' + r.port : ''}</h3><span class="sp"></span>
        {#if r.backup}<span class="tag">backup</span>{/if}
        <span class="pill"><span class="dot {!r.inSplit ? 'mute' : r.healthy ? 'ok' : 'crit'}"></span>{!r.inSplit ? 'idle' : r.healthy ? 'healthy' : 'down'}</span></div>
      <div class="nums">
        <div><div class="eyebrow">Weight</div><div class="n mono">{r.weight}%</div></div>
        <div><div class="eyebrow">Getting</div><div class="n mono" class:warn={r.effective != null && r.effective !== r.weight}>{r.effective ?? '—'}%</div></div>
        <div><div class="eyebrow">Job share</div><div class="n mono">{r.jobShare != null ? (r.jobShare * 100).toFixed(1) : '—'}<small>%</small></div></div>
        <div><div class="eyebrow">Pool credit</div><div class="n mono">{gh(r.credited)}</div></div>
      </div>
      <div class="row small mono"><span class="ok">{r.accepted} accepted</span><span class={r.rejected ? 'warn' : 'mute'}>{r.rejected} rejected ({r.rej.toFixed(2)}%)</span><span class="sp"></span>
        <span class="mute">diff {r.diff ? fmtDiff(r.diff) : '—'}</span></div>
    </div>
  {/each}
</section>
<section class="card fm">
  <h2>How failover works</h2>
  <ol class="small how">
    <li>Each new job goes to a pool picked by weighted round robin, so a 70 / 30 split is exact over time, not random.</li>
    <li>A pool that disconnects or has no work drops out at once. Its share goes to the healthy pools in proportion.</li>
    <li>When it comes back it must stay up for 30 s before it rejoins, so a flapping pool cannot steal work.</li>
    <li>If every weighted pool is down, the 0 % backups take the work until one recovers.</li>
    <li>Pool settings (hosts, workers, TLS) live on <a href="#/pools">Pools</a>. Pool credit is accepted share difficulty over the last 10 min, so it is noisy at low difficulty.</li>
  </ol>
</section>
{:else}<div class="card mute">Loading settings…</div>{/if}
<style>.fm{display:grid;gap:12px}.wrow{display:grid;grid-template-columns:14px minmax(160px,1.2fr) 2fr 70px 70px;gap:12px;align-items:center}
.wrow .dot{width:12px;height:12px;animation:none;box-shadow:none}.h{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.num{width:70px}.pct{text-align:right;font-weight:800}
.sw{display:flex;gap:8px;align-items:center;margin-left:14px}input[type=range]{accent-color:var(--acc)}.tall{height:22px;border-radius:9px}
.pool{display:grid;gap:10px}.pool h3{margin:0;font-size:14px}.pool .dot{animation:none;box-shadow:none}
.nums{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.n{font-size:20px;font-weight:800}.n small{font-size:11px;color:var(--mute)}
.how{margin:0;padding-left:18px;display:grid;gap:4px}
@media (max-width:800px){.wrow{grid-template-columns:14px 1fr 70px;}.wrow input[type=range]{grid-column:1/-1;grid-row:2}.pct{display:none}.nums{grid-template-columns:1fr 1fr}}</style>
