export function mismatch(fw, www) {
  if (!fw || !www) return null;
  return fw.trim() === www.trim() ? null : { fw, www, text: `Firmware is ${fw} but the web UI is ${www}. Flash the matching app .bin and www.bin as a pair.` };
}
