import { getGamepadPopupPlacement, getGamepadViewportSafeArea, type GamepadPopupPlacement } from '@/gamepad/focus';

/** Teleported OPT dialogs use the page's visual width and scale, not body pixels. */
export function getOptiDialogPlacement(trigger: HTMLElement | null, panel: HTMLElement | null): GamepadPopupPlacement {
  const page = trigger?.closest<HTMLElement>('.game-recognition-popover')
    || document.querySelector<HTMLElement>('.app-content');
  const safe = getGamepadViewportSafeArea();
  const rect = page?.getBoundingClientRect();
  const scale = rect && page?.offsetWidth ? Math.max(.5, Math.min(4, rect.width / page.offsetWidth)) : 1;
  const css = page ? getComputedStyle(page) : null;
  const inset = (value: string | undefined) => (Number.parseFloat(value || '') || 0) * scale;
  // Top-menu dialogs match that menu's outer edges; in-page dialogs fit between
  // the content padding and reserved scrollbar gutter.
  const content = page?.matches('.app-content');
  const left = Math.max(safe.left, (rect?.left ?? safe.left) + (content ? inset(css?.paddingLeft) : 0));
  const pageWidth = content && page
    ? page.clientWidth * scale - inset(css?.paddingLeft) - inset(css?.paddingRight)
    : rect?.width ?? safe.right - left;
  const width = Math.max(0, Math.min(pageWidth, safe.right - left));
  const source = trigger?.getBoundingClientRect();
  const anchor = source ? new DOMRect(left, source.top, width, source.height) : null;
  const height = panel ? panel.scrollHeight + 2 : 360 * scale;
  const placement = getGamepadPopupPlacement(anchor, width, height, 8 * scale);
  const available = safe.bottom - safe.top;
  // Use the whole safe viewport if neither side fits; avoid a needlessly short
  // scroller above/below the trigger while the rest of the viewport is empty.
  if (!anchor || Number.parseFloat(placement.style.maxHeight) < Math.min(height, available) - 1) {
    delete placement.style.bottom;
    placement.style.top = Math.max(safe.top, Math.min(source?.bottom ?? safe.top, safe.bottom - Math.min(height, available))) + 'px';
    placement.style.maxHeight = available + 'px';
    placement.above = false;
  }
  placement.style.left = left + 'px';
  placement.style.fontSize = 14 * scale + 'px';
  return placement;
}
