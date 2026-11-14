<script>
  // Dashboard strip: active profile, governor, pool split and schedule at a glance.
  import { app } from '../lib/store.svelte.js';
  import { profileLabel, governorText, poolRows, hhmm } from '../lib/costean.js';
  let st = $derived(app.costean?.status), g = $derived(governorText(st));
  let rows = $derived(poolRows(app.info).filter(r => r.inSplit));
  let split = $derived(!!app.info?.poolSplitActive);
</script>
{#if st}
<section class="card strip" data-testid="costean-strip">
  <a href="#/tuning" class="cell"><div class="eyebrow">Profile</div><div class="pv"><b>{profileLabel(st.activeProfile)}</b>{#if st.profileSource === 'schedule'}<span class="tag teal">schedule</span>{/if}</div>
    <div class="mute small mono">{st.applied?.mhz} MHz · {st.applied?.mv} mV</div></a>
  <a href="#/tuning" class="cell"><div class="eyebrow">Governor</div><div class="pv"><span class="dot {g.level}"></span><span class="small">{g.text}</span></div>
    <div class="mute small">limits {st.effectiveLimits?.chipTemp}°C chip · {st.effectiveLimits?.vrTemp}°C VR · {st.effectiveLimits?.power} W</div></a>
  <a href="#/split" class="cell wide"><div class="eyebrow">Pool split {#if st.failover}<span class="tag warn">failover</span>{/if}</div>
    {#if split}<div class="splitbar">{#each rows as r}<i class="p{r.i}" style:width="{r.effective ?? r.weight}%"></i>{/each}</div>
      <div class="mute small mono">{rows.filter(r => !r.backup || r.effective).map(r => `${r.host.split('.')[0]} ${r.effective ?? r.weight}%`).join('  ·  ')}{rows.some(r => r.backup) ? `  ·  ${rows.filter(r => r.backup).length} backup` : ''}</div>
    {:else}<div class="small">Split off: pool 1 mines, the fallback pool is the backup.</div>{/if}</a>
  <a href="#/schedules" class="cell"><div class="eyebrow">Schedule</div>
    <div class="pv small">{#if !app.costean.config?.schedule?.enabled}Off{:else if !st.schedule?.clockValid}Waiting for network time{:else if st.schedule.profileWindow >= 0 || st.schedule.lcdWindow >= 0}<b class="ok">{app.costean.config.schedule.windows[Math.max(st.schedule.profileWindow, st.schedule.lcdWindow)]?.name || 'Window active'}</b>{:else}No window active{/if}</div>
    {#if st.schedule?.nextChangeLocal}<div class="mute small">next change {hhmm(Math.floor((st.schedule.nextChangeLocal % 86400) / 60))}</div>{/if}</a>
</section>
{/if}
<style>.strip{display:grid;grid-template-columns:1fr 1.4fr 1.6fr 1fr;gap:0;padding:0}.cell{display:grid;gap:5px;padding:12px 16px;color:var(--text);text-decoration:none;border-left:1px solid var(--line);align-content:start}.cell:first-child{border-left:0}
.cell:hover{background:#ffffff05}.pv{display:flex;gap:8px;align-items:center}.pv b{font:800 18px var(--ui)}
@media (max-width:1100px){.strip{grid-template-columns:1fr 1fr}.cell:nth-child(3){border-left:0}}</style>
