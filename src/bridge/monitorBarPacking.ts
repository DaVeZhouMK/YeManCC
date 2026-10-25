// At most seven group widths. Pure bounded packing, no value acquisition.
export function packedBarWidth(widths: readonly number[], available: number, gap: number, padding: number): number {
  if (![available, gap, padding, ...widths].every(Number.isFinite) || available <= 0) return 0;
  const inner = Math.max(0, available - Math.max(0, padding));
  let row = 0, longest = 0;
  for (const raw of widths.slice(0, 7)) {
    const width = Math.min(inner, Math.max(0, raw));
    if (row && row + Math.max(0, gap) + width > inner + .1) { longest = Math.max(longest, row); row = width; }
    else row += (row ? Math.max(0, gap) : 0) + width;
  }
  return Math.min(available, Math.max(0, padding) + Math.max(longest, row));
}
