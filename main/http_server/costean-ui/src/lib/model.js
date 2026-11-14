export function modeOf(info) {
  if (!info) return null;
  if (info.overheat_mode) return { level: 'crit', text: 'Overheat' };
  if (info.miningPaused) return { level: 'warn', text: 'Paused' };
  if (info.pools_unavailable) return { level: 'crit', text: 'No pool' };
  if (info.poolSplitActive) return { level: 'ok', text: 'Split mining' };
  if (info.isUsingFallbackStratum) return { level: 'warn', text: 'Fallback pool' };
  return { level: 'ok', text: 'Mining' };
}

export const STOCK_FREQ = 525;
export const STOCK_VOLT = 1150;
export const FREQ_PRESETS = [400, 490, 525, 550, 600, 625, 690];
export const VOLT_PRESETS = [1000, 1060, 1100, 1150, 1200, 1250];
