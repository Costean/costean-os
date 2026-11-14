<script>
  import { app, toast } from '../lib/store.svelte.js';
  import { api, fmtDiff } from '../lib/api.js';
  const i = $derived(app.info || {});
  const d = v => typeof v === 'number' ? fmtDiff(v) : (v ?? '—');
  const board = $derived(Array.isArray(app.scoreboard) ? app.scoreboard : []);
  async function dismiss() {
    try {
      await api.dismissBlock();
      toast('Block banner dismissed');
      if (app.info) app.info.showNewBlock = false;
    } catch (e) { toast(e.message || 'Dismiss failed', 'err'); }
  }
</script>
{#if i.showNewBlock}
  <div class="card c-gold" data-testid="block-banner">
    <h2>Block found</h2>
    <p>This miner just found a block. Difficulty and scoreboard below use the live firmware fields.</p>
    <button class="pri" onclick={dismiss}>Dismiss</button>
  </div>
{/if}
<div class="grid g3">
  <div class="card c-gold stat"><div class="lbl">Best difficulty</div><div class="v" data-testid="best-diff">{d(i.bestDiff)}</div><div class="mute small">Session {d(i.bestSessionDiff)}</div></div>
  <div class="card c-cyan stat"><div class="lbl">Blocks found</div><div class="v" data-testid="block-found">{i.blockFound ?? 0}</div><div class="mute small">Since last boot (RAM)</div></div>
  <div class="card c-ice stat"><div class="lbl">Network difficulty</div><div class="v" data-testid="network-diff">{d(i.networkDifficulty)}</div></div>
</div>
<div class="card">
  <h2>Scoreboard</h2>
  {#if !board.length}
    <p class="mute">No scoreboard entries yet.</p>
  {:else}
    <table data-testid="scoreboard">
      <thead><tr><th>#</th><th>Difficulty</th><th>When</th><th>Job / note</th></tr></thead>
      <tbody>
        {#each board as row, n}
          <tr>
            <td>{n+1}</td>
            <td class="mono">{d(row.difficulty ?? row.diff ?? row.bestDiff)}</td>
            <td class="mono">{row.ntime ?? row.time ?? ''}</td>
            <td class="mono small">{row.job_id ?? row.jobId ?? ''}</td>
          </tr>
        {/each}
      </tbody>
    </table>
  {/if}
</div>
