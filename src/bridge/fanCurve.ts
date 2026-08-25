import type { FanNode } from './fanApi';

export type FanCurveField = 'tempC' | 'dutyPercent';

/**
 * Normalize one user edit without silently moving an earlier point back over
 * the edit. Node 1's temperature is the only fixed value (0°C); its duty is
 * intentionally editable. When an earlier duty is raised, only later duties
 * are propagated upward to preserve the HC monotonicity contract.
 */
export function normalizeFanNodes(
  source: readonly FanNode[],
  index: number,
  field: FanCurveField,
  rawValue: number,
): FanNode[] {
  const next = source.map((node) => ({
    tempC: Number(node.tempC),
    dutyPercent: Number(node.dutyPercent),
  }));
  if (next.length !== 4 || index < 0 || index >= next.length || !Number.isFinite(rawValue)) return next;

  if (field === 'tempC') {
    if (index === 0) {
      next[0].tempC = 0;
      return next;
    }
    const lower = next[index - 1].tempC;
    const upper = index < next.length - 1 ? next[index + 1].tempC : 100;
    next[index].tempC = Math.max(lower, Math.min(upper, rawValue));
    return next;
  }

  next[index].dutyPercent = Math.max(0, Math.min(100, rawValue));
  if (index === next.length - 1) {
    // Node 4 has a global 50% floor and may never fall below node 3.
    next[index].dutyPercent = Math.max(next[index - 1].dutyPercent, 50, next[index].dutyPercent);
    return next;
  }

  // A node cannot dip below its predecessor. If it rises above a successor,
  // raise the successor chain instead of snapping the edited node back to 0.
  if (index > 0) {
    next[index].dutyPercent = Math.max(next[index - 1].dutyPercent, next[index].dutyPercent);
  }
  for (let i = index + 1; i < next.length; i += 1) {
    next[i].dutyPercent = Math.max(next[i].dutyPercent, next[i - 1].dutyPercent);
  }
  next[3].dutyPercent = Math.max(50, next[3].dutyPercent);
  return next;
}

export function validateFanNodes(nodes: readonly FanNode[]): boolean {
  if (nodes.length !== 4 || nodes[0].tempC !== 0) return false;
  return nodes.every((node, index) => {
    if (![node.tempC, node.dutyPercent].every(Number.isFinite)) return false;
    if (node.tempC < 0 || node.tempC > 100 || node.dutyPercent < 0 || node.dutyPercent > 100) return false;
    if (index === 0) return true;
    return node.tempC >= nodes[index - 1].tempC && node.dutyPercent >= nodes[index - 1].dutyPercent;
  }) && nodes[3].dutyPercent >= 50;
}
