<script>
  // Pools: URL, port, wallet address + worker, password. The form is filled once from the miner and is
  // not overwritten by the 2 s live refresh while you type. After Save it reads the pools back to prove
  // they were stored.
  import { app, refresh, toast } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  import { formFromInfo, validateForm, buildPoolsBody, clearedSlots, verifyPools, joinUser, isFilled } from '../lib/pools.js';
  let rows = $state(null);
  let err = $state('');
  let saving = $state(false);
  let saved = $state(null);   // { at, pools: [{id, url, user}] } after a verified save
  const prim = $derived(Number(app.info?.primaryPoolIndex ?? 0));
  const sec = $derived(Number(app.info?.secondaryPoolIndex ?? 1));
  $effect(() => { if (app.info && rows == null) rows = formFromInfo(app.info); });
  const dirty = $derived(rows != null && JSON.stringify(rows.map(({ existed, ...r }) => r)) !== JSON.stringify(formFromInfo(app.info).map(({ existed, ...r }) => r)));
  function role(id) { return id === prim ? 'main pool' : id === sec ? 'fallback' : app.info?.poolSplitEnabled ? 'split / backup' : 'spare'; }

  async function apply() {
    err = ''; saved = null;
    const e = validateForm(rows, { primary: prim });
    if (e) { err = e; return; }
    const body = buildPoolsBody(app.info, rows, false);
    delete body.poolSplitEnabled;           // split on/off lives on the Pool Split page
    const clear = clearedSlots(rows).filter(id => id !== prim && id !== sec);
    saving = true;
    try {
      await api.patch(body);
      for (const id of clear) await api.deletePool(id).catch(() => {});
      await refresh();
      const bad = verifyPools(app.info, body.pools);
      if (bad.length) { err = `The miner did not keep pool ${bad.map(i => i + 1).join(', ')}. Nothing was lost on your side: check the values and try again.`; return; }
      saved = { at: new Date(), pools: body.pools.map(p => ({ id: p.id, url: `${p.stratumURL}:${p.stratumPort}`, user: p.stratumUser })) };
      rows = formFromInfo(app.info);
      toast('Pools saved on the miner');
    } catch (x) {
      err = /Wrong API input/i.test(x.message || '') ? 'The miner rejected these values. Check the URL, port and address.' : (x.message || 'Save failed');
    } finally { saving = false; }
  }
  const restart = async () => { try { await api.restart(); toast('Restarting the miner…'); } catch { toast('Restart request failed', 'err'); } };
</script>
<div class="card c-cyan">
  <h2>Pools</h2>
  <p class="mute small">Your main pool, a fallback, and up to two more for Pool Split. The worker name is added to your address as <span class="mono">address.worker</span>. Leave a password blank to keep the stored one. There is no Costean dev fee.</p>
  {#if saved}<div class="savedbox" data-testid="pool-saved"><b>✓ Saved on the miner and read back</b>
    {#each saved.pools as p}<div class="mono small">Pool {p.id + 1}: {p.url} · {p.user}</div>{/each}
    <div class="row small"><span class="mute">Changes to the pool the miner is using now take effect after a restart.</span><span class="sp"></span><button onclick={restart} data-testid="pool-restart">Restart now</button></div></div>{/if}
  {#if err}<div class="crit" data-testid="pool-err">{err}</div>{/if}
</div>
{#if rows}
{#each rows as p (p.id)}
  <div class="card" class:c-ok={p.id === prim} data-testid="pool-{p.id}">
    <div class="row"><h2>Pool {p.id + 1}</h2><span class="tag {p.id === prim ? 'acc' : p.id === sec ? 'teal' : ''}">{role(p.id)}</span><span class="sp"></span>
      {#if app.info?.poolSplitStats?.[p.id] && app.info.poolSplitActive}<span class="mute small mono">{app.info.poolSplitStats[p.id].connected ? 'connected' : 'down'} · {app.info.poolSplitStats[p.id].sharesAccepted ?? 0} accepted</span>{/if}</div>
    <div class="pg">
      <label class="f url"><span>Pool URL</span><input data-testid="pool-{p.id}-url" bind:value={p.url} placeholder="stratum+tcp://pool.example.com" autocapitalize="none" autocorrect="off" spellcheck="false" /></label>
      <label class="f"><span>Port</span><input data-testid="pool-{p.id}-port" type="number" min="1" max="65535" bind:value={p.port} /></label>
      <label class="f wide"><span>Wallet address or pool username</span><input data-testid="pool-{p.id}-address" bind:value={p.address} placeholder="bc1q…" autocapitalize="none" autocorrect="off" spellcheck="false" /></label>
      <label class="f"><span>Worker name</span><input data-testid="pool-{p.id}-worker" bind:value={p.worker} placeholder="optional, e.g. gamma" autocapitalize="none" autocorrect="off" spellcheck="false" /></label>
      <label class="f"><span>Password</span><input type="password" bind:value={p.password} placeholder={p.existed ? 'unchanged if blank' : 'x'} autocomplete="off" /></label>
      <label class="f"><span>Protocol</span><select bind:value={p.protocol}><option value="SV1">Stratum V1</option><option value="SV2">Stratum V2</option></select></label>
    </div>
    {#if isFilled(p)}<div class="mute small mono">Pool user sent: {joinUser(p.address, p.worker) || '—'}</div>{/if}
  </div>
{/each}
<div class="row"><button class="pri" onclick={apply} disabled={saving} data-testid="pool-save">{saving ? 'Saving…' : 'Save pools'}</button>
  {#if dirty}<span class="tag warn">unsaved changes</span>{/if}<span class="mute small">Split percentages are set on <a href="#/split">Pool Split</a>.</span></div>
{:else}<div class="card mute">Loading pools…</div>{/if}
<style>
.pg{display:grid;grid-template-columns:2fr 1fr;gap:10px}.pg .wide{grid-column:1/-1}
@media (min-width:900px){.pg{grid-template-columns:2fr .7fr 2.4fr 1fr 1fr .9fr}.pg .wide{grid-column:auto}}
.savedbox{border:1px solid color-mix(in srgb,var(--ok) 50%,transparent);background:color-mix(in srgb,var(--ok) 10%,transparent);border-radius:10px;padding:10px 12px;display:grid;gap:4px;margin-top:8px}.savedbox b{color:var(--ok)}
</style>
