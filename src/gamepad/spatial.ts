/**
 * Declarative spatial-navigation contract for feature pages.
 *
 * A page marks every controller focus target with numeric `data-gp-row` and
 * `data-gp-col` attributes. The shared gamepad engine then owns all movement:
 * left/right stay in the current row, up/down move to the adjacent row, and
 * both axes stop at the edge instead of wrapping around.
 *
 * Responsive pages can opt into data-gp-spatial="nearest" on their scope.
 * Directions then follow actual control rectangles, including reflowed layouts.
 * Rectangle reads occur only on navigation, never on a timer or data refresh.
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

/** Layout coordinates inside a focus scope, independent of a pane's scroll position.
 * Within one scroller this is a shared translation. Across split panes it keeps
 * the fixed preview/header from competing with settings scrolled out of view.
 */
export function spatialNavigationRect(el: HTMLElement): Pick<DOMRect, 'left'|'right'|'top'|'bottom'|'width'|'height'> {
  const r = el.getBoundingClientRect(), scope = el.closest('[data-gp-scope]');
  let x = 0, y = 0;
  for (let parent = el.parentElement; parent && parent !== scope; parent = parent.parentElement) {
    if (!parent.scrollTop && !parent.scrollLeft) continue;
    const style = getComputedStyle(parent), box = parent.getBoundingClientRect();
    if (/auto|scroll/.test(style.overflowY) && parent.offsetHeight > 0) y += parent.scrollTop * box.height / parent.offsetHeight;
    if (/auto|scroll/.test(style.overflowX) && parent.offsetWidth > 0) x += parent.scrollLeft * box.width / parent.offsetWidth;
  }
  return {left:r.left+x, right:r.right+x, top:r.top+y, bottom:r.bottom+y, width:r.width, height:r.height};
}

/** Scoped nearest-direction resolver. No wrap, hidden controls or cross-page jumps. */
export function nearestSpatialTarget(
  elements: readonly HTMLElement[], current: HTMLElement, direction: SpatialDirection,
): HTMLElement | null {
  if ((direction.dx === 0) === (direction.dy === 0)) return null;
  const horizontal = direction.dx !== 0, sign = horizontal ? direction.dx : direction.dy;
  const scope = current.closest('[data-gp-scope]');
  const b = spatialNavigationRect(current);
  const pane = current.closest<HTMLElement>('[data-gp-pane]');
  const paneId = pane?.dataset.gpPane;
  const bx = b.left + b.width / 2, by = b.top + b.height / 2;
  const candidates = elements.filter(el => el !== current &&
    el.closest('[data-gp-scope]') === scope && !el.closest('[hidden],[inert],[aria-hidden="true"]') &&
    !el.matches(':disabled,[aria-disabled="true"],[data-gp-ignore],[tabindex="-1"]'))
    .map(el => {
      const r = spatialNavigationRect(el), style = getComputedStyle(el);
      const x = r.left + r.width / 2, y = r.top + r.height / 2;
      const forward = ((horizontal ? x - bx : y - by) * sign);
      const overlap = horizontal ? Math.min(b.bottom, r.bottom) - Math.max(b.top, r.top)
        : Math.min(b.right, r.right) - Math.max(b.left, r.left);
      const gap = horizontal ? sign > 0 ? r.left - b.right : b.left - r.right
        : sign > 0 ? r.top - b.bottom : b.top - r.bottom;
      const closedDetails = el.closest('details:not([open])');
      const collapsed = !!closedDetails && !(el.tagName === 'SUMMARY' && el.parentElement === closedDetails);
      return { el, visible: !collapsed && r.width > 0 && r.height > 0 && style.visibility !== 'hidden' && style.display !== 'none',
        forward, beam: overlap > .5 ? 0 : 1, separated: gap >= -.5, gap: Math.max(0, gap),
        distance: Math.hypot(x - bx, y - by), across: Math.abs(horizontal ? y - by : x - bx),
        sameVisualRow: Math.abs(y-by) <= Math.max(8,Math.min(b.height,r.height)/2+4) };
    // Horizontal movement stays on the visual row. A diagonal fallback at
    // the row edge used to jump to unrelated settings above or below it.
    // Vertical movement leaves that row, including controls whose centres
    // differ slightly because switches, dropdowns and buttons have different heights.
    }).filter(item => item.visible && item.forward > 1 && (horizontal ? item.beam === 0 && item.sameVisualRow : item.separated));
  if (!horizontal && pane) {
    // Staggered rows in another scroll pane must not intercept Up/Down.
    // Header/footer controls outside all panes remain reachable at the edge.
    const local = candidates.filter(item => { const other=item.el.closest<HTMLElement>('[data-gp-pane]'); return paneId ? other?.dataset.gpPane===paneId : other===pane; });
    const admitted = local.length ? local : candidates.filter(item => !item.el.closest('[data-gp-pane]'));
    candidates.splice(0,candidates.length,...admitted);
  }
  if (horizontal) {
    candidates.sort((a,b) => a.gap - b.gap || a.across - b.across || a.distance - b.distance);
  } else {
    // Visit the adjacent visual row first, then retain the nearest column.
    // Aligned controls several rows away must not win over a nearer row.
    const nearestCenter = Math.min(...candidates.map(item => item.forward));
    const rowTolerance = Math.max(6,Math.min(12, b.height / 2));
    const adjacentRow = candidates.filter(item => item.forward <= nearestCenter + rowTolerance);
    adjacentRow.sort((a,b) => a.across - b.across || a.gap - b.gap || a.distance - b.distance);
    return adjacentRow[0]?.el ?? null;
  }
  return candidates[0]?.el ?? null;
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
  if (current.closest('[data-gp-spatial="nearest"]')) return nearestSpatialTarget(elements, current, direction);
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
