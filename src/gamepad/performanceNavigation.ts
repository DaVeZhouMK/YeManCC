import { spatialNavigationTarget, type SpatialDirection } from './spatial';
/** One row map for the global, dedicated and simulation performance editors. */
export function performancePanelTarget(elements: readonly HTMLElement[], current: HTMLElement, direction: SpatialDirection): HTMLElement | null | undefined {
  const panel = current.closest<HTMLElement>('[data-gp-performance-panel]');
  const body = current.closest<HTMLElement>('[data-gp-custom-body]');
  const menu = current.closest<HTMLElement>('[data-gp-game-quick-menu]');
  const row = current.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow;
  const closestColumn = (items: readonly HTMLElement[]) => {
    const box = current.getBoundingClientRect(), x = box.left + box.width / 2;
    return [...items].sort((a, b) => {
      const ar = a.getBoundingClientRect(), br = b.getBoundingClientRect();
      return Math.abs(ar.left + ar.width / 2 - x) - Math.abs(br.left + br.width / 2 - x);
    })[0] ?? null;
  };
  const inRows = (items: readonly HTMLElement[], keys: readonly string[]) => items.filter(el =>
    keys.includes(el.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow || ''));
  if (!panel) {
    // The expanded dedicated entry leads into the first actual performance row.
    if (menu && row === 'custom-entry' && direction.dy > 0) {
      const owner = current.closest('[data-gp-custom-panel]');
      const candidates = elements.filter(el => owner?.contains(el) && el.closest('[data-gp-performance-panel]') && el.dataset.gpRow !== undefined);
      if (candidates.length) {
        const first = Math.min(...candidates.map(el => Number(el.dataset.gpRow)));
        return closestColumn(candidates.filter(el => Number(el.dataset.gpRow) === first));
      }
    }
    // The first input row below the editor returns to its last enabled row.
    if (body && direction.dy < 0 && ['custom-input-pad', 'custom-input-gyro'].includes(row || '')) {
      const candidates = elements.filter(el => body.contains(el) && el.closest('[data-gp-performance-panel]') && el.dataset.gpRow !== undefined);
      if (candidates.length) {
        const last = Math.max(...candidates.map(el => Number(el.dataset.gpRow)));
        return closestColumn(candidates.filter(el => Number(el.dataset.gpRow) === last));
      }
    }
    if (body && row === 'custom-actions' && direction.dy < 0) {
      const target = closestColumn(inRows(elements.filter(el => body.contains(el)), ['custom-input-pad', 'custom-input-gyro']));
      if (target) return target;
    }
    if (menu && row === 'rules-entry' && direction.dy < 0) {
      const target = closestColumn(inRows(elements.filter(el => menu.contains(el)), ['custom-actions']));
      if (target) return target;
    }
    return undefined;
  }
  // Scrollable game names consume horizontal input before the row map.
  if (current.closest('[data-gp-inline-edit]')) return undefined;
  const candidates = elements.filter(el => el.closest('[data-gp-performance-panel]') === panel);
  // The compact mode row has four columns; slider rows have two. Numeric
  // column 1 is a different screen position in those rows. Keep the declared
  // adjacent row but choose its actual visual column.
  if (direction.dy && current.dataset.gpRow !== undefined) {
    const row = Number(current.dataset.gpRow);
    const rows = [...new Set(candidates.map(el => Number(el.dataset.gpRow)).filter(Number.isFinite))].sort((a,b) => a-b);
    const adjacent = direction.dy > 0 ? rows.find(r => r > row) : [...rows].reverse().find(r => r < row);
    if (adjacent !== undefined) return closestColumn(candidates.filter(el => Number(el.dataset.gpRow) === adjacent));
  }
  const next = spatialNavigationTarget(candidates, current, direction);
  if (next) return next;
  if (body && direction.dy) {
    const siblings = elements.filter(el => direction.dy > 0 ? body.contains(el) : !!menu?.contains(el));
    const rowKeys = direction.dy > 0 ? ['custom-input-pad', 'custom-input-gyro'] : ['custom-entry'];
    const target = closestColumn(inRows(siblings, rowKeys));
    if (target) return target;
    if (direction.dy < 0) return closestColumn(inRows(siblings, ['controls']));
  }
  return null;
}
