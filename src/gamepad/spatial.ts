/**
 * Declarative spatial-navigation contract for feature pages.
 *
 * A page marks every controller focus target with numeric `data-gp-row` and
 * `data-gp-col` attributes. The shared gamepad engine then owns all movement:
 * left/right stay in the current row, up/down move to the adjacent row, and
 * both axes stop at the edge instead of wrapping around.
 *
 * A focused graph point may additionally declare `data-gp-inline-edit`. The
 * shared engine emits a cancellable `gp:spatial-edit` event first; a feature
 * can consume it to change the selected point without starting a second input
 * loop. If it is not consumed, normal row/column focus movement applies.
 *
 * This deliberately contains no input polling and no feature-specific state.
 * Fan UI (and future pages with the same geometry) only supplies DOM semantics.
 */

export interface SpatialPosition {
  row: number;
  col: number;
}

export type SpatialDirection = { dx: -1 | 0 | 1; dy: -1 | 0 | 1 };

function parseCoordinate(value: string | undefined): number | null {
  if (value == null || value.trim() === '') return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

export function getSpatialPosition(el: HTMLElement | null): SpatialPosition | null {
  if (!el) return null;
  const row = parseCoordinate(el.dataset.gpRow);
  const col = parseCoordinate(el.dataset.gpCol);
  return row == null || col == null ? null : { row, col };
}

/**
 * Return the target for an explicit spatial row/column layout.
 *
 * `undefined` means the current element is not part of the contract and the
 * caller may fall back to the application's geometry heuristic. `null` means
 * it is part of the contract but the requested direction is at a hard edge.
 */
export function spatialNavigationTarget(
  elements: readonly HTMLElement[],
  current: HTMLElement,
  direction: SpatialDirection,
): HTMLElement | null | undefined {
  const currentPosition = getSpatialPosition(current);
  if (!currentPosition) return undefined;

  const candidates = elements
    .map((el) => ({ el, position: getSpatialPosition(el) }))
    .filter((item): item is { el: HTMLElement; position: SpatialPosition } => !!item.position);

  if (direction.dx !== 0 && direction.dy === 0) {
    const sameRow = candidates
      .filter(({ el, position }) => el !== current && position.row === currentPosition.row)
      .sort((a, b) => a.position.col - b.position.col);
    const horizontal = direction.dx > 0
      ? sameRow.find((item) => item.position.col > currentPosition.col)
      : [...sameRow].reverse().find((item) => item.position.col < currentPosition.col);
    return horizontal?.el ?? null;
  }

  if (direction.dy !== 0 && direction.dx === 0) {
    const rows = [...new Set(candidates.map(({ position }) => position.row))].sort((a, b) => a - b);
    const adjacentRow = direction.dy > 0
      ? rows.find((row) => row > currentPosition.row)
      : [...rows].reverse().find((row) => row < currentPosition.row);
    if (adjacentRow == null) return null;

    const rowCandidates = candidates
      .filter(({ position }) => position.row === adjacentRow)
      .sort((a, b) => {
        const distance = Math.abs(a.position.col - currentPosition.col) -
          Math.abs(b.position.col - currentPosition.col);
        return distance || a.position.col - b.position.col;
      });
    return rowCandidates[0]?.el ?? null;
  }

  // The shared engine dispatches one axis at a time. Treat a diagonal request
  // as an explicit no-op rather than accidentally crossing a row and column.
  return null;
}
