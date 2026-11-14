<script>
  import { onMount } from 'svelte';
  import { app, start } from './lib/store.svelte.js';
  import { profileLabel } from './lib/costean.js';
  import { mismatch } from './lib/versions.js';
  import Confirm from './components/Confirm.svelte';
  import Dashboard from './pages/Dashboard.svelte';
  import Tuning from './pages/Tuning.svelte';
  import Split from './pages/Split.svelte';
  import Schedules from './pages/Schedules.svelte';
  import History from './pages/History.svelte';
  import Mining from './pages/Mining.svelte';
  import Pools from './pages/Pools.svelte';
  import Thermal from './pages/Thermal.svelte';
  import BlockFound from './pages/BlockFound.svelte';
  import Alerts from './pages/Alerts.svelte';
  import System from './pages/System.svelte';
  import Diagnostics from './pages/Diagnostics.svelte';
  import Update from './pages/Update.svelte';
  const NAV = [
    ['dashboard', 'Dashboard'], ['tuning', 'Profiles & Limits'], ['split', 'Pool Split'], ['schedules', 'Schedules'], ['history', 'History'],
    ['mining', 'Mining & Tuning'], ['pools', 'Pools'], ['thermal', 'Thermal'], ['block', 'Block Found'], ['alerts', 'Alerts'],
    ['system', 'System & Network'], ['diagnostics', 'Diagnostics'], ['update', 'Update & Recovery'],
  ];
  const PAGES = { dashboard: Dashboard, tuning: Tuning, split: Split, schedules: Schedules, history: History, mining: Mining, pools: Pools, thermal: Thermal, block: BlockFound, alerts: Alerts, system: System, diagnostics: Diagnostics, update: Update };
  const ICON = { dashboard: '◆', tuning: '◈', split: '◐', schedules: '◷', history: '▤', mining: '⛏', pools: '◎', thermal: '❄', block: '◇', alerts: '!', system: '⌁', diagnostics: '⚙', update: '⟳' };
  let menu = $state(false);
  let mm = $derived(mismatch(app.info?.version, app.wwwVersion?.www));
  onMount(() => {
    start();
    const onHash = () => app.page = location.hash.slice(2).split('?')[0] || 'dashboard'; addEventListener('hashchange', onHash);
    return () => removeEventListener('hashchange', onHash);
  });
  const Page = $derived(PAGES[app.page] || Dashboard);
  const title = $derived((NAV.find(n => n[0] === app.page) || NAV[0])[1]);
  const host = $derived(app.info?.hostname || 'bitaxe');
  const ip = $derived(app.info?.ipv4 || '');
