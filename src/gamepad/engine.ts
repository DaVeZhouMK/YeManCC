// gamepad/engine.ts — 手柄全量导航引擎（对齐 PLAN §五）
//
// Native Raw Input/XInput supplies page actions; this module maps those
// actions to DOM geometry and unified dispatch for the page UI.
// 无手柄时引擎静默，不影响纯鼠标用户。
//
// 按键映射（Xbox 360 / XInput STANDARD GAMEPAD）：
//   LB(4)/RB(5)    → 切换上/下一页
//   A(0)           → 确认/点击/切换
//   B(1)           → 取消/blur
//   Y(3)           → 全局编辑游戏识别名单
//   X(2)           → 下拉菜单（与 A 一致：打开菜单选择）
//   Start(9)       → 调试面板
//   D-pad/左摇杆   → 页面内焦点移动（空间导航）
//   LT(6)/RT(7)/←→ → 滑块微调（仅 A 进入“编辑模式”后；未进入时方向键只移动焦点）
import { ROUTES } from '@/router';
import type { Router } from 'vue-router';
import { loadPerformanceSchedule } from '@/bridge/performanceSchedule';
import { summonGet, type GamepadSettings } from '@/bridge/yeman';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { focusGamepadElement, setGamepadFocused } from '@/gamepad/focus';
import { enqueueGamepadTaskDetached } from '@/gamepad/serial';

export interface GamepadEngineOptions {
  router: Router;
  onAction?: (label: string) => void;
}

// 可聚焦元素选择器（需与各 View 的 tabindex 配合）。
// 带 data-gp-ignore 的元素被排除——用于左侧 NavRail 的页面图标/刷新/退出，避免上下左右误切页/退出。
const FOCUSABLE =
  'button:not([data-gp-ignore]), [tabindex]:not([tabindex="-"]):not([data-gp-ignore]), input:not([disabled]):not([data-gp-ignore]), select:not([disabled]):not([data-gp-ignore])';

let rafId = 0;
let prevButtons: boolean[] = [];
let prevAxes: number[] = [];
// Per-frame snapshots intentionally follow the legacy engine semantics. This
// keeps edge detection stable across summon, visibility and reconnect resets.
let lastNav = 0;
let lastPageSwitch = -Infinity; // 切页冷却（防止连按卡顿）
const PAGE_SWITCH_COOLDOWN = 100; // ms，原 400ms 过严致连按 LB/RB 丢按；降到 100ms 使快速切页（含切到 TDP）每次都生效
// 滑块线性加速：按住越久步长越大、间隔越短
// ⚠️ 速度参数需与 native 手柄 Start+方向 自动连发保持一致（见 main.cpp 注释），修改时两边同步。
let sliderAccelStart = 0;  // 本次按住起始时间（用于线性加速）
let sliderLastApply = 0;   // 上次应用加速步进的时间
const SLIDER_REPEAT_BASE = 150; // ms：基础重复间隔（原 45，放慢 30%+，短按只调 1 次）
const SLIDER_REPEAT_MIN  = 40;  // ms：最快重复间隔（原 16，放慢）
const SLIDER_ACCEL_DELAY = 500; // ms：起跳后多久才开始加速（原 350）
const SLIDER_ACCEL_STEP  = 400; // ms：每过这么久步长 +1（原 280）
const SLIDER_ACCEL_CAP   = 12;  // 单帧最大步长（原 20）
let gamepadConnected = false; // 追踪手柄连接状态
let lastPadTs = 0;   // 上一帧手柄读数时间戳（用于检测唤醒后冻结快照）
let stuckSince = 0;  // 读数停滞起始时间
let sliderEditMode = false; // 滑块编辑模式：A 进入后，左右键调整数值，B/上下退出
let testMode = false;       // 手柄测试模式：仅显示/检测输入，不操作程序 UI
let testBHoldStart = 0;     // 测试模式中 B 按住退出计时
const TEST_B_HOLD_MS = 3000;
let gpSettings: GamepadSettings = {
  enabled: true,
  bDoubleMinimize: true,
  tdpShortcut: true,
  fpsShortcut: true,
  killGame: false,
  openKeyboard: false,
  returnDesktop: false,
  mouseToggle: false,
  mouseBackend: 'joyxoff',
};
// 快捷应用「模拟鼠标」开启时：屏蔽内页按键选择/功能（上下导航、A/X），
// 保留 LR 切页、B 返回和 Y 编辑游戏；由 QuickAppView 通过 gp:mouse-mode 事件驱动。
let mouseModeSuppress = false;
export function isMouseModeSuppressed(): boolean {
  return mouseModeSuppress;
}
let startUsedAsModifier = false; // Start 键是否在本轮按住中被用作快捷调节修饰键

// ── LB/RB 切页防误触（LB+RB 呼出组合）──
// 切页由按下边沿触发，而呼出是“按住 LB+RB 0.5s”。若按下瞬间直接切页，
// 呼出组合按住期间会把页面切走。因此改为挂起一帧裁决：
// 按下 LB/RB 先不切页，下一帧看另一键是否也已按下（组合）→ 取消；否则执行切页。
let lbNavPending = false; // LB 按下边沿待裁决
let rbNavPending = false; // RB 按下边沿待裁决
let summonSuppress = false; // 呼出后直到 LB/RB 全释放前抑制切页（呼出动作本身不产生页面移动）
let shoulderReleaseLockUntil = 0; // 组合键松开后的防抖窗口，丢弃释放抖动产生的单键边沿
const SUMMON_POST_RELEASE_LOCKOUT_MS = 450;
// Native focus and WebView2 compositor visibility settle independently. Keep
// a short, cancellable focus session so a summon that races a restore/repaint
// cannot leave the DOM without a gamepad focus target.
let summonFocusGeneration = 0;
let summonFocusTimers: number[] = [];

let engineOpts: GamepadEngineOptions | null = null; // 供连接/可见/唤醒事件重启循环使用
let noPadStreak = 0;       // 连续无手柄帧计数（Grace 防瞬时断连误杀）
const NO_PAD_GRACE = 30;   // 约 0.5s@60fps：连续这么多帧无手柄才停止循环
let summonActive = false;  // 呼出后置 true：强制视为可见，直到窗口再次隐藏（绕开 WebView2 可见态未及时刷新）
let nativeUiInputActive = false;
type NativeUiAction =
  | 'page-prev' | 'page-next' | 'confirm' | 'back' | 'dropdown' | 'edit-game'
  | 'debug' | 'nav-left' | 'nav-right' | 'nav-up' | 'nav-down'
  | 'slider-decrease' | 'slider-increase';

