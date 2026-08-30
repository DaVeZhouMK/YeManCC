const DEFAULT_SAFE_TOP = 24;
const DEFAULT_SAFE_BOTTOM = 24;
// 手柄回滚时给当前控件上方的页面标题/卡片标题留一点上下文，
// 避免焦点虽然可见，但同一页顶部内容被贴出滚动视口。
const FOCUS_CONTEXT_TOP = 16;
// 只在控件靠近底部时做小幅回收，避免一次方向键把整页大幅拉到中间。
const GENTLE_BOTTOM_ZONE = 64;
const GENTLE_REPOSITION_MAX = 72;
const GENTLE_CENTER_RATIO = 0.54;

function parsePixels(value: string, fallback: number): number {
  const parsed = Number.parseFloat(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : fallback;
}

function visualScale(container: HTMLElement): number {
  if (container.clientHeight <= 0) return 1;
  const rectHeight = container.getBoundingClientRect().height;
  return rectHeight > 0
    ? Math.max(0.5, Math.min(4, rectHeight / container.clientHeight))
    : 1;
}

export interface GamepadViewportSafeArea {
  top: number;
  bottom: number;
  left: number;
  right: number;
}

export interface GamepadPopupPlacement {
  style: Record<string, string>;
  above: boolean;
}

/**
 * Return visual viewport bounds for fixed/teleported UI. The app content's
 * scroll-padding is authored in the zoomed app coordinate space, so convert
 * it to viewport pixels before using it for a fixed popup.
 */
export function getGamepadViewportSafeArea(): GamepadViewportSafeArea {
  const content = document.querySelector<HTMLElement>('.app-content');
  const fallbackBottom = Math.max(32, Math.min(64, window.innerHeight * 0.06));
  let bottomGap = fallbackBottom;
  let topGap = DEFAULT_SAFE_TOP;

  if (content) {
    const style = getComputedStyle(content);
    const scale = visualScale(content);
    bottomGap = parsePixels(style.scrollPaddingBottom, DEFAULT_SAFE_BOTTOM) * scale;
    topGap = parsePixels(style.scrollPaddingTop, DEFAULT_SAFE_TOP) * scale;
    const contentRect = content.getBoundingClientRect();
    const clippedBottomCss = Math.max(0, (contentRect.bottom - window.innerHeight) / scale);
    // CSS zoom can place the scroll container below the real viewport. Add
    // that clipped tail to the scrollable content so maxScrollTop can reveal
    // the final control instead of stopping at the container's layout bottom.
    content.style.setProperty('--gamepad-clip-bottom', `${Math.ceil(clippedBottomCss)}px`);
  }

  bottomGap = Math.max(DEFAULT_SAFE_BOTTOM, Math.min(64, bottomGap));
  topGap = Math.max(DEFAULT_SAFE_TOP, Math.min(32, topGap));
  const safeBottom = Math.min(
    window.innerHeight,
    Math.max(topGap + 40, window.innerHeight - bottomGap),
  );

  // Teleported overlays do not inherit .app-stage's zoomed CSS variables.
  document.documentElement.style.setProperty(
    '--gamepad-viewport-bottom-gap',
    `${Math.ceil(window.innerHeight - safeBottom)}px`,
  );

  return {
    top: topGap,
    bottom: safeBottom,
    left: 8,
    right: Math.max(8, window.innerWidth - 8),
  };
}

/** Position a fixed popup without allowing it to enter the bottom safe area. */
export function getGamepadPopupPlacement(
  anchor: DOMRect | null,
  width: number,
  desiredHeight: number,
  gap = 8,
): GamepadPopupPlacement {
  const safe = getGamepadViewportSafeArea();
  const popupWidth = Math.max(0, Math.min(width, window.innerWidth - safe.left - (window.innerWidth - safe.right)));
  const left = anchor
    ? Math.max(safe.left, Math.min(anchor.right - popupWidth, safe.right - popupWidth))
    : Math.max(safe.left, (window.innerWidth - popupWidth) / 2);
  const safeHeight = Math.max(0, safe.bottom - safe.top);
  if (!anchor) {
    return {
      above: false,
      style: {
        position: 'fixed',
        left: `${left}px`,
        top: `${safe.top}px`,
        width: `${popupWidth}px`,
        maxHeight: `${Math.max(1, safeHeight)}px`,
      },
    };
  }

  const spaceBelow = Math.max(0, safe.bottom - anchor.bottom - gap);
  const spaceAbove = Math.max(0, anchor.top - safe.top - gap);
  const above = spaceBelow < desiredHeight && spaceAbove > spaceBelow;
  const available = above ? spaceAbove : spaceBelow;
  if (available <= 0) {
    return {
      above: false,
      style: {
        position: 'fixed',
        left: `${left}px`,
        top: `${safe.top}px`,
        width: `${popupWidth}px`,
        maxHeight: `${Math.max(1, safeHeight)}px`,
      },
    };
  }

  const maxHeight = Math.max(1, Math.min(desiredHeight, available));

  if (above) {
    return {
      above: true,
      style: {
        position: 'fixed',
        left: `${left}px`,
        bottom: `${window.innerHeight - anchor.top + gap}px`,
        width: `${popupWidth}px`,
        maxHeight: `${maxHeight}px`,
      },
    };
  }

  return {
    above: false,
    style: {
      position: 'fixed',
      left: `${left}px`,
        top: `${anchor.bottom + gap}px`,
      width: `${popupWidth}px`,
      maxHeight: `${maxHeight}px`,
    },
  };
}

function findScrollableAncestors(el: HTMLElement): HTMLElement[] {
  const result: HTMLElement[] = [];
  let node = el.parentElement;
  while (node && node !== document.body) {
    const style = getComputedStyle(node);
    const scrollable = /(auto|scroll|overlay)/.test(style.overflowY) &&
      node.scrollHeight > node.clientHeight + 1;
    if (scrollable) result.push(node);
    node = node.parentElement;
  }
  return result;
}

function getVisibleScrollRect(container: HTMLElement): { top: number; bottom: number } {
  const containerRect = container.getBoundingClientRect();
  let top = Math.max(0, containerRect.top);
  let bottom = Math.min(window.innerHeight, containerRect.bottom);
  let node = container.parentElement;

  // The scroll container can extend outside a zoomed app-stage. Its usable
  // viewport is the intersection with every ancestor that clips overflow.
  while (node && node !== document.body) {
    const style = getComputedStyle(node);
    const clips = /(hidden|clip|auto|scroll|overlay)/.test(style.overflowY) ||
      /(hidden|clip|auto|scroll|overlay)/.test(style.overflowX);
    if (clips) {
      const rect = node.getBoundingClientRect();
      top = Math.max(top, rect.top);
      bottom = Math.min(bottom, rect.bottom);
    }
    node = node.parentElement;
  }

  return { top, bottom: Math.max(top, bottom) };
}

/** Keep an element fully inside every scrollable ancestor's safe area.
 * When a target enters from below, gently favor the middle of the usable
 * viewport. A target that is only slightly clipped is moved just enough to
 * become fully visible; a target already visible near the bottom gets only a
 * small one-shot nudge. This keeps the old edge-safe behavior without making
 * the focused control sit against the bottom edge.
 */
export function scrollElementIntoSafeArea(el: HTMLElement): void {
  for (const container of findScrollableAncestors(el)) {
    const style = getComputedStyle(container);
    let visualPerScrollUnit = visualScale(container);
    let gentleShiftRemaining = GENTLE_REPOSITION_MAX;
    let visibilityCorrectionApplied = false;
    const safeTopCss = parsePixels(style.scrollPaddingTop, 0);
    const safeBottomCss = parsePixels(style.scrollPaddingBottom, 0);

    // Re-measure after each correction. This handles scrollTop clamping and
    // CSS zoom without making assumptions about the browser's scroll model.
    for (let attempt = 0; attempt < 4; attempt++) {
      const visibleRect = getVisibleScrollRect(container);
      const elementRect = el.getBoundingClientRect();
      const safeTop = (safeTopCss + FOCUS_CONTEXT_TOP) * visualPerScrollUnit;
      const safeBottom = safeBottomCss * visualPerScrollUnit;
      const viewportTop = visibleRect.top + safeTop;
      const viewportBottom = visibleRect.bottom - safeBottom;
      const availableHeight = Math.max(0, viewportBottom - viewportTop);
      let visualDelta = 0;
      let gentleCorrectionRequested = false;

      if (elementRect.height > availableHeight) {
        visualDelta = elementRect.top - viewportTop;
      } else if (elementRect.top < viewportTop) {
        visualDelta = elementRect.top - viewportTop;
      } else if (elementRect.bottom > viewportBottom) {
        const overflow = elementRect.bottom - viewportBottom;
        if (overflow <= GENTLE_BOTTOM_ZONE) {
          // First guarantee complete visibility, preserving the old strategy
          // for controls that are only a few pixels below the safe edge.
          visualDelta = overflow;
          visibilityCorrectionApplied = true;
        } else {
          // Larger jumps are easier to use when the focused control lands
          // around the middle, but the desired center is still slightly below
          // the true midpoint so the page does not feel over-scrolled.
          const minCenter = viewportTop + elementRect.height / 2;
          const maxCenter = viewportBottom - elementRect.height / 2;
          const desiredCenter = Math.max(
            minCenter,
            Math.min(maxCenter, viewportTop + availableHeight * GENTLE_CENTER_RATIO),
          );
          visualDelta = elementRect.top + elementRect.height / 2 - desiredCenter;
        }
      } else if (
        !visibilityCorrectionApplied &&
        gentleShiftRemaining > 0 &&
        elementRect.bottom > viewportBottom - GENTLE_BOTTOM_ZONE
      ) {
        // Already visible but visually low: pull it up only a little. The
        // remaining budget is shared by the re-measurement loop below, so a
        // single focus operation cannot turn into a full-page reposition.
        const minCenter = viewportTop + elementRect.height / 2;
        const maxCenter = viewportBottom - elementRect.height / 2;
        const desiredCenter = Math.max(
          minCenter,
          Math.min(maxCenter, viewportTop + availableHeight * GENTLE_CENTER_RATIO),
        );
        const desiredDelta = elementRect.top + elementRect.height / 2 - desiredCenter;
        visualDelta = Math.max(-gentleShiftRemaining, Math.min(gentleShiftRemaining, desiredDelta));
        gentleCorrectionRequested = Math.abs(visualDelta) >= 0.5;
      }

      if (Math.abs(visualDelta) < 0.5) break;

      const beforeScroll = container.scrollTop;
      const beforeTop = elementRect.top;
      const maxScrollTop = Math.max(0, container.scrollHeight - container.clientHeight);
      const next = Math.max(
        0,
        Math.min(maxScrollTop, beforeScroll + visualDelta / visualPerScrollUnit),
      );
      container.scrollTop = next;
      const actualScroll = container.scrollTop;
      if (Math.abs(actualScroll - beforeScroll) < 0.5) break;

      const actualVisualMovement = beforeTop - el.getBoundingClientRect().top;
      if (Math.abs(actualVisualMovement) >= 0.5) {
        if (gentleCorrectionRequested) {
          gentleShiftRemaining = Math.max(0, gentleShiftRemaining - Math.abs(actualVisualMovement));
        }
        visualPerScrollUnit = Math.max(
          0.5,
          Math.min(4, Math.abs(actualVisualMovement / (actualScroll - beforeScroll))),
        );
      }
    }
  }
}

export function setGamepadFocused(el: HTMLElement | null): void {
  document.querySelectorAll('.focused').forEach((node) => node.classList.remove('focused'));
  if (el) el.classList.add('focused');
}

/** Programmatic focus entry point for controller navigation and modal restore. */
export function focusGamepadElement(el: HTMLElement | null): boolean {
  if (!el || !el.isConnected || el.hasAttribute('disabled')) return false;
  // data-gp-ignore is an explicit product decision: the control may remain
  // clickable by mouse/touch, but controller focus must never land on it.
  if (el.matches('[data-gp-ignore]') || el.closest('[data-gp-ignore]')) return false;
  if (el.getAttribute('aria-hidden') === 'true' || el.closest('[hidden], [inert], [aria-hidden="true"]')) return false;
  const style = getComputedStyle(el);
  if (style.display === 'none' || style.visibility === 'hidden') return false;
  el.focus({ preventScroll: true });
  getGamepadViewportSafeArea();
  scrollElementIntoSafeArea(el);
  setGamepadFocused(el);
  return document.activeElement === el;
}
