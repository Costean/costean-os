<script>
  // Time windows that switch the profile and/or the OLED screen. Evaluated on the miner with its own time zone rules.
  import { app, applyCostean } from '../lib/store.svelte.js';
  import { clone } from '../lib/form.js';
  import { PROFILES, LCD_MODES, DAYS, hhmm, parseHHMM, daysLabel, toggleDay, validateSchedule, weekGrid, blankWindow, profileLabel, windowMinutes, ALL_DAYS, WEEKDAYS, WEEKEND } from '../lib/costean.js';
  let cfg = $derived(app.costean?.config), st = $derived(app.costean?.status);
  let e = $state(null);
  $effect(() => { if (cfg && !e) e = clone(cfg.schedule); });
  let err = $derived(e ? validateSchedule(e.windows) : null);
  let grid = $derived(e ? weekGrid(e.enabled ? e.windows : []) : []);
  let dirty = $derived(e && cfg && JSON.stringify(e) !== JSON.stringify(cfg.schedule));
  const local = $derived(st?.schedule?.localTime ? new Date(st.schedule.localTime * 1000) : null);
  const fmtLocal = d => d ? `${DAYS[d.getUTCDay()]} ${hhmm(d.getUTCHours() * 60 + d.getUTCMinutes())}` : '—';
  const setTime = (w, k, v) => { const m = parseHHMM(v); if (m != null) w[k] = m; };
  const COL = { eco: 'var(--acc2)', stock: '#8e939c', custom: '#cfc8bd', turbo: 'var(--acc)' };
  async function save() {
    const turbo = e.windows.some(w => w.on && w.profile === 'turbo');
    if (await applyCostean({ schedule: e }, 'Save schedule', turbo ? { danger: true, phrase: 'TURBO', warn: 'A window uses the Turbo profile, which runs above stock. Limits still throttle it.' } : {})) e = null;
  }