// 是否有任意已连接手柄
function hasConnectedPad(): boolean {
  try {
    return false;
  } catch { /* 某些 WebView2 版本 getGamepads() 可能抛异常 */ }
  return false;
}

// Native owns the controller state in every visibility state. The renderer
// only consumes native actions and never polls a browser controller snapshot.
function browserLoopAllowed(): boolean {
  return summonActive || (
    document.visibilityState === 'visible' && isUiVisible()
  );
}

// 若循环未运行则启动（幂等）：用于连接/可见/唤醒事件拉起
function ensureLoop(opts: GamepadEngineOptions) {
  if (rafId || !browserLoopAllowed()) return;
  noPadStreak = 0;
  rafId = requestAnimationFrame(() => tick(opts));
}

// 监听手柄连接/断开事件（某些 WebView2 版本需要此触发轮询）
if (typeof window !== 'undefined') {
  // 插入/切换手柄 → 立即重启循环
  window.addEventListener('native-pad-added', () => {
    gamepadConnected = true;
    if (engineOpts && browserLoopAllowed()) ensureLoop(engineOpts);
  });
  // 断开 → 若已无任何手柄仍连着，停止循环（省 CPU）；仍有其他手柄则保留
  window.addEventListener('native-pad-removed', () => {
    if (!hasConnectedPad()) {
      gamepadConnected = false;
      stopGamepadLoop();
    }
  });
  window.addEventListener('performance-schedule:visibility', (e: Event) => {
    const enabled = Boolean((e as CustomEvent<{ detail?: { enabled?: boolean }; enabled?: boolean }>).detail?.enabled);
    setVisibleRoutes(enabled);
  });
  // 睡眠守护唤醒后，native 通知重启手柄引擎：重置边沿/时间戳状态，并尝试重启循环
  window.addEventListener('ipc:gamepad.restart', () => {
    restartGamepadState();
    if (engineOpts && hasConnectedPad() && browserLoopAllowed()) ensureLoop(engineOpts);
    window.setTimeout(() => {
      if (!engineOpts || !browserLoopAllowed()) return;
      const active = document.activeElement as HTMLElement | null;
      if (!active || !focusables().includes(active)) focusFirst();
    }, 120);
  });
  window.addEventListener('ipc:gamepad.ui-input', ((e: CustomEvent<{ action?: string }>) => {
    const action = e.detail?.action as NativeUiAction | undefined;
    if (!engineOpts || !action || testMode) return;
    nativeUiInputActive = true;
    enqueueGamepadTaskDetached(() => dispatchNativeUiAction(engineOpts!, action));
  }) as EventListener);
  onUiVisibilityChange(({ visible }) => {
    if (visible && engineOpts && hasConnectedPad()) {
      ensureLoop(engineOpts);
    } else if (!visible && !summonActive) {
      // Raw Input remains registered in native for summon/global shortcuts;
      // there is no renderer work to do while the window is hidden.
      stopGamepadLoop();
    }
  });
  // R1L1 呼出（native 后台线程 bringToFront 后发出）：强制接管手柄导航。
  // ① 置 summonActive 让循环即便 WebView2 可见态未刷新也照常处理输入；
  // ② 置 summonSuppress：呼出后到 LB/RB 全部松开前抑制切页，避免呼出动作误切页；
  // ③ 重置边沿基线，避免呼出瞬间把“仍按住的 R1L1”误判成新的按下边沿（误切页）；
  // ④ 重启循环（若此前因无手柄被停 / 隐藏态未运行，现在必定拉起）。
  window.addEventListener('ipc:gamepad.summon', () => {
    summonActive = true;
    summonSuppress = true;
    // The native event is delivered immediately after the window is focused.
    // Do not impose the wake-up neutral-frame gate here: the summon combo is
    // already known to be held, and waiting for two empty frames made the
    // first L/R press after releasing the combo disappear. The shoulder
    // suppression below still prevents the held summon combo from navigating.
    restartGamepadState();
    if (engineOpts) ensureLoop(engineOpts);
    scheduleSummonFocus();
  });
  // A native tray hide ends the summon session even when Chromium does not
  // emit a matching document.visibilitychange event.
  const clearSummonState = () => {
    summonActive = false;
    summonSuppress = false;
    shoulderReleaseLockUntil = 0;
    lbNavPending = false;
    rbNavPending = false;
    cancelSummonFocus();
    if (!browserLoopAllowed()) stopGamepadLoop();
  };
  window.addEventListener('ipc:window.hidden', clearSummonState);
  window.addEventListener('ipc:window.minimized', clearSummonState);
  // 回到可见（含系统唤醒窗口恢复可见）：兜底探测手柄并启动循环
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      if (engineOpts && hasConnectedPad() && browserLoopAllowed()) ensureLoop(engineOpts);
    } else if (!summonActive) {
      // 窗口再次隐藏：清除呼出强制态并停止 RAF；native Raw Input 仍负责
      // 后台唤醒，不需要浏览器在托盘中持续采样。
      summonActive = false;
      summonSuppress = false; // 隐藏时不再需要呼出抑制，下次呼出会重新设置
      shoulderReleaseLockUntil = 0;
      lbNavPending = false;   // 丢弃未裁决的切页挂起，恢复可见后不误切页
      rbNavPending = false;
      cancelSummonFocus();
      stopGamepadLoop();
    }
  });
  // 设置页实时同步手柄快捷开关
  window.addEventListener('ipc:gamepad.settings', ((e: CustomEvent<GamepadSettings>) => {
    if (e.detail) gpSettings = { ...gpSettings, ...e.detail };
  }) as EventListener);
  // 手柄测试模式：仅检测输入，不操作 UI
  window.addEventListener('ipc:gamepad.testmode', ((e: CustomEvent<boolean>) => {
    testMode = !!e.detail;
  }) as EventListener);
}
// 重置手柄引擎边沿状态（唤醒后由 native 触发，也用于解冻卡死快照）
function restartGamepadState() {
  prevButtons = [];
  prevAxes = [];
  gamepadConnected = false;
  lastPadTs = 0;
  stuckSince = 0;
  lastPageSwitch = -Infinity;
  lbNavPending = false; // 丢弃未裁决的切页挂起，避免基线重建后误切页
  rbNavPending = false;
  shoulderReleaseLockUntil = 0;
}

function log(opts: GamepadEngineOptions, msg: string) {
  opts.onAction?.(msg);
}

