// Deep-diff an edited copy against the original, returning only changed leaf fields (arrays kept index-aligned).
export function changes(orig, edit) {
  if (Array.isArray(edit)) { const out = edit.map((e, i) => changes(orig?.[i], e) ?? {}); return out.some(o => Object.keys(o).length) ? out : undefined; }
  if (edit && typeof edit === 'object') { const out = {}; for (const k of Object.keys(edit)) { const c = changes(orig?.[k], edit[k]); if (c !== undefined) out[k] = c; } return Object.keys(out).length ? out : undefined; }
  return edit === orig || (edit === '' && orig == null) ? undefined : edit;
}
export const clone = o => JSON.parse(JSON.stringify(o ?? {}));