</script>
<div class="shell">
<aside class="side" class:open={menu}>
  <a class="brand" href="#/dashboard">
    <svg viewBox="0 0 28 28" width="30" height="30" aria-hidden="true"><rect x="7" y="7" width="14" height="14" rx="2.5" transform="rotate(45 14 14)" fill="none" stroke="var(--acc)" stroke-width="2.6"/><path d="M10.5 17.5 17.5 10.5" stroke="var(--acc2)" stroke-width="2.6" stroke-linecap="round"/></svg>
    <span><b>Costean <em>OS</em></b><i>BITAXE GAMMA 602 · BM1370</i></span></a>
  <div class="eyebrow navh">Your miner</div>
  <nav>{#each NAV as [id, label]}<a href="#/{id}" class:on={app.page === id} onclick={() => menu = false}><span class="ic">{ICON[id]}</span>{label}{#if id === 'block' && app.info?.blockFound > 0}<span class="nb" data-testid="nav-block-count">{app.info.blockFound}</span>{/if}</a>{/each}</nav>
  <div class="sp"></div>
  <a class="rec" href="/recovery">Fallback recovery page ↗</a>
  <div class="conn"><span class="dot {app.online ? 'ok' : 'crit'}"></span><div><b>{app.online ? 'Connected' : 'Offline'}</b><div class="mute small mono">{host}{ip ? ' · ' + ip : ''}</div></div></div>
</aside>
<div class="body">
  <header class="top">
    <button class="burger" onclick={() => menu = !menu} aria-label="Menu">☰</button>
    <div><div class="eyebrow">Costean OS 1.2 · control center</div><div class="ttl">{title}</div></div>
    <span class="sp"></span>
    {#if app.costean?.status}{@const cs = app.costean.status}<a class="pill" href="#/tuning" data-testid="mode-pill"><span class="dot {cs.governor?.state === 'throttled' || cs.governor?.state === 'backed-off' ? 'warn' : 'ok'}"></span><b>{profileLabel(cs.activeProfile).toUpperCase()}</b>{cs.governor?.state === 'throttled' ? ' · THROTTLED' : ''} · {cs.applied?.mhz} MHz / {cs.applied?.mv} mV</a>
    {:else if app.info}<span class="pill" data-testid="mode-pill">{app.info.frequency} MHz / {app.info.coreVoltage} mV</span>{/if}
    {#if app.info}<span class="pill mono hide-s">{app.info.ASICModel || 'BM1370'} · {app.info.boardVersion || '602'}</span>{/if}
  </header>
  <main>
    {#if app.demo}<div class="demo-banner" data-testid="demo">DEMO DATA — mock server, not a real miner</div>{/if}
    {#if mm}<div class="card crit" data-testid="mismatch">⚠ Version mismatch: {mm.text}</div>{/if}
    {#if !app.online && app.lastOk}<div class="card crit">Miner not responding. Last data {Math.round((Date.now() - app.lastOk) / 1000)}s ago.</div>{/if}
    <Page />
    <footer class="mute small">Costean OS web UI {app.wwwVersion?.www || 'dev'} · runs 100% on your miner · based on bitaxeorg ESP-Miner v2.15.4 · GPL-3.0</footer>
  </main>
</div>
</div>
<Confirm />
{#if app.toast}<div class="toast card {app.toast.kind}" role="status">{app.toast.msg}</div>{/if}
<style>
.shell{display:grid;grid-template-columns:250px 1fr;min-height:100vh;align-items:start}
.side{position:sticky;top:0;height:100vh;display:flex;flex-direction:column;gap:6px;padding:18px 14px;background:linear-gradient(180deg,#0f1115cc,#0c0d10cc);border-right:1px solid var(--line);backdrop-filter:blur(8px)}
.brand{display:flex;gap:10px;align-items:center;text-decoration:none;color:var(--text);padding:4px 6px 16px}.brand span{display:grid;line-height:1.1}.brand b{font:800 19px var(--ui);letter-spacing:.02em}.brand em{font-style:normal;color:var(--acc)}.brand i{font:600 10px var(--ui);color:var(--acc2);letter-spacing:.06em;font-style:normal;margin-top:3px}
.navh{padding:6px 10px}
nav{display:grid;gap:3px}nav a{display:flex;gap:10px;align-items:center;padding:9px 12px;border-radius:11px;color:#cfc8bd;text-decoration:none;font-size:13.5px;border:1px solid transparent}
nav a:hover{background:#ffffff08;color:#fff}nav a.on{background:linear-gradient(90deg,#ee8a4c26,#ee8a4c08);border-color:#ee8a4c55;color:#fff;box-shadow:inset 3px 0 0 var(--acc)}
.ic{width:18px;text-align:center;color:var(--acc);font-size:13px}
.rec{font-size:12px;color:var(--mute);padding:8px 12px;text-decoration:none}.rec:hover{color:var(--acc)}
.conn{display:flex;gap:10px;align-items:center;padding:10px 12px;border-top:1px solid var(--line)}
.body{min-width:0}
.top{position:sticky;top:0;z-index:30;display:flex;gap:12px;align-items:center;padding:12px 24px;background:#0a0b0ecc;backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.ttl{font:800 17px var(--ui)}.top a.pill{color:var(--text);text-decoration:none}.nb{margin-left:auto;min-width:18px;padding:0 6px;border-radius:9px;background:var(--gold);color:#3a2500;font:800 11px/18px var(--mono);text-align:center}.burger{display:none}
main{padding:16px 24px 40px;display:grid;gap:14px;align-content:start;max-width:1500px}
footer{text-align:center;padding-top:10px}.toast{position:fixed;bottom:24px;right:24px;z-index:90;padding:10px 16px}.toast.err{border-color:var(--crit)}
@media (max-width:900px){.shell{grid-template-columns:1fr}.side{position:fixed;inset:0 30% 0 0;z-index:80;transform:translateX(-105%);transition:.2s}.side.open{transform:none}.burger{display:block}main{padding:12px}.hide-s{display:none}}
</style>
