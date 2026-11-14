// App-wide confirm. Every write goes through here. Above-stock requires typing the phrase.
export const confirmState = $state({ open: false, title: '', detail: '', danger: false, phrase: '', warn: '', otp: false, resolve: null });
export function ask(title, { detail = '', danger = false, phrase = '', otp = false, warn = '' } = {}) {
  return new Promise(resolve => Object.assign(confirmState, { open: true, title, detail, danger, phrase, warn, otp, resolve }));
}
/** resolves {ok:boolean, totp?:string} */
export function answer(ok, totp) { confirmState.open = false; confirmState.resolve?.({ ok, totp }); }
