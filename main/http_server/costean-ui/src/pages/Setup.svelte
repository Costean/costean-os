<script>
  // Costean OS first-run Wi-Fi setup. Shown automatically while the setup hotspot is up (fresh flash, or the saved
  // network can't be joined), and at #/wifi from System & Network. Scans on open, tap a network, type the password.
    import { app } from '../lib/store.svelte.js';
  import { api } from '../lib/api.js';
  import { cleanNetworks, bars, signalLabel, securityLabel, isOpen, passwordProblem, ssidProblem, joinState, resetNotice } from '../lib/wifi.js';

  let { onskip = () => {} } = $props();
  let networks = $state([]);
  let scanning = $state(false);
  let scanErr = $state('');
  let scannedOnce = $state(false);
  let step = $state('list');          // list | password | manual | saving | saved | error
  let chosen = $state(null);          // { ssid, rssi, authmode, manual }
  let pass = $state('');
  let manualSsid = $state('');
  let manualOpen = $state(false);
  let show = $state(false);
  let touched = $state(false);
  let saveErr = $state('');
  let passEl = $state();

  const i = $derived(app.info || {});
  const apMode = $derived(!!i.apEnabled);
  // costean.6: only a real failure shows "Couldn't join" (it used to show while the miner was still joining, which
  // invited typing the password again and restarting the miner in a loop).
  const js = $derived(joinState(i));
  const failed = $derived(apMode && js.kind === 'failed' ? js : null);
  const busy = $derived(apMode && (js.kind === 'joining' || js.kind === 'dhcp'));
  const rn = $derived(resetNotice(i));
  let leaving = $state(false);
  async function restartNormal() { leaving = true; try { await api.restartNormal(); } catch { } }
  const pProblem = $derived(chosen ? passwordProblem(pass, chosen.authmode) : '');
  const sProblem = $derived(step === 'manual' ? ssidProblem(manualSsid) : '');
  const host = $derived(i.hostname && i.hostname !== 'Unknown' ? i.hostname : 'bitaxe');

  async function scan(refresh = false, tries = 3) {
    if (scanning) return;
    scanning = true; scanErr = '';
    try {
      for (let t = 0; t < tries; t++) {
        try {
          const r = await api.wifiScan(refresh || t > 0);
          networks = cleanNetworks(r?.networks);
          if (networks.length) break;
        } catch (e) {
          if (t === tries - 1) throw e;
        }
        await new Promise(res => setTimeout(res, 1500));
      }
      if (!networks.length) scanErr = 'No networks found yet. Move the miner closer to your router, then tap Rescan.';
    } catch (e) {
      scanErr = 'Scan did not finish. Tap Rescan to try again.';
    } finally { scanning = false; scannedOnce = true; }
  }

  function pick(n) {
    chosen = { ...n, manual: false }; pass = ''; show = false; touched = false; saveErr = '';
    step = 'password';
    setTimeout(() => passEl?.focus(), 50);
  }
  function manual() { chosen = null; manualSsid = ''; manualOpen = false; pass = ''; touched = false; saveErr = ''; step = 'manual'; }
  function back() { step = 'list'; chosen = null; pass = ''; saveErr = ''; }

  async function connect() {
    touched = true;
    if (step === 'manual') {
      if (sProblem) return;
      chosen = { ssid: manualSsid, rssi: null, authmode: manualOpen ? 0 : 3, manual: true };
    }
    if (pProblem) return;
    const body = { ssid: chosen.ssid, wifiPass: isOpen(chosen.authmode) ? '' : pass };
    step = 'saving'; saveErr = '';
    try {
      await api.patch(body);
    } catch (e) {
      saveErr = e.message || 'The miner did not accept the settings.'; step = chosen.manual ? 'manual' : 'password'; return;
    }
    // The miner reboots and drops the hotspot, so a failed reply here is expected.
    try { await api.restart(); } catch { }
    step = 'saved';
  }

  // Do not scan while the miner is joining or getting its IP (the miner refuses anyway: a scan there breaks DHCP).
  let autoScanned = false;
  $effect(() => { if (!autoScanned && app.info && !busy) { autoScanned = true; scan(false); } });
</script>