</script>
<h1>Schedules</h1><div class="mute">Quiet nights, an eco workday, the screen off while you sleep. When windows overlap, the lowest-power profile and the darkest screen win.</div>
{#if e && st}
<section class="card c-cyan status" data-testid="sched-status">
  <div><div class="eyebrow">Miner clock</div><div class="big mono">{fmtLocal(local)}</div><div class="mute small">{e.tz.replace('_', ' ')}{st.schedule.dst ? ' · daylight time' : ''}</div></div>
  <div><div class="eyebrow">Now</div><div class="row">{#if !st.schedule.clockValid}<span class="tag warn">waiting for network time</span>{:else if st.schedule.profileWindow < 0 && st.schedule.lcdWindow < 0}<span class="tag">no window active</span>{:else}
    {#if st.schedule.profileWindow >= 0}<span class="tag teal">{cfg.schedule.windows[st.schedule.profileWindow]?.name || 'window'} · {profileLabel(st.schedule.profile)}</span>{/if}
    {#if st.schedule.lcdWindow >= 0}<span class="tag acc">screen {st.schedule.lcd}</span>{/if}{/if}</div>
    <div class="mute small">{st.schedule.nextChangeLocal ? 'Next change ' + fmtLocal(new Date(st.schedule.nextChangeLocal * 1000)) : 'No change in the next 8 days'}</div></div>
  <span class="sp"></span>
  <label class="chk"><input type="checkbox" bind:checked={e.enabled} data-testid="sched-enabled" /> Schedules on</label>
  <label class="f tz"><span>Time zone</span><select bind:value={e.tz}>{#each st.zones as z}<option value={z}>{z.replace('_', ' ')}</option>{/each}</select></label>
</section>
<section class="card c-blue" data-testid="week">
  <div class="row"><h2>Week at a glance</h2><span class="sp"></span>
    {#each PROFILES as p}<span class="lg"><i style:background={COL[p.id]}></i>{p.label}</span>{/each}<span class="lg"><i class="dimsw"></i>Screen dim / off</span></div>
  <div class="week">
    <div></div>{#each Array(24) as _, h}<div class="hh mute">{h % 3 ? '' : String(h).padStart(2, '0')}</div>{/each}
    {#each grid as row, d}<div class="dd mute">{DAYS[d]}</div>{#each row as c}<div class="cell" class:dim={c?.lcd === 'dim'} class:off={c?.lcd === 'off'} style:background={c?.profile ? COL[c.profile] : null}></div>{/each}{/each}
  </div>
</section>
<section class="grid wins" data-testid="windows">
  {#each e.windows as w, i}
    <div class="card win" class:disabled={!w.on}>
      <div class="row"><label class="chk"><input type="checkbox" bind:checked={w.on} /></label><input class="nm" maxlength="19" bind:value={w.name} placeholder="Name" />
        <span class="sp"></span><span class="mute small">{daysLabel(w.days)} · {hhmm(w.start)}–{hhmm(w.end)}{w.end <= w.start ? ' (next day)' : ''} · {(windowMinutes(w) / 60).toFixed(1)} h</span>
        <button class="x" onclick={() => e.windows.splice(i, 1)} aria-label="Delete window">✕</button></div>
      <div class="row">
        <div class="days">{#each DAYS as dn, di}<button class:on={w.days & (1 << di)} onclick={() => w.days = toggleDay(w.days, di)}>{dn[0]}{dn[1]}</button>{/each}</div>
        <div class="seg"><button onclick={() => w.days = ALL_DAYS}>All</button><button onclick={() => w.days = WEEKDAYS}>Mon–Fri</button><button onclick={() => w.days = WEEKEND}>Sat–Sun</button></div>
      </div>
      <div class="r4">
        <label class="f"><span>From</span><input type="time" value={hhmm(w.start)} onchange={ev => setTime(w, 'start', ev.currentTarget.value)} /></label>
        <label class="f"><span>To</span><input type="time" value={hhmm(w.end)} onchange={ev => setTime(w, 'end', ev.currentTarget.value)} /></label>
        <label class="f"><span>Profile</span><select bind:value={w.profile}><option value="none">No change</option>{#each PROFILES as p}<option value={p.id}>{p.label}</option>{/each}</select></label>
        <label class="f"><span>Screen</span><select bind:value={w.lcd}>{#each LCD_MODES as [v, l]}<option value={v}>{l}</option>{/each}</select></label>
      </div>
    </div>
  {/each}
</section>
<div class="row">
  <button onclick={() => e.windows.push(blankWindow())} disabled={e.windows.length >= 8} data-testid="add-window">+ Add window</button>
  <button onclick={() => e.windows.push({ on: true, days: WEEKDAYS, start: 9 * 60, end: 17 * 60, profile: 'eco', lcd: 'normal', name: 'Workday eco' })} disabled={e.windows.length >= 8}>+ Workday eco</button>
  <span class="mute small">{e.windows.length} / 8 windows. A button press wakes a scheduled-off screen for 30 s.</span>
  <span class="sp"></span>{#if err}<span class="crit small" data-testid="sched-err">{err}</span>{/if}
  {#if dirty}<button onclick={() => e = clone(cfg.schedule)}>Revert</button>{/if}<button class="pri" disabled={!!err || !dirty} onclick={save} data-testid="save-sched">Review & save</button>
</div>
{:else}<div class="card mute">Loading schedules…</div>{/if}
<style>.status{display:flex;gap:28px;flex-wrap:wrap;align-items:center}.tz{min-width:200px}
.lg{display:inline-flex;gap:6px;align-items:center;font-size:12px;color:var(--mute)}.lg i{width:12px;height:12px;border-radius:3px;display:inline-block}.dimsw{background:repeating-linear-gradient(45deg,#3a3f49 0 3px,#0e1013 3px 6px);border:1px solid var(--line2)}
.week{display:grid;grid-template-columns:40px repeat(24,1fr);gap:3px;margin-top:8px}.hh{font:10px var(--mono)}.dd{font-size:11px;align-self:center}
.cell{height:22px;border-radius:4px;background:#14161b;border:1px solid var(--line);position:relative}.cell.dim::after,.cell.off::after{content:'';position:absolute;inset:0;border-radius:3px;background:repeating-linear-gradient(45deg,#0008 0 3px,transparent 3px 6px)}
.cell.off::after{background:repeating-linear-gradient(45deg,#000c 0 3px,#0004 3px 6px)}
.wins{grid-template-columns:repeat(auto-fit,minmax(460px,1fr))}.win{display:grid;gap:10px}.win.disabled{opacity:.55}.nm{max-width:200px;font-weight:700}
.x{padding:4px 9px}.days{display:flex;gap:3px}.days button{padding:5px 8px;font-size:12px;border-radius:7px}.days button.on{background:var(--acc);color:#1a0f08;border-color:var(--acc);font-weight:700}
.r4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}input[type=time]{color-scheme:dark}</style>
