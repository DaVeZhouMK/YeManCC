/** Display only: six cells cover -999.9 without leading zeroes. The view reserves
 * nine mono cells, including -999999.9 and the longest finite exponent (-1.8e+308).
 * Large values stay finite and visible; telemetry is never clamped or mutated.
 */
export function formatGyroAxis(value: number): string {
  if (!Number.isFinite(value)) return '—'.padStart(6, ' ');
  const fixed = Math.abs(value) >= 1e21 ? value.toExponential(1) : value.toFixed(1);
  // Rounded negative zero has no useful direction; keep -0.1 and below signed.
  const text = fixed === '-0.0' ? '0.0' : fixed;
  return (text.length <= 9 ? text : value.toExponential(1)).padStart(6, ' ');
}
