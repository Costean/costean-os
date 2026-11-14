<script>
  import { app, save } from '../lib/store.svelte.js';
  let auto = $state(true);
  let speed = $state(70);
  $effect(() => {
    if (app.info) {
      auto = !!app.info.autofanspeed;
      speed = Number(app.info.manualFanSpeed ?? app.info.fanspeed) || 70;
    }
  });
  const i = $derived(app.info || {});
</script>
<div class="grid g2">
  <div class="card">
    <h2>Temperatures</h2>
    <div class="row"><span>ASIC</span><span class="sp"></span><b class="mono">{Number(i.temp||0).toFixed(1)} °C</b></div>
    <div class="row"><span>Voltage regulator</span><span class="sp"></span><b class="mono">{Number(i.vrTemp||0).toFixed(1)} °C</b></div>
    {#if i.overheat_mode}<div class="crit">Overheat mode is on. Fan is forced to 100% and mining limits apply. This firmware does not change upstream thermal limits.</div>{/if}
  </div>
  <div class="card">
    <h2>Fan</h2>
    <label class="chk"><input type="checkbox" bind:checked={auto} /> Automatic fan control</label>
    {#if !auto}
      <label class="f"><span>Manual speed {speed}%</span><input type="range" min="0" max="100" bind:value={speed} /></label>
    {/if}
    <div class="mute small">Current {i.fanspeed ?? '—'}% · {i.fanrpm ?? 0} rpm</div>
    <button class="pri" onclick={() => save({ autofanspeed: auto, manualFanSpeed: Number(speed) })}>Save fan</button>
  </div>
</div>
