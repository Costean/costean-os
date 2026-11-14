<script>
  import { confirmState as c, answer } from '../lib/confirm.svelte.js';
  let typed = $state(''), totp = $state('');
  $effect(() => { if (c.open) { typed = ''; totp = ''; } });
  let ready = $derived((!c.phrase || typed.trim() === c.phrase) && (!c.otp || /^\d{6}$/.test(totp)));
</script>
{#if c.open}
<div class="modal" role="dialog" aria-modal="true" aria-labelledby="cf-t" data-testid="confirm">
  <div class="card box">
    <div class="mute small">CONFIRM CHANGE</div>
    <h3 id="cf-t">{c.title}</h3>
    {#if c.detail}<pre class="mono">{c.detail}</pre>{/if}
    {#if c.phrase}
      <div class="warnbox">{#if c.warn}{c.warn}{:else}<b>Above stock.</b> Higher frequency or voltage raises heat and power draw and can shorten chip life.{/if} Type <b class="mono">{c.phrase}</b> to continue.</div>
      <input data-testid="phrase" placeholder={c.phrase} bind:value={typed} autocomplete="off" />
    {/if}
    {#if c.otp}<label class="f"><span>Authenticator code (OTP is enabled)</span><input inputmode="numeric" maxlength="6" bind:value={totp} /></label>{/if}
    <div class="row" style="margin-top:12px"><span class="sp"></span>
      <button onclick={() => answer(false)}>Cancel</button>
      <button data-testid="confirm-ok" class={c.danger ? 'danger' : 'pri'} disabled={!ready} onclick={() => answer(true, totp)}>Confirm</button></div>
  </div>
</div>{/if}
<style>.modal{position:fixed;inset:0;background:#0009;backdrop-filter:blur(4px);display:grid;place-items:center;z-index:80;padding:16px}.box{max-width:520px;width:100%;display:grid;gap:10px}
pre{background:var(--bg2);border:1px solid var(--line);border-radius:10px;padding:10px;margin:0;white-space:pre-wrap;word-break:break-all;max-height:40vh;overflow:auto;font-size:12px}
.warnbox{border:1px solid var(--crit);background:color-mix(in srgb,var(--crit) 12%,transparent);border-radius:10px;padding:10px;font-size:13px}</style>
