<script>
  // Lightweight SVG time-series chart. series: [{name,color,points:[{t,v}],unit}]
  let { series = [], height = 180, unit = '', yMin = null, days = false } = $props();
  let w = $state(600);
  const uid = 'c' + Math.random().toString(36).slice(2, 8);
  const pad = { l: 44, r: 10, t: 10, b: 22 };
  let hover = $state(null);
  let all = $derived(series.flatMap(s => s.points.filter(p => p.v != null && !isNaN(p.v))));
  let tMin = $derived(all.length ? Math.min(...all.map(p => p.t)) : 0);
  let tMax = $derived(all.length ? Math.max(...all.map(p => p.t)) : 1);
  let vMin = $derived(yMin ?? (all.length ? Math.min(...all.map(p => p.v)) * 0.97 : 0));
  let vMax = $derived(all.length ? Math.max(...all.map(p => p.v)) * 1.03 || 1 : 1);
  const x = t => pad.l + (t - tMin) / ((tMax - tMin) || 1) * (w - pad.l - pad.r);
  const y = v => pad.t + (1 - (v - vMin) / ((vMax - vMin) || 1)) * (height - pad.t - pad.b);
  const path = pts => { let d = '', pen = false; for (const p of pts) { if (p.v == null || isNaN(p.v)) { pen = false; continue; } d += (pen ? 'L' : 'M') + x(p.t).toFixed(1) + ',' + y(p.v).toFixed(1); pen = true; } return d; };
  const area = pts => { const f = pts.filter(p => p.v != null && !isNaN(p.v)); if (!f.length) return ''; return 'M' + f.map(p => x(p.t).toFixed(1) + ',' + y(p.v).toFixed(1)).join('L') + `L${x(f[f.length - 1].t)},${height - pad.b}L${x(f[0].t)},${height - pad.b}Z`; };
  const ticks = $derived(Array.from({ length: 4 }, (_, i) => vMin + (vMax - vMin) * i / 3));
  const tt = t => days ? new Date(t).toLocaleString([], { weekday: 'short', hour: '2-digit', minute: '2-digit' }) : new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  function move(e) { const r = e.currentTarget.getBoundingClientRect(); const px = e.clientX - r.left; const t = tMin + (px - pad.l) / (w - pad.l - pad.r) * (tMax - tMin);
    hover = { px, vals: series.map(s => { let best = null; for (const p of s.points) if (!best || Math.abs(p.t - t) < Math.abs(best.t - t)) best = p; return { name: s.name, color: s.color, v: best?.v, t: best?.t }; }) }; }
</script>
<div class="chart" bind:clientWidth={w}>
  {#if !all.length}<div class="empty mute" style:height="{height}px">Collecting data…</div>{:else}
  <svg width={w} {height} onpointermove={move} onpointerleave={() => hover = null} role="img" aria-label="chart">
    <defs>{#each series as s, i}<linearGradient id="{uid}g{i}" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color={s.color} stop-opacity=".35"/><stop offset="1" stop-color={s.color} stop-opacity="0"/></linearGradient>{/each}</defs>
    {#each ticks as v}<line x1={pad.l} x2={w - pad.r} y1={y(v)} y2={y(v)} class="gl"/><text x={pad.l - 6} y={y(v) + 4} text-anchor="end" class="ax">{v.toFixed(v < 10 ? 1 : 0)}</text>{/each}
    <text x={pad.l} y={height - 6} class="ax">{tt(tMin)}</text><text x={w - pad.r} y={height - 6} text-anchor="end" class="ax">{tt(tMax)}</text>
    {#each series as s, i}{#if i === 0}<path d={area(s.points)} fill="url(#{uid}g{i})"/>{/if}<path d={path(s.points)} fill="none" stroke={s.color} stroke-width="2" stroke-linejoin="round"/>{/each}
    {#if hover}<line x1={hover.px} x2={hover.px} y1={pad.t} y2={height - pad.b} class="hl"/>{/if}
  </svg>
  {#if hover}<div class="tip card" style:left="{Math.min(hover.px + 12, w - 170)}px">{#if hover.vals[0]?.t}<div class="mute small">{new Date(hover.vals[0].t).toLocaleTimeString()}</div>{/if}{#each hover.vals as h}<div><span style:color={h.color}>●</span> {h.name}: <b>{h.v?.toFixed(h.v < 10 ? 2 : 1) ?? '—'}</b> {unit}</div>{/each}</div>{/if}
  {/if}
  <div class="legend">{#each series as s}<span><i style:background={s.color}></i>{s.name}</span>{/each}</div>
</div>
<style>.chart{position:relative;width:100%}.gl{stroke:var(--line)}.hl{stroke:var(--mute);stroke-dasharray:3 3}.ax{fill:var(--mute);font-size:10px}
.tip{position:absolute;top:8px;padding:8px 10px;font-size:12px;pointer-events:none;min-width:150px}.empty{display:grid;place-items:center}
.legend{display:flex;gap:12px;font-size:12px;color:var(--mute);margin-top:4px}.legend i{display:inline-block;width:10px;height:3px;border-radius:2px;margin-right:5px;vertical-align:middle}</style>
