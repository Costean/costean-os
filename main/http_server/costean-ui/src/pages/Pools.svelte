<script>
  import { app, save, toast } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  let pools = $state([]);
  let splitOn = $state(false);
  let err = $state('');
  $effect(() => {
    if (app.info) {
      const src = app.info.pools || [];
      pools = [0,1,2,3].map(i => {
        const p = src.find(x => x.id === i) || src[i] || {};
        const host = p.stratumURL || p.url || '';
        const port = p.stratumPort || 3333;
        const url = host && !String(host).includes(':') ? `${host}:${port}` : host;
        return {
          id: p.id ?? i,
          url,
          user: p.stratumUser || p.user || '',
          password: '',
          protocol: p.stratumProtocol || p.protocol || 'SV1',
          splitPercent: p.splitPercent ?? 0,
        };
      });
      splitOn = !!app.info.poolSplitEnabled;
    }
  });
  const sum = $derived(pools.reduce((a,p) => a + (Number(p.splitPercent)||0), 0));
  const filled = $derived(pools.filter(p => p.url && p.user));
  function even() {
    const n = Math.max(filled.length, 2);
    pools = pools.map((p,i) => ({ ...p, splitPercent: (p.url && p.user && i < n) ? Math.floor(100/n) + (i < 100%n ? 1 : 0) : 0 }));
    // fix remainder
    const live = pools.filter(p => p.url && p.user).slice(0,4);
    if (live.length >= 2) {
      const base = Math.floor(100 / live.length);
      let rem = 100 - base * live.length;
      pools = pools.map(p => {
        if (!live.includes(p) && !(p.url && p.user)) return { ...p, splitPercent: 0 };
        const hit = live.find(x => x === p);
        if (!hit) return { ...p, splitPercent: 0 };
        const extra = rem > 0 ? 1 : 0; rem -= extra;
        return { ...p, splitPercent: base + extra };
      });
    }
  }
  async function apply() {
    err = '';
    if (splitOn) {
      const live = pools.filter(p => p.url && p.user);
      if (live.length < 2 || live.length > 4) { err = 'Split needs 2 to 4 pools with a URL and user.'; return; }
      if (live.some(p => (p.protocol||'SV1') === 'SV2' || p.protocol === 'STRATUM_V2')) {
        err = 'Multi-pool split is V1 only. Disable SV2 on split pools, or turn split off.';
        return;
      }
      const s = live.reduce((a,p)=>a+(Number(p.splitPercent)||0),0);
      if (s !== 100) { err = `Split percentages must sum to 100 (currently ${s}).`; return; }
    }
    const body = { poolSplitEnabled: splitOn };
    body.pools = pools.map(p => {
      let host = (p.url || '').trim();
      let port = 3333;
      const stripped = host.replace(/^stratum\+tcp:\/\//, '').replace(/^stratum\+ssl:\/\//, '');
      const idx = stripped.lastIndexOf(':');
      if (idx > 0 && /^\d+$/.test(stripped.slice(idx+1))) {
        host = stripped.slice(0, idx);
        port = Number(stripped.slice(idx+1));
      } else {
        host = stripped;
      }
      const o = {
        id: p.id,
        stratumURL: host,
        stratumPort: port,
        stratumUser: p.user,
        stratumProtocol: p.protocol || 'SV1',
        splitPercent: Number(p.splitPercent)||0,
        stratumPassword: p.password ? p.password : '*****',
      };
      return o;
    });
    const ok = await save(body, true);
    if (ok) toast('Pool settings saved. Restart to apply a split change.');
  }
</script>
<div class="card">
  <h2>Pools</h2>
  <p class="mute">The default pool is the official ESP-Miner default (public-pool.io, with solo.ckpool.org as fallback). There is no Costean dev fee and no extra hardcoded addresses.</p>
  <label class="chk"><input type="checkbox" bind:checked={splitOn} /> Enable multi-pool split (up to 4 V1 pools)</label>
  {#if splitOn}
    <p class="small">Split uses simultaneous V1 connections and a weighted time-sliced job scheduler on this single BM1370. Each share is submitted to the pool whose job produced it. SV2 is not compatible with split.</p>
    <div class="row"><button onclick={even}>Even split</button><span class="mono small">Sum {sum}%</span></div>
    {#if app.info?.poolSplitError}<div class="warn">{app.info.poolSplitError}</div>{/if}
    {#if app.info?.poolSplitActive}<div class="ok">Split is active on the miner.</div>{:else if app.info?.poolSplitEnabled}<div class="warn">Split is enabled in settings but not active yet — restart after a valid 2–4 pool, 100% V1 configuration.</div>{/if}
  {/if}
  {#if err}<div class="crit">{err}</div>{/if}
</div>
{#each pools as p, i}
  <div class="card">
    <h2>Pool {i+1}{i===0 ? ' (primary)' : i===1 ? ' (fallback when split is off)' : ''}</h2>
    <div class="grid g2">
      <label class="f"><span>URL</span><input bind:value={p.url} placeholder="stratum+tcp://host:3333" /></label>
      <label class="f"><span>User</span><input bind:value={p.user} /></label>
      <label class="f"><span>Password</span><input type="password" bind:value={p.password} placeholder="unchanged if blank" /></label>
      <label class="f"><span>Protocol</span>
        <select bind:value={p.protocol}><option value="SV1">Stratum V1</option><option value="SV2">Stratum V2</option></select>
      </label>
      {#if splitOn}
        <label class="f"><span>Split %</span><input type="number" min="0" max="100" bind:value={p.splitPercent} /></label>
      {/if}
    </div>
    {#if app.info?.poolSplitStats?.[i]}
      <div class="mute small mono">Live: {app.info.poolSplitStats[i].configuredPercent ?? 0}% · accepted {app.info.poolSplitStats[i].sharesAccepted ?? 0} · rejected {app.info.poolSplitStats[i].sharesRejected ?? 0} · {app.info.poolSplitStats[i].connected ? 'connected' : 'down'}</div>
    {/if}
  </div>
{/each}
<div class="row"><button class="pri" onclick={apply}>Save pools</button><span class="mute small">Enabling or changing split requires a restart.</span></div>
