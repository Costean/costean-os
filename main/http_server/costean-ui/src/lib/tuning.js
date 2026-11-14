// Costean OS Manual tuning range for the Bitaxe Gamma 602 (mirrors firmware Board602::kManual*).
export const MANUAL = { minMhz: 400, maxMhz: 1000, minMv: 1000, maxMv: 1300 };
export const FREQ_CHOICES = [425, 490, 525, 575, 600, 625, 650, 690, 750, 800, 850, 900, 950, 1000];
export const VOLT_CHOICES = [1100, 1150, 1180, 1200, 1225, 1250, 1275, 1300];
export const HIGH_MHZ = 700, HIGH_MV = 1200;
export const PSU_PRESETS = [25, 30, 40, 50, 60];
export const PSU_RANGE = [10, 150];
export const powerForPsu = w => Math.max(10, Math.floor(w * 9 / 10));

/** Returns an error string or null. */
export function checkManual(mhz, mv, r = MANUAL) {
  const f = Number(mhz), v = Number(mv);
  if (!Number.isFinite(f) || f < r.minMhz || f > r.maxMhz) return `Frequency must be ${r.minMhz}-${r.maxMhz} MHz.`;
  if (!Number.isInteger(v) || v < r.minMv || v > r.maxMv) return `Core voltage must be ${r.minMv}-${r.maxMv} mV (whole numbers).`;
  return null;
}
export const isHigh = (mhz, mv) => Number(mhz) > HIGH_MHZ || Number(mv) > HIGH_MV;
/** Rough input power for one BM1370 (scales with clock x voltage^2 from the 602's ~17 W at 525 MHz / 1150 mV). */
export const estWatts = (mhz, mv) => Math.round(17 * (Number(mhz) / 525) * Math.pow(Number(mv) / 1150, 2) * 10) / 10;
export const estThs = mhz => Math.round(Number(mhz) * 2040 / 1000 / 10) / 100;