function getPad(): Gamepad | null {
  try {
    return null;
  } catch {
    /* 某些 WebView2 版本 getGamepads() 可能抛异常 */
  }
  return null;
}

let visibleRoutePaths: string[] = ROUTES.map((r) => r.path);
let visibleRouteTitles: string[] = ROUTES.map((r) => r.title);

function setVisibleRoutes(enabled: boolean) {
  const hidden = enabled ? new Set(['/tdp', '/cpu']) : new Set<string>();
  const visible = ROUTES.filter((r) => !hidden.has(r.path));
  visibleRoutePaths = visible.map((r) => r.path);
  visibleRouteTitles = visible.map((r) => r.title);
}

async function refreshVisibleRoutes() {
  const cfg = await loadPerformanceSchedule().catch(() => null);
  setVisibleRoutes(cfg?.enabled === true);
}

// ── 页面切换 ──
function navigate(opts: GamepadEngineOptions, dir: -1 | 1) {
  enqueueGamepadTaskDetached(() => navigateNow(opts, dir));
}

async function navigateNow(opts: GamepadEngineOptions, dir: -1 | 1) {
  const order = visibleRoutePaths;
  const cur = opts.router.currentRoute.value.path;
  let idx = order.indexOf(cur);
  if (idx < 0) idx = 0;
  idx = (idx + dir + order.length) % order.length;
  await opts.router.push(order[idx]);
  await new Promise<void>((resolve) => window.setTimeout(resolve, 80));
  focusFirst();
  log(opts, `切页 → ${visibleRouteTitles[idx]}`);
}

// 自动聚焦页面内首个可见可聚焦元素（手柄初值光标位置）
function focusFirst(): boolean {
  const els = focusables();
  if (els.length === 0) return false;
  const el = els[0];
  try {
    if (!focusGamepadElement(el)) {
      el.focus({ preventScroll: false });
      setGamepadFocused(el);
    }
    return document.activeElement === el;
  } catch {
    // A transient WebView2 repaint can invalidate the element between the
    // visibility scan and focus(); retrying on the next summon/frame is safe.
    return false;
  }
}

function cancelSummonFocus(): void {
  summonFocusGeneration += 1;
  for (const timer of summonFocusTimers) window.clearTimeout(timer);
  summonFocusTimers = [];
}

function scheduleSummonFocus(): void {
  cancelSummonFocus();
  const generation = summonFocusGeneration;
  const attempt = () => {
    if (generation !== summonFocusGeneration || !summonActive) return;
    // Do not steal a focus target that the user or a route transition has
    // already established. Only retry when WebView2/Vue still left the page
    // without a usable controller target.
    const active = document.activeElement as HTMLElement | null;
    if (active && focusables().includes(active)) return;
    focusFirst();
  };
  requestAnimationFrame(attempt);
  // Native show/restore, WebView2 paint, and Vue route rendering can complete
  // in different turns. These bounded checkpoints cover all three without
  // leaving a permanent timer or stealing focus after the session ends.
  for (const delay of [16, 80, 180, 360, 640]) {
    summonFocusTimers.push(window.setTimeout(attempt, delay));
  }
}

// ── 页面内焦点列表 ──
// 若有自绘模态浮层（data-gp-modal，如确认弹窗），导航范围收窄到浮层内部，
// 防止方向键从弹窗按钮跳出到下层页面误触其它按钮；浮层关闭后恢复全页。
function focusables(): HTMLElement[] {
  // Transitions can leave the old panel in the DOM briefly. Use the last
  // visible modal so a newly opened Teleport overlay owns navigation.
  const modals = Array.from(document.querySelectorAll<HTMLElement>('[data-gp-modal]'))
    .filter((el) => {
      const style = getComputedStyle(el);
      const r = el.getBoundingClientRect();
      return style.display !== 'none' && style.visibility !== 'hidden' && r.width > 0 && r.height > 0;
    });
  const modal = modals[modals.length - 1] ?? null;
  const root: ParentNode = modal || document;
  return Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE)).filter(
    (el) => {
      if (el.hasAttribute('disabled')) return false;
      if (el.getAttribute('aria-hidden') === 'true') return false;
      if (el.closest('[aria-hidden="true"]')) return false;
      if (el.closest('[hidden], [inert]')) return false;
      if (el.getAttribute('aria-disabled') === 'true') return false;
      // Vue Transition 会在离场动画期间保留二级菜单 DOM。父气泡状态
      // 已经折叠时，即使旧按钮仍有尺寸，也绝不能进入手柄候选列表。
      const submenuBody = el.closest<HTMLElement>('[data-gp-game-rules-body], [data-gp-custom-body]');
      const submenuPanel = submenuBody?.closest<HTMLElement>('[data-gp-expanded]');
      if (submenuBody && submenuPanel?.dataset.gpExpanded !== 'true') return false;
      const style = getComputedStyle(el);
      if (style.display === 'none' || style.visibility === 'hidden') return false;
      const r = el.getBoundingClientRect();
      // 必须可见且有实际尺寸
      return r.width > 0 && r.height > 0;
    }
  );
}

// ── 可见焦点高亮（手柄/键盘）：显式切换 .focused，
//    因为 Chromium 对程序化 .focus() 不触发 :focus-visible，
//    而 tokens.css 的 .focused 提供 accent 发光环 ──
// ── 空间方向导航（对齐 HTA 的方向键焦点切换） ──
function isInsideCustomPanel(el: HTMLElement): boolean {
  return !!el.closest('[data-gp-custom-body]');
}

function firstGameMenuControl(root: ParentNode | null): HTMLElement | null {
  if (!root) return null;
  const selector = 'button:not(:disabled):not([data-gp-ignore]), input:not(:disabled):not([data-gp-ignore]), select:not(:disabled):not([data-gp-ignore]), [tabindex]:not([tabindex="-1"]):not([data-gp-ignore])';
  const direct = root instanceof HTMLElement && root.matches(selector) ? root : null;
  return direct || root.querySelector<HTMLElement>(selector);
}

function gameMenuControlForRow(menu: HTMLElement, rowKey: string): HTMLElement | null {
  if (!rowKey) return null;
  const row = menu.querySelector<HTMLElement>(`[data-gp-game-row="${rowKey}"]`);
  if (!row) return null;
  const target = firstGameMenuControl(row);
  if (!target) return null;
  if (target.hasAttribute('disabled') || target.getAttribute('aria-disabled') === 'true') return null;
  if (target.closest('[hidden], [inert], [aria-hidden="true"]')) return null;
  const body = target.closest<HTMLElement>('[data-gp-game-rules-body], [data-gp-custom-body]');
  const panel = body?.closest<HTMLElement>('[data-gp-expanded]');
  if (body && panel?.dataset.gpExpanded !== 'true') return null;
  const style = getComputedStyle(target);
  const rect = target.getBoundingClientRect();
  return style.display === 'none' || style.visibility === 'hidden' || rect.width <= 0 || rect.height <= 0
    ? null
    : target;
}