<div class="setup" data-testid="setup">
  <header>
    <svg viewBox="0 0 28 28" width="34" height="34" aria-hidden="true"><rect x="7" y="7" width="14" height="14" rx="2.5" transform="rotate(45 14 14)" fill="none" stroke="var(--acc)" stroke-width="2.6"/><path d="M10.5 17.5 17.5 10.5" stroke="var(--acc2)" stroke-width="2.6" stroke-linecap="round"/></svg>
    <div><b class="brand">Costean <em>OS</em></b><div class="sub">BITAXE GAMMA 602 · SETUP</div></div>
  </header>

  {#if app.demo}<div class="demo-banner" data-testid="demo">DEMO DATA — mock server, not a real miner</div>{/if}
  <ol class="steps" aria-label="Setup steps">
    <li class:on={step === 'list'} class:done={step !== 'list'}><span>1</span>Network</li>
    <li class:on={step === 'password' || step === 'manual'} class:done={step === 'saving' || step === 'saved'}><span>2</span>Password</li>
    <li class:on={step === 'saving' || step === 'saved'}><span>3</span>Connect</li>
  </ol>

  {#if rn && step !== 'saved'}
    <div class="card note {rn.safe ? 'critc' : 'warnc'}" data-testid="reset-notice">
      {#if rn.safe}<b>Safe mode: mining is paused.</b> <span>The miner restarted unexpectedly {rn.count > 1 ? `${rn.count} times in a row` : ''}. Last: {rn.text}.</span>
        <div class="mute small">Wi-Fi and this page still work. When the cause is fixed, restart normally.</div>
        <button class="pri" onclick={restartNormal} disabled={leaving} data-testid="leave-safe">{leaving ? 'Restarting…' : 'Restart normally'}</button>
      {:else}<b>The miner restarted unexpectedly.</b> <span>{rn.text}.</span>{#if rn.wifi}<div class="mute small">Wi-Fi status at the time: {rn.wifi}</div>{/if}{/if}
    </div>
  {/if}

  {#if failed && step === 'list'}
    <div class="card note warnc" data-testid="join-failed"><b>Couldn't join “{failed.ssid}”: {failed.text}</b>
      {#if failed.hint}<span>{failed.hint}</span>{/if}
      <div class="mute small mono" data-testid="join-failed-code">{failed.reason ? `Error ${failed.reason}` : failed.status}{failed.retries ? ` · ${failed.retries} tries` : ''} · still retrying in the background</div>
      <div class="mute small">{failed.code === 'password' ? 'Pick your network again and re-enter the password.' : 'Check the network, or pick it again below.'}</div></div>
  {:else if busy && step === 'list'}
    <div class="card note okc" data-testid="joining"><b>{js.kind === 'dhcp' ? `Joined “${js.ssid}”, getting an IP address…` : `Joining “${js.ssid}”…`}</b>
      <span class="mute small">No need to type the password again. This normally takes under a minute; the hotspot closes by itself when it works.</span></div>
  {/if}

  {#if step === 'list'}
    <section class="card">
      <div class="head">
        <div><h1>Choose your Wi-Fi</h1><div class="mute small">{scanning ? 'Looking for networks near the miner…' : networks.length ? `${networks.length} network${networks.length === 1 ? '' : 's'} found · tap yours` : scannedOnce ? 'Nothing found yet' : ''}</div></div>
        <button class="icon" onclick={() => scan(true, 2)} disabled={scanning || busy} title={busy ? 'Scanning waits until the miner has finished joining' : ''} aria-label="Rescan" data-testid="rescan"><span class:spin={scanning}>↻</span> Rescan</button>
      </div>
      {#if scanning && !networks.length}
        <div class="radar" aria-hidden="true"><i></i></div>
        {#each [0, 1, 2, 3] as k}<div class="ghost" style="opacity:{1 - k * 0.2}"></div>{/each}
      {:else}
        <ul class="nets" data-testid="networks">
          {#each networks as n (n.ssid)}
            <li><button class="net" onclick={() => pick(n)} data-testid="net">
              <svg class="sig" viewBox="0 0 20 16" width="22" height="18" aria-label={signalLabel(n.rssi)}>{#each [0, 1, 2, 3] as b}<rect x={1 + b * 5} y={12 - b * 3.6} width="3.4" height={3.6 + b * 3.6} rx="1" fill={b < Math.max(1, bars(n.rssi)) ? 'var(--acc2)' : '#3a3f49'} />{/each}</svg>
              <span class="nm"><b>{n.ssid}</b><small>{securityLabel(n.authmode)} · {signalLabel(n.rssi)} · {n.rssi} dBm</small></span>
              {#if !isOpen(n.authmode)}<svg class="lock" viewBox="0 0 16 16" width="15" height="15" aria-label="Password protected"><rect x="3" y="7" width="10" height="7.5" rx="1.6" fill="currentColor"/><path d="M5.2 7V5.2a2.8 2.8 0 0 1 5.6 0V7" fill="none" stroke="currentColor" stroke-width="1.6"/></svg>{:else}<span class="tag">open</span>{/if}
              <span class="chev">›</span>
            </button></li>
          {/each}
        </ul>
        {#if scanErr}<div class="mute small err" data-testid="scan-err">{scanErr}</div>{/if}
      {/if}
      <button class="ghostbtn" onclick={manual} data-testid="manual">Network not listed? Enter its name</button>
    </section>
  {:else if step === 'password' || step === 'manual'}
    <section class="card">
      {#if step === 'password'}
        <div class="chosen" data-testid="chosen">
          <svg class="sig" viewBox="0 0 20 16" width="22" height="18" aria-hidden="true">{#each [0, 1, 2, 3] as b}<rect x={1 + b * 5} y={12 - b * 3.6} width="3.4" height={3.6 + b * 3.6} rx="1" fill={b < Math.max(1, bars(chosen.rssi)) ? 'var(--acc2)' : '#3a3f49'} />{/each}</svg>
          <span class="nm"><b>{chosen.ssid}</b><small>{securityLabel(chosen.authmode)} · {signalLabel(chosen.rssi)}</small></span>
          <button class="link" onclick={back}>Change</button>
        </div>
      {:else}
        <h1>Enter your network</h1>
        <div class="mute small">For hidden networks. The name is case-sensitive.</div>
        <label class="f"><span>Network name (SSID)</span><input data-testid="manual-ssid" bind:value={manualSsid} autocapitalize="none" autocorrect="off" spellcheck="false" maxlength="32" placeholder="My home Wi-Fi" /></label>
        {#if touched && sProblem}<div class="err small">{sProblem}</div>{/if}
        <label class="chk"><input type="checkbox" bind:checked={manualOpen} /> Open network (no password)</label>
      {/if}
      {#if !(chosen && isOpen(chosen.authmode)) && !(step === 'manual' && manualOpen)}
        <label class="f"><span>Wi-Fi password</span>
          <div class="pw"><input data-testid="password" bind:this={passEl} bind:value={pass} type={show ? 'text' : 'password'} autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" maxlength="64" placeholder="Password" onkeydown={(e) => e.key === 'Enter' && connect()} />
          <button class="eye" type="button" onclick={() => show = !show} aria-label={show ? 'Hide password' : 'Show password'}>{show ? 'Hide' : 'Show'}</button></div></label>
        {#if touched && pProblem}<div class="err small" data-testid="pw-err">{pProblem}</div>{/if}
      {:else}
        <div class="mute small">This network is open. No password needed.</div>
      {/if}
      {#if saveErr}<div class="err small">{saveErr}</div>{/if}
      <button class="pri big" onclick={connect} data-testid="connect">Save &amp; connect</button>
      {#if step === 'manual'}<button class="link center" onclick={back}>← Back to the list</button>{/if}
      <div class="mute small">The miner saves this, restarts and joins your Wi-Fi. Your phone will drop off the setup hotspot.</div>
    </section>
  {:else if step === 'saving'}
    <section class="card mid" data-testid="saving"><div class="spinner"></div><h1>Saving…</h1><div class="mute small">Storing “{chosen?.ssid}” on the miner.</div></section>
  {:else if step === 'saved'}
    <section class="card mid" data-testid="saved">
      <div class="ok-badge" aria-hidden="true">✓</div>
      <h1>Saved. Joining “{chosen?.ssid}”</h1>
      <div class="mute">The miner is restarting now. This page will stop responding, that is normal.</div>
      <ol class="next">
        <li>Put your phone back on <b>{chosen?.ssid}</b>.</li>
        <li>Open the IP address shown on the miner's screen, or try <b class="mono">http://{host}.local</b>.</li>
        <li>If the password was wrong, the hotspot <b class="mono">{i.apSsid || 'Bitaxe_XXXX'}</b> comes back within about a minute. Join it and try again.</li>
      </ol>
    </section>
  {/if}

  {#if step !== 'saving' && step !== 'saved'}<button class="link center skip" onclick={onskip} data-testid="skip">{apMode ? 'Skip for now and open the dashboard' : '← Back to the dashboard'}</button>{/if}
  <div class="foot mute small">Costean OS {i.version || ''}{i.apSsid && apMode ? ` · hotspot ${i.apSsid}` : ''}</div>
</div>

<style>
.setup{max-width:480px;margin:0 auto;padding:18px 14px 28px;display:grid;gap:12px;min-height:100vh;align-content:start}
header{display:flex;gap:12px;align-items:center;padding:6px 4px}
.brand{font:800 22px var(--ui);letter-spacing:.02em}.brand em{font-style:normal;color:var(--acc)}.sub{font:700 10px var(--ui);letter-spacing:.2em;color:var(--acc2);margin-top:2px}
.steps{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.steps li{display:flex;gap:7px;align-items:center;font:700 11px var(--ui);letter-spacing:.08em;text-transform:uppercase;color:var(--mute);padding:7px 9px;border-radius:10px;border:1px solid var(--line);background:#111317}
.steps li span{display:grid;place-items:center;width:18px;height:18px;border-radius:50%;background:#22262e;color:var(--mute);font:800 10px var(--mono)}
.steps li.on{color:var(--text);border-color:#ee8a4c77;background:linear-gradient(90deg,#ee8a4c22,#ee8a4c08)}.steps li.on span{background:var(--acc);color:#1a0f08}
.steps li.done span{background:var(--acc2);color:#04150e}
section.card{display:grid;gap:12px;padding:16px}
h1{font-size:20px}
.head{display:flex;gap:10px;align-items:flex-start;justify-content:space-between}
.icon{white-space:nowrap;padding:8px 12px;border-radius:10px}.spin{display:inline-block;animation:sp 1s linear infinite}@keyframes sp{to{transform:rotate(360deg)}}
.nets{list-style:none;margin:0;padding:0;display:grid;gap:6px;max-height:56vh;overflow:auto}
.net,.chosen{width:100%;display:flex;gap:12px;align-items:center;text-align:left;padding:13px 12px;border-radius:12px;background:#0f1115;border:1px solid var(--line);min-height:56px}
.net:hover,.net:focus-visible{border-color:var(--acc);color:var(--text);background:#ee8a4c10}
.chosen{border-color:#3fd8c266;background:#3fd8c20d}
.nm{display:grid;flex:1;min-width:0}.nm b{font-size:15px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.nm small{color:var(--mute);font-size:11.5px}
.lock{color:var(--acc);flex:none}.chev{color:var(--mute);font-size:22px;line-height:1}.sig{flex:none}
.ghost{height:56px;border-radius:12px;background:linear-gradient(90deg,#14171c,#1b1f26,#14171c);background-size:200% 100%;animation:gh 1.3s infinite linear;border:1px solid var(--line)}@keyframes gh{to{background-position:-200% 0}}
.radar{height:4px;border-radius:2px;background:#1b1f26;overflow:hidden}.radar i{display:block;height:100%;width:35%;background:linear-gradient(90deg,transparent,var(--acc2),transparent);animation:rd 1.2s infinite ease-in-out}@keyframes rd{from{transform:translateX(-100%)}to{transform:translateX(300%)}}
.ghostbtn{background:transparent;border:1px dashed var(--line2);padding:12px;border-radius:12px;color:var(--mute)}
.pw{display:flex;gap:6px}.pw input{font-size:16px;padding:12px}.eye{flex:none;padding:0 14px}
input{font-size:16px;padding:11px 12px}
.big{padding:14px;font-size:15px;border-radius:12px}
.link{background:none;border:0;color:var(--acc);padding:6px;font-weight:600}.center{justify-self:center}
.skip{color:var(--mute);font-weight:500}
.err{color:var(--crit)}
.note{display:grid;gap:4px}.warnc{border-color:color-mix(in srgb,var(--warn) 55%,transparent);--tab:var(--warn)}
.critc{border-color:color-mix(in srgb,var(--crit) 60%,transparent);--tab:var(--crit)}.okc{border-color:color-mix(in srgb,var(--acc2) 50%,transparent);--tab:var(--acc2)}
.note .pri{justify-self:start;margin-top:4px}
.mid{justify-items:center;text-align:center;padding:26px 18px}
.spinner{width:44px;height:44px;border-radius:50%;border:3px solid #2a2e36;border-top-color:var(--acc);animation:sp .9s linear infinite}
.ok-badge{width:56px;height:56px;border-radius:50%;display:grid;place-items:center;font:800 28px var(--ui);color:#04150e;background:linear-gradient(180deg,#5fe7d3,var(--acc2));box-shadow:0 0 24px #3fd8c255}
.next{text-align:left;margin:4px 0 0;padding-left:20px;display:grid;gap:8px;font-size:14px}
.foot{text-align:center}
</style>