function moveGameMenuFocus(menu: HTMLElement, base: HTMLElement, dy: number): HTMLElement | null {
  const currentRow = base.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow || '';
  const rows = [
    'controls',
    'actions-1',
    'actions-2',
    'rules-entry',
    'rules-current',
    'rules-editor',
    'rules-dropdown',
    'rules-footer',
    'custom-entry',
    'custom-ac',
    'custom-dc',
    'custom-actions',
    'footer',
  ];
  const index = rows.indexOf(currentRow);
  if (index < 0) return null;
  for (let i = index + (dy > 0 ? 1 : -1); i >= 0 && i < rows.length; i += dy > 0 ? 1 : -1) {
    const target = gameMenuControlForRow(menu, rows[i]);
    if (target && target !== base) return target;
  }
  return null;
}

function moveFocus(dx: number, dy: number) {
  const els = focusables();
  if (els.length === 0) return;
  const activeGameMenu = document.querySelector<HTMLElement>('[data-gp-game-quick-menu]');
  const customPanel = activeGameMenu?.querySelector<HTMLElement>('[data-gp-custom-body]');
  const customExpanded = !!customPanel;
  // The dedicated-profile editor is a self-contained controller region. Once
  // expanded, do not let spatial navigation land on its hidden/colliding
  // header or its action row. From the profile's last visible control, Up
  // exits directly to the blacklist/whitelist entry as requested.
  // The collapsed/expanded header is a visual container, not a second
  // controller stop. The action buttons remain in the list when expanded so A
  // can still activate them, but the header itself is excluded to prevent the
  // first profile row from becoming a duplicate/overlapping focus target.
  const rulesExpanded = !!activeGameMenu?.querySelector<HTMLElement>('[data-gp-game-rules-body]');
  // 可见的入口和可见的独立气泡内容都是真实焦点目标；只有 v-if 已移除、
  // inert/aria-hidden 或不可见的内容才由 focusables() 排除。
  const navEls = els;
  const cur = document.activeElement as HTMLElement | null;
  // 当前焦点元素必须在列表中，否则从第一个开始
  const baseIdx = cur ? navEls.indexOf(cur) : -1;
  const base = baseIdx >= 0 ? navEls[baseIdx] : navEls[0];
  if (activeGameMenu && dy !== 0 && activeGameMenu.contains(base)) {
    const currentGameRow = base.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow || '';
    const target = moveGameMenuFocus(activeGameMenu, base, dy);
    if (target) {
      try {
        focusGamepadElement(target, dy < 0);
      } catch {
        // Ignore transient DOM changes during Vue transitions.
      }
      return;
    }
    // 顶部游戏菜单任何已声明行都由上面的专用导航负责；没有目标时
    // 停在当前行，禁止旧的全页空间算法把焦点穿到其它控件。
    if (currentGameRow) return;
  }
  const br = base.getBoundingClientRect();
  const bx = br.left + br.width / 2;
  const by = br.top + br.height / 2;
  let best: HTMLElement | null = null;
  type FocusBox = { el: HTMLElement; r: DOMRect; x: number; y: number };
  const boxes: FocusBox[] = navEls
    .filter((el) => el !== base)
    .map((el) => {
      const r = el.getBoundingClientRect();
      return { el, r, x: r.left + r.width / 2, y: r.top + r.height / 2 };
    });

  if (dy !== 0) {
    const rulesEntry = activeGameMenu?.querySelector<HTMLElement>('[data-gp-game-rules-entry]');
    const customEntry = activeGameMenu?.querySelector<HTMLElement>('[data-gp-custom-entry]');
    const customBody = activeGameMenu?.querySelector<HTMLElement>('[data-gp-custom-body]');
    const customExpanded = !!customBody;
    const gameRowKey = (el: HTMLElement | null): string => {
      if (!activeGameMenu || !el || !activeGameMenu.contains(el)) return '';
      return el.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow || '';
    };
    const nearestGameRow = (rowKey: string): HTMLElement | null => {
      if (!rowKey) return null;
      const candidates = navEls.filter((el) => el !== base && gameRowKey(el) === rowKey);
      if (!candidates.length) return null;
      candidates.sort((a, b) => {
        const ar = a.getBoundingClientRect();
        const br = b.getBoundingClientRect();
        const ax = ar.left + ar.width / 2;
        const bx2 = br.left + br.width / 2;
        return Math.abs(ax - bx) - Math.abs(bx2 - bx);
      });
      return candidates[0];
    };
    // 一级入口必须是“可直接聚焦”的目标。不要只依赖空间导航的候选数组：
    // Vue 在切换气泡/动画的中间帧可能会让入口暂时不在 navEls 中，导致
    // 手柄上下时把它跳过，但鼠标仍然可以点击。这里按菜单行语义直接
    // 抓取入口，作为顶部游戏菜单的硬兜底。
    const directGameMenuTarget = (rowKey: string): HTMLElement | null => {
      if (!activeGameMenu || !rowKey) return null;
      const row = activeGameMenu.querySelector<HTMLElement>(
        `[data-gp-game-row="${rowKey}"]`,
      );
      if (!row) return null;
      const target = row.matches(FOCUSABLE)
        ? row
        : row.querySelector<HTMLElement>(FOCUSABLE);
      if (!target || target === base) return null;
      if (target.hasAttribute('disabled') || target.getAttribute('aria-disabled') === 'true') return null;
      if (target.closest('[data-gp-ignore]')) return null;
      const style = getComputedStyle(target);
      const rect = target.getBoundingClientRect();
      if (style.display === 'none' || style.visibility === 'hidden' || rect.width <= 0 || rect.height <= 0) return null;
      return target;
    };
    // 专属配置展开后，标题本身会主动从焦点列表移除，避免标题和第一
    // 个 AC 下拉重叠。因此跨气泡导航必须把目标改成展开内容的第一个
    // 可用控件，而不是硬跳到被 data-gp-ignore 排除的标题。
    const customBodyTarget = customBody?.querySelector<HTMLElement>(
      // Dropdown 的外层只是事件容器，不能接收 DOM focus；必须命中真正
      // 可聚焦的 trigger button。此前命中外层 div 后 focus 失败，因此
      // 手柄看起来会“穿过”专属配置。
      'button:not(:disabled):not([data-gp-ignore]), [tabindex]:not([tabindex="-"]):not([data-gp-ignore]), select:not([disabled]), input:not([disabled])',
    ) || null;
    const customTarget = customExpanded ? customBodyTarget : customEntry;
    const customTargetAvailable = !!customTarget &&
      !customTarget.matches('[data-gp-ignore]') &&
      customTarget.getAttribute('aria-disabled') !== 'true';
    const insideRules = !!base.closest('[data-gp-game-rules-body]');
    // 黑/白名单入口与其二级气泡都属于同一条菜单路径：按下时明确
    // 跳到专属配置入口/内容，不能让空间距离算法把焦点穿到页脚或其它控件。
    if (dy > 0 && customTarget && customTargetAvailable &&
      (base === rulesEntry || insideRules)) {
      best = customTarget;
    }
    // 从专属配置内部按上，永远直接返回“游戏黑 / 白名单”这一排；
    // 不再经过专属配置标题或第一条 AC 气泡，避免焦点重叠。
    if (dy < 0 && rulesEntry && navEls.includes(rulesEntry) &&
      (isInsideCustomPanel(base) || base === customEntry)) {
      best = rulesEntry;
    }

    // 顶部游戏菜单是固定的菜单序列，不让浮动布局/动画参与“下一个气泡”
    // 的判断。尤其 FSR4.1 / 游戏加速按下必须先到黑白名单入口，再到专属配置。
    if (!best && activeGameMenu) {
      const currentRow = gameRowKey(base);
      const targetRow = dy > 0
        ? ({
            controls: 'actions-1',
            'actions-1': 'actions-2',
            'actions-2': 'rules-entry',
            'rules-entry': 'custom-entry',
            'custom-entry': 'footer',
          } as Record<string, string>)[currentRow]
        : ({
            footer: 'custom-entry',
            'custom-entry': 'rules-entry',
            'rules-entry': 'actions-2',
            'actions-2': 'actions-1',
            'actions-1': 'controls',
          } as Record<string, string>)[currentRow];
      best = directGameMenuTarget(targetRow) || nearestGameRow(targetRow);
    }
    // ── 上下导航：行优先 ──
    // 用户希望方向键按“内容行”移动。例如从“帧数目标 30”按上，应跳到
    // 同一卡片内上一行的“使用电池”下拉，而不是跳到更靠左的“野蛮系统电源”。
    const ROW_BAND = 28; // 同一行内 center-y 容差（px）
    type Cand = { el: HTMLElement; y: number; x: number; dyAbs: number };
    const cands: Cand[] = [];
    for (const el of navEls) {
      if (el === base) continue;
      const r = el.getBoundingClientRect();
      const ey = r.top + r.height / 2;
      const ddy = ey - by;
      if (dy > 0 && ddy <= 0) continue;
      if (dy < 0 && ddy >= 0) continue;
      cands.push({ el, y: ey, x: r.left + r.width / 2, dyAbs: Math.abs(ddy) });
    }
    if (cands.length === 0) {
      if (!best) return;
    }
    if (best) {
      // 已命中专属配置 → 黑 / 白名单的专用返回路径，不再让空间导航覆盖它。
    } else {
    // 先找垂直最近的元素，再用它的 y 定义“目标行”
    cands.sort((a, b) => a.dyAbs - b.dyAbs);
    const targetY = cands[0].y;
    const rowCands = cands.filter((c) => Math.abs(c.y - targetY) <= ROW_BAND);
    // 在目标行里选 x 最近的，保持水平落点一致
    let bestXDist = Infinity;
    for (const c of rowCands) {
      const xDist = Math.abs(c.x - bx);
      if (xDist < bestXDist) {
        bestXDist = xDist;
        best = c.el;
      }
    }
    }
  } else {
    // 左右导航严格锁定当前视觉行。游戏菜单的控件高度、过渡动画和
    // 2x2 快捷功能网格不能再交给全页空间算法猜测，否则会从左右串到
    // 上下行。data-gp-game-row 是页面声明的视觉行边界；其它页面继续
    // 使用下面的矩形重叠兜底规则。
    const gameMenu = activeGameMenu;
    const baseGameRow = gameMenu?.contains(base)
      ? base.closest<HTMLElement>('[data-gp-game-row]')
      : null;
    const baseGameRowKey = baseGameRow?.dataset.gpGameRow || '';
    const sameRow = baseGameRowKey
      ? navEls
        // Some rows mark the individual button (FSR), while others mark the
        // wrapping container (游戏加速). Compare row keys, never DOM identity.
        .filter((el) => el !== base && el.closest<HTMLElement>('[data-gp-game-row]')?.dataset.gpGameRow === baseGameRowKey)
        .map((el) => {
          const r = el.getBoundingClientRect();
          return { el, r, x: r.left + r.width / 2, y: r.top + r.height / 2 };
        })
        .filter((box) => dx > 0 ? box.x > bx : box.x < bx)
      : boxes.filter((box) => {
        const distance = box.x - bx;
        const inDirection = dx > 0 ? distance > 0 : distance < 0;
        const overlapsVertically = Math.min(br.bottom, box.r.bottom) > Math.max(br.top, box.r.top);
        return inDirection && overlapsVertically;
      });
    if (sameRow.length === 0) return;

    // 同一行内只按横向距离选择，垂直误差仅用于稳定相同距离时的结果。
    sameRow.sort((a, b) => {
      const xDistance = Math.abs(a.x - bx) - Math.abs(b.x - bx);
      if (xDistance !== 0) return xDistance;
      return Math.abs(a.y - by) - Math.abs(b.y - by);
    });
    best = sameRow[0].el;
  }

  if (best) {
    try {
      if (!focusGamepadElement(best, dy < 0)) {
        best.focus({ preventScroll: false });
        setGamepadFocused(best);
      }
    } catch {
      // Ignore transient DOM changes during route transitions.
    }
  }
}

function isRangeInput(el: HTMLElement | null): el is HTMLInputElement {
  return el !== null && el.tagName === 'INPUT' && (el as HTMLInputElement).type === 'range';
}

// ── 激活当前焦点 ──
function activate(opts: GamepadEngineOptions) {
  const el = document.activeElement as HTMLElement | null;
  if (!el) return;
  // Vue 的离场动画会短暂保留已经折叠的二级菜单 DOM。即使组件已经
  // 把焦点恢复到入口，WebView2 仍可能在同一帧报告旧 activeElement。
  // A 只能激活“当前焦点列表”中的节点，禁止程序化 click 穿透到已折叠
  // 的黑白名单按钮或专属配置 AC/DC 下拉。
  if (!focusables().includes(el)) {
    const gameMenu = document.querySelector<HTMLElement>('[data-gp-game-quick-menu]');
    const recovery = el.closest('[data-gp-custom-body]')
      ? gameMenu?.querySelector<HTMLElement>('[data-gp-custom-entry]')
      : el.closest('[data-gp-game-rules-body]')
        ? gameMenu?.querySelector<HTMLElement>('[data-gp-game-rules-entry]')
        : null;
    if (!recovery || !focusGamepadElement(recovery)) focusFirst();
    return;
  }
  // 滑块：A 进入/退出编辑模式
  if (isRangeInput(el)) {
    if (sliderEditMode) {
      commitSlider();
      sliderEditMode = false;
      log(opts, 'A · 应用并退出滑块编辑');
    } else {
      sliderEditMode = true;
      log(opts, 'A · 进入滑块编辑模式');
    }
    return;
  }
  // 自定义下拉 → 打开菜单（与鼠标点击一致；不再顺序轮转档位）
  if (el.dataset && el.dataset.gpDropdown !== undefined) {
    el.dispatchEvent(new CustomEvent('gp:dropdown-open', { bubbles: true }));
    return;
  }
  // 原生下拉框 → 循环 option
  if (el.tagName === 'SELECT') {
    const sel = el as HTMLSelectElement;
    const next = (sel.selectedIndex + 1) % sel.options.length;
    sel.selectedIndex = next;
    sel.dispatchEvent(new Event('change', { bubbles: true }));
    return;
  }
  // 其他可点击元素 → click
  if (typeof (el as any).click === 'function') {
    (el as any).click();
  }
}

// ── 滑块微调 ──
function adjustSlider(delta: number) {
  const el = document.activeElement as HTMLElement | null;
  if (el && el.tagName === 'INPUT' && (el as HTMLInputElement).type === 'range') {
    const inp = el as HTMLInputElement;
    const step = Number(inp.step) || 1;
    const max = Number(inp.max);
    const min = Number(inp.min);
    let v = Number(inp.value) + delta * step;
    v = Math.max(min, Math.min(max, v));
    inp.value = String(v);
    inp.dispatchEvent(new Event('input', { bubbles: true }));
    // 不立即 dispatch change；按 A 应用并退出滑块编辑模式。
  }
}

function commitSlider() {
  const el = document.activeElement as HTMLElement | null;
  if (el && el.tagName === 'INPUT' && (el as HTMLInputElement).type === 'range') {
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
}

// ── 滑块微调（线性加速）：按住越久，单步跨度越大、触发越密 ──
// 仅当焦点在滑块（engine 调用前已判定）时由调用方传入左右/LT/RT 状态。
function microAdjustSlider(now: number, heldL: boolean, heldR: boolean, lt: boolean, rt: boolean) {
  if (!(heldL || heldR || lt || rt)) { sliderAccelStart = 0; sliderLastApply = 0; return; }
  const dir = heldR || rt ? 1 : -1;
  if (sliderAccelStart === 0) sliderAccelStart = now;
  const held = now - sliderAccelStart;
  let step = 1;
  let interval = SLIDER_REPEAT_BASE;
  if (held > SLIDER_ACCEL_DELAY) {
    step = Math.min(1 + Math.floor((held - SLIDER_ACCEL_DELAY) / SLIDER_ACCEL_STEP), SLIDER_ACCEL_CAP);
    interval = Math.max(SLIDER_REPEAT_MIN, SLIDER_REPEAT_BASE - Math.floor(held / 400) * 15);
  }
  if (now - sliderLastApply >= interval) {
    adjustSlider(dir * step);
    sliderLastApply = now;
  }
}

// These two actions are global controller commands. They must remain usable
// even while simulated-mouse mode intentionally suppresses page controls.
function handleGamepadBack(opts: GamepadEngineOptions): void {
  const backEvent = new CustomEvent('ipc:gamepad-back', { cancelable: true });
  window.dispatchEvent(backEvent);
  if (backEvent.defaultPrevented) {
    log(opts, 'B · 当前页面处理');
  } else if (sliderEditMode) {
    sliderEditMode = false;
    log(opts, 'B · 退出滑块编辑');
  } else {
    setGamepadFocused(null);
    const active = document.activeElement as HTMLElement | null;
    active?.blur?.();
    log(opts, 'B · 返回');
  }
}

function handleGamepadEditGame(opts: GamepadEngineOptions): void {
  const editGameEvent = new CustomEvent('ipc:gamepad-edit-game', { cancelable: true });
  window.dispatchEvent(editGameEvent);
  if (editGameEvent.defaultPrevented) {
    log(opts, 'Y · 编辑游戏识别名单');
  } else {
    log(opts, 'Y · 游戏识别菜单不可用');
  }
}

function dispatchNativeUiAction(opts: GamepadEngineOptions, action: NativeUiAction): void {
  if (action === 'page-prev') {
    navigate(opts, -1);
    setGamepadFocused(null);
    return;
  }
  if (action === 'page-next') {
    navigate(opts, 1);
    setGamepadFocused(null);
    return;
  }
  if (action === 'back') {
    handleGamepadBack(opts);
    return;
  }
  if (action === 'edit-game') {
    handleGamepadEditGame(opts);
    return;
  }
  const gameMenuOpen = !!document.querySelector('[data-gp-game-quick-menu]');
  const gameQuickDialogOpen = !!document.querySelector('[data-gp-game-quick-dialog]');
  if (mouseModeSuppress && !gameMenuOpen) return;
  if (action === 'debug') {
    window.dispatchEvent(new CustomEvent('ipc:gamepad-start'));
    log(opts, 'Start -> debug');
    return;
  }
  if (action === 'confirm') {
    activate(opts);
    log(opts, 'A -> confirm');
    return;
  }
  if (action === 'dropdown') {
    if (gameQuickDialogOpen) return;
    const el = document.activeElement as HTMLElement | null;
    if (el?.dataset?.gpDropdown !== undefined) {
      el.dispatchEvent(new CustomEvent('gp:dropdown-open', { bubbles: true }));
    } else if (el?.tagName === 'SELECT') {
      activate(opts);
    }
    log(opts, 'X -> dropdown');
    return;
  }

  if (action === 'slider-decrease' || action === 'slider-increase') {
    if (sliderEditMode && isRangeInput(document.activeElement as HTMLElement)) {
      adjustSlider(action === 'slider-increase' ? 1 : -1);
    }
    return;
  }

  const dx = action === 'nav-right' ? 1 : action === 'nav-left' ? -1 : 0;
  const dy = action === 'nav-down' ? 1 : action === 'nav-up' ? -1 : 0;
  if (sliderEditMode && isRangeInput(document.activeElement as HTMLElement)) {
    if (dx !== 0) adjustSlider(dx);
    else if (dy !== 0) sliderEditMode = false;
    return;
  }
  const ddTrigger = document.querySelector<HTMLElement>('[aria-expanded="true"][aria-haspopup="listbox"]');
  if (ddTrigger) {
    if (dy !== 0) {
      ddTrigger.dispatchEvent(new CustomEvent('gp:dropdown-nav', { bubbles: true, detail: { dir: dy } }));
    }
    return;
  }
  if (dx !== 0) moveFocus(dx, 0);
  else if (dy !== 0) moveFocus(0, dy);
}

// ── 主循环 ──
function tick(opts: GamepadEngineOptions) {
  const now = performance.now();
  const pad = getPad();
  // 窗口隐藏（在托盘）时不处理手柄输入：避免后台空转，也避免“非活动窗口仍监听”
  // 呼出期间 (summonActive) 强制视为可见，绕开 WebView2 可见态未及时刷新的窗口
  if (!summonActive && !browserLoopAllowed()) {
    // A visibility event normally cancels the frame first. Keep this guard as
    // a race-safe backstop so a callback already queued before hiding cannot
    // recreate a permanent background RAF loop.
    stopGamepadLoop();
    return;
  }
  if (pad) {
    noPadStreak = 0;
    if (!gamepadConnected) {
      gamepadConnected = true;
      log(opts, `手柄已连接: ${pad.id || 'unknown'}`);
    }
    // 唤醒后读数为冻结快照：若 timestamp 长时间停滞，重置边沿状态，
    // 待浏览器恢复轮询后下一次真实输入即被识别为新边沿（避免卡死在"无任何输入"状态）
    if (pad.timestamp !== lastPadTs) {
      lastPadTs = pad.timestamp;
      stuckSince = 0;
    } else if (stuckSince === 0) {
      stuckSince = now;
    } else if (now - stuckSince > 2000) {
      prevButtons = [];
      prevAxes = [];
      stuckSince = now;
    }
    // Use a fresh snapshot per frame, matching the stable legacy engine.
    const b = Array.from(pad.buttons, (button) => button.pressed);
    const a = Array.from(pad.axes);

    // 测试模式：B 按住 3 秒退出；期间不向程序页面派发任何其它手柄操作。
    if (testMode) {
      if (b[1]) {
        if (testBHoldStart === 0) testBHoldStart = now;
        if (now - testBHoldStart >= TEST_B_HOLD_MS) {
          testBHoldStart = 0;
          testMode = false;
          window.dispatchEvent(new CustomEvent('ipc:gamepad.testmode', { detail: false }));
          log(opts, 'B · 按住 3 秒退出手柄检测');
        }
      } else {
        testBHoldStart = 0;
      }
      prevButtons = b;
      prevAxes = a;
      rafId = requestAnimationFrame(() => tick(opts));
      return;
    }

    // 边沿检测：本次按下且上次未按下
    const pressed = (i: number) => b[i] && !prevButtons[i];
    // 方向输入（D-pad 数字键 + 左摇杆模拟）
    const heldLeft = b[14] || a[0] < -0.5;
    const heldRight = b[15] || a[0] > 0.5;
    const heldUp = b[12] || a[1] < -0.5;
    const heldDown = b[13] || a[1] > 0.5;

    // Native Raw Input is the primary path after it has delivered one UI
    // action. Keep the browser API as a fallback for non-XInput devices.
    if (nativeUiInputActive) {
      prevButtons = b;
      prevAxes = a;
      rafId = requestAnimationFrame(() => tick(opts));
      return;
    }

    // ── LB/RB (L1/R1) 切页（挂起一帧裁决，防 LB+RB 呼出组合误切页）──
    // 呼出后两键未全释放前，抑制 LB/RB 切页：呼出动作本身不产生页面移动。
    const lbDown = b[4];
    const rbDown = b[5];
    // After the summon combo is released, controllers can report a brief
    // shoulder bounce as a fresh one-button edge. Suppress that burst before
    // allowing ordinary L1/R1 page navigation again.
    if (summonSuppress && !lbDown && !rbDown) {
      summonSuppress = false;
      shoulderReleaseLockUntil = now + SUMMON_POST_RELEASE_LOCKOUT_MS;
      lbNavPending = false;
      rbNavPending = false;
    }
    const suppressPage = summonSuppress || now < shoulderReleaseLockUntil;
    if (suppressPage) {
      lbNavPending = false;
      rbNavPending = false;
    }
    // 裁决上一帧挂起的切页：另一键也已按下（呼出组合）→ 取消；否则执行切页。
    if (lbNavPending) {
      if (rbDown || suppressPage) lbNavPending = false; // 组合/呼出中：丢弃
      else if (now - lastPageSwitch > PAGE_SWITCH_COOLDOWN) { navigate(opts, -1); lastPageSwitch = now; setGamepadFocused(null); lbNavPending = false; } // LB → 上一页
      else lbNavPending = false; // 冷却中：丢弃
    }
    if (rbNavPending) {
      if (lbDown || suppressPage) rbNavPending = false; // 组合/呼出中：丢弃
      else if (now - lastPageSwitch > PAGE_SWITCH_COOLDOWN) { navigate(opts, 1); lastPageSwitch = now; setGamepadFocused(null); rbNavPending = false; } // RB → 下一页
      else rbNavPending = false; // 冷却中：丢弃
    }
    // 新按下边沿：先挂起待裁决（若另一键已按下则视为组合，直接不切页）
    if (pressed(4) && !rbDown && !lbNavPending && !suppressPage) lbNavPending = true;
    if (pressed(5) && !lbDown && !rbNavPending && !suppressPage) rbNavPending = true;

    // ── 模拟鼠标开启：屏蔽内页按键选择/功能 ──
    // LR、B、Y are deliberately handled outside this suppression boundary:
    // LR changes pages, B exits/backs out, and Y edits the current game rule.
    if (mouseModeSuppress) {
      const gameMenuOpen = !!document.querySelector('[data-gp-game-quick-menu]');
      if (gameMenuOpen) {
        if (pressed(0)) enqueueGamepadTaskDetached(() => activate(opts));
      }
      if (pressed(1)) enqueueGamepadTaskDetached(() => handleGamepadBack(opts));
      if (pressed(3)) enqueueGamepadTaskDetached(() => handleGamepadEditGame(opts));
      prevButtons = b;
      prevAxes = a;
      rafId = requestAnimationFrame(() => tick(opts));
      return;
    }

    // ── Start → 调试面板（释放时才触发；若本轮被用作修饰键则屏蔽，避免和快捷调节冲突）──
    if (pressed(9)) {
      startUsedAsModifier = false;
    }
    if (!b[9] && prevButtons[9] && !startUsedAsModifier) {
      window.dispatchEvent(new CustomEvent('ipc:gamepad-start'));
      log(opts, 'Start · 调试面板');
    }

    // ── 面包按钮 ──
    if (pressed(0)) {
      enqueueGamepadTaskDetached(() => {
        activate(opts);
        log(opts, 'A · 确认');
      });
    }
    if (pressed(1)) enqueueGamepadTaskDetached(() => handleGamepadBack(opts));
    if (pressed(3)) enqueueGamepadTaskDetached(() => handleGamepadEditGame(opts));
    if (pressed(2)) {
      const el = document.activeElement as HTMLElement | null;
      if (el?.dataset?.gpDropdown !== undefined) {
        enqueueGamepadTaskDetached(() => {
          const current = document.activeElement as HTMLElement | null;
          if (current?.dataset?.gpDropdown !== undefined) {
            current.dispatchEvent(new CustomEvent('gp:dropdown-open', { bubbles: true }));
          }
        });
      } else if (el?.tagName === 'SELECT') {
        enqueueGamepadTaskDetached(() => activate(opts));
      }
      log(opts, 'X · 下拉');
    }

    // ── Start 键作为修饰键：Start + 方向键快捷调节 ──
    // ⚠️ 实际调节逻辑已移到 native Raw Input 回调（仅在有输入时被唤醒），
    //    这样在游戏内全屏、窗口未聚焦时也能生效且零延迟。前端只负责：
    //    ① 按住期间屏蔽方向键的焦点移动/滑块微调；② 抑制调试面板（startUsedAsModifier）。
    const startHeld = b[9];
    if (startHeld) {
      startUsedAsModifier = gpSettings.tdpShortcut || gpSettings.fpsShortcut;
      lastNav = now;
    } else if (sliderEditMode && isRangeInput(document.activeElement as HTMLElement)) {
      // 滑块编辑模式：左右调整数值（线性加速），上下/B退出（B 在上方处理）
      microAdjustSlider(now, heldLeft, heldRight, b[6], b[7]);
      if (pressed(12) || pressed(13)) {
        sliderEditMode = false;
        log(opts, '方向键 · 退出滑块编辑');
      }
      lastNav = now;
      } else {
        // 非编辑模式：方向键一律用于页面内焦点移动——即使焦点停在滑块上，也只移动焦点，
        // 不再微调数值。滑块只有按 A（确认/点击）进入“编辑模式”后，方向键/LT/RT 才调整数值
        // （见上方 sliderEditMode 分支）。这修复了“焦点一上滑块、左右就被吃掉、无法选后续元素”。
        const dirX = heldRight ? 1 : heldLeft ? -1 : 0;
        const dirY = heldDown ? 1 : heldUp ? -1 : 0;
        if ((dirX !== 0 || dirY !== 0) && now - lastNav > 220) {
          // ── 下拉菜单打开（teleport 到 body）时：只上下在菜单项内移动高亮，
          //    焦点严格限制在菜单内，不穿透到下层页面（修复手柄上下选出菜单外内容）；
          //    左右方向键不做事（与鼠标菜单一致，不再循环档位）──
          const ddTrigger = document.querySelector<HTMLElement>(
            '[aria-expanded="true"][aria-haspopup="listbox"]'
          );
          if (ddTrigger) {
            if (dirY !== 0) {
              // 上下：驱动 Dropdown 自己的 highlight（视觉与焦点一致），不跳出菜单
              ddTrigger.dispatchEvent(
                new CustomEvent('gp:dropdown-nav', { bubbles: true, detail: { dir: dirY } })
              );
            }
            // dirX：菜单打开时忽略左右（保持与鼠标点击菜单一致的选择体验）
          } else if (dirX !== 0 && dirY === 0) enqueueGamepadTaskDetached(() => moveFocus(dirX, 0));
          else if (dirY !== 0 && dirX === 0) enqueueGamepadTaskDetached(() => moveFocus(0, dirY));
          else if (dirX !== 0 && dirY !== 0) {
            // 对角线优先水平
            enqueueGamepadTaskDetached(() => moveFocus(dirX, 0));
          }
          lastNav = now;
        }
      }

    prevButtons = b;
    prevAxes = a;
  } else {
    // 无手柄：连续若干帧后停止循环（Grace 防瞬时断连误杀），省 CPU
    noPadStreak++;
    if (noPadStreak >= NO_PAD_GRACE) {
      rafId = 0; // 标记停止；本帧不再重排，待连接/可见/唤醒事件重启
      return;
    }
  }
  rafId = requestAnimationFrame(() => tick(opts));
}

export function startGamepad(opts: GamepadEngineOptions): () => void {
  engineOpts = opts;
  void refreshVisibleRoutes();
  if (rafId) return () => stopGamepad();
  prevButtons = [];
  prevAxes = [];
  const onMouseMode = (e: Event) => {
    mouseModeSuppress = Boolean((e as CustomEvent<{ on?: boolean }>).detail?.on);
  };
  window.addEventListener('gp:mouse-mode', onMouseMode);
  mouseModeCleanup = () => {
    window.removeEventListener('gp:mouse-mode', onMouseMode);
  };
  summonGet().then((s) => { gpSettings = { ...gpSettings, ...s }; });
  // 仅检测到手柄时才启动 60fps 循环；无手柄保持停止，由连接/可见/唤醒事件拉起（省 CPU）
  if (hasConnectedPad() && browserLoopAllowed()) {
    log(opts, '手柄引擎已启动（检测到手柄）');
    ensureLoop(opts);
  } else {
    log(opts, '手柄引擎就绪（无手柄，待连接）');
  }
  return () => stopGamepad();
}
let mouseModeCleanup: (() => void) | null = null;
function stopGamepadLoop() {
  if (rafId) cancelAnimationFrame(rafId);
  rafId = 0;
}
export function stopGamepad() {
  if (mouseModeCleanup) { mouseModeCleanup(); mouseModeCleanup = null; }
  cancelSummonFocus();
  stopGamepadLoop();
}
