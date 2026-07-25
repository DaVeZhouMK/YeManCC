// gamepad/engine.ts — 手柄全量导航引擎（对齐 PLAN §五）
//
// 轮询 navigator.getGamepads()（60Hz rAF），边沿检测按键跳变，
// 通过 DOM 几何空间导航 + 统一的 action 分发，实现纯手柄操作系统。
// 无手柄时引擎静默，不影响纯鼠标用户。
//
// 按键映射（Xbox 360 / XInput STANDARD GAMEPAD）：
//   LB(4)/RB(5)    → 切换上/下一页
//   A(0)           → 确认/点击/切换
//   B(1)           → 取消/blur
//   Y(3)           → 应用滑块值（commit）
//   X(2)           → 下拉选择循环
//   Start(9)       → 调试面板
//   D-pad/左摇杆   → 页面内焦点移动（空间导航）
//   LT(6)/RT(7)/←→ → 滑块微调
import { ROUTES } from '@/router';
import type { Router } from 'vue-router';
import { summonGet, type GamepadSettings } from '@/bridge/yeman';

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
let holdStart = 0;
let lastNav = 0;
let lastPageSwitch = 0; // 切页冷却（防止连按卡顿）
const PAGE_SWITCH_COOLDOWN = 400; // ms，防止快速连击导致多页跳转+渲染堆积
let gamepadConnected = false; // 追踪手柄连接状态
let lastPadTs = 0;   // 上一帧手柄读数时间戳（用于检测唤醒后冻结快照）
let stuckSince = 0;  // 读数停滞起始时间
let sliderEditMode = false; // 滑块编辑模式：A 进入后，左右键调整数值，B/上下退出
let testMode = false;       // 手柄测试模式：仅显示/检测输入，不操作程序 UI
let gpSettings: GamepadSettings = {
  enabled: true,
  bDoubleMinimize: true,
  tdpShortcut: false,
  fpsShortcut: false,
};
let startUsedAsModifier = false; // Start 键是否在本轮按住中被用作快捷调节修饰键

// 监听手柄连接/断开事件（某些 WebView2 版本需要此触发轮询）
if (typeof window !== 'undefined') {
  window.addEventListener('gamepadconnected', () => { gamepadConnected = true; });
  window.addEventListener('gamepaddisconnected', () => { gamepadConnected = false; });
  // 睡眠守护唤醒后，native 通知重启手柄引擎：重置边沿/时间戳状态，使下一次真实输入被当作新边沿
  window.addEventListener('ipc:gamepad.restart', () => { restartGamepadState(); });
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
}

function log(opts: GamepadEngineOptions, msg: string) {
  opts.onAction?.(msg);
}

function getPad(): Gamepad | null {
  try {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) if (p && p.connected) return p;
  } catch {
    /* 某些 WebView2 版本 getGamepads() 可能抛异常 */
  }
  return null;
}

// ── 页面切换 ──
function navigate(opts: GamepadEngineOptions, dir: -1 | 1) {
  const order = ROUTES.map((r) => r.path);
  const cur = opts.router.currentRoute.value.path;
  let idx = order.indexOf(cur);
  if (idx < 0) idx = 0;
  idx = (idx + dir + order.length) % order.length;
  opts.router.push(order[idx]);
  log(opts, `切页 → ${ROUTES[idx].title}`);
}

// ── 页面内焦点列表 ──
function focusables(): HTMLElement[] {
  return Array.from(document.querySelectorAll<HTMLElement>(FOCUSABLE)).filter(
    (el) => {
      if (el.hasAttribute('disabled')) return false;
      const r = el.getBoundingClientRect();
      // 必须可见且有实际尺寸
      return r.width > 0 && r.height > 0;
    }
  );
}

// ── 可见焦点高亮（手柄/键盘）：显式切换 .focused，
//    因为 Chromium 对程序化 .focus() 不触发 :focus-visible，
//    而 tokens.css 的 .focused 提供 accent 发光环 ──
function setFocused(el: HTMLElement | null) {
  document.querySelectorAll('.focused').forEach((n) => n.classList.remove('focused'));
  if (el) el.classList.add('focused');
}

// ── 空间方向导航（对齐 HTA 的方向键焦点切换） ──
function moveFocus(dx: number, dy: number) {
  const els = focusables();
  if (els.length === 0) return;
  const cur = document.activeElement as HTMLElement | null;
  // 当前焦点元素必须在列表中，否则从第一个开始
  const baseIdx = cur ? els.indexOf(cur) : -1;
  const base = baseIdx >= 0 ? els[baseIdx] : els[0];
  const br = base.getBoundingClientRect();
  const bx = br.left + br.width / 2;
  const by = br.top + br.height / 2;
  let best: HTMLElement | null = null;
  let bestScore = Infinity;
  for (const el of els) {
    if (el === base) continue;
    const r = el.getBoundingClientRect();
    const ex = r.left + r.width / 2;
    const ey = r.top + r.height / 2;
    const ddx = ex - bx;
    const ddy = ey - by;
    // 投影必须为正（朝目标方向移动）
    const proj = ddx * dx + ddy * dy;
    if (proj <= 0) continue;
    // 垂直偏离惩罚
    const perp = Math.abs(ddx * dy - ddy * dx);
    // 综合得分：投影距离 + 垂直偏离 × 2
    const score = proj + perp * 2;
    if (score < bestScore) {
      bestScore = score;
      best = el;
    }
  }
  if (best) {
    best.focus({ preventScroll: false });
    best.scrollIntoView?.({ block: 'nearest', behavior: 'smooth' });
    setFocused(best);
  }
}

function isRangeInput(el: HTMLElement | null): el is HTMLInputElement {
  return el !== null && el.tagName === 'INPUT' && (el as HTMLInputElement).type === 'range';
}

// ── 激活当前焦点 ──
function activate(opts: GamepadEngineOptions) {
  const el = document.activeElement as HTMLElement | null;
  if (!el) return;
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
  // 自定义下拉 → 循环选项（对齐原 SELECT 行为：X/A 都切换并应用）
  if (el.dataset && el.dataset.gpDropdown !== undefined) {
    el.dispatchEvent(new CustomEvent('gp:dropdown-cycle', { bubbles: true }));
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
    // 不立即 dispatch change（等 Y 键确认才 commit）
  }
}

function commitSlider() {
  const el = document.activeElement as HTMLElement | null;
  if (el && el.tagName === 'INPUT' && (el as HTMLInputElement).type === 'range') {
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
}

// ── 主循环 ──
function tick(opts: GamepadEngineOptions) {
  const now = performance.now();
  const pad = getPad();
  // 窗口隐藏（在托盘）时不处理手柄输入：避免后台空转，也避免“非活动窗口仍监听”
  if (document.visibilityState !== 'visible') {
    if (pad) { prevButtons = pad.buttons.map((x) => x.pressed); prevAxes = Array.from(pad.axes); }
    rafId = requestAnimationFrame(() => tick(opts));
    return;
  }
  if (pad) {
    if (!gamepadConnected) {
      gamepadConnected = true;
      log(opts, `🎮 手柄已连接: ${pad.id || 'unknown'}`);
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
    const b = pad.buttons.map((x) => x.pressed);
    const a = Array.from(pad.axes);

    // 测试模式：仅保留状态轮询，不执行任何 UI 操作
    if (testMode) {
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

    // ── LB/RB (L1/R1) 切页（带冷却防连击卡顿）──
    if (pressed(4) && now - lastPageSwitch > PAGE_SWITCH_COOLDOWN) { navigate(opts, -1); lastPageSwitch = now; setFocused(null); } // LB → 上一页
    if (pressed(5) && now - lastPageSwitch > PAGE_SWITCH_COOLDOWN) { navigate(opts, 1); lastPageSwitch = now; setFocused(null); }  // RB → 下一页

    // ── Start → 调试面板（释放时才触发；若本轮被用作修饰键则屏蔽，避免和快捷调节冲突）──
    if (pressed(9)) {
      startUsedAsModifier = false;
    }
    if (!b[9] && prevButtons[9] && !startUsedAsModifier) {
      window.dispatchEvent(new CustomEvent('ipc:gamepad-start'));
      log(opts, 'Start · 调试面板');
    }

    // ── 面包按钮 ──
    if (pressed(0)) { activate(opts); log(opts, 'A · 确认'); }
    if (pressed(1)) {
      // 单按 B = 返回/失焦（仅窗口聚焦、Web Gamepad 可读时生效）。
      // 双击 B 最小化到托盘已移到 native 后台 XInput 线程（gamepadSummonThread），
      // 全局免点击；此处不再处理，避免与 native 双触发。
      if (sliderEditMode) {
        sliderEditMode = false;
        log(opts, 'B · 退出滑块编辑');
      } else {
        const a = document.activeElement as HTMLElement | null;
        a?.classList.remove('focused');
        a?.blur?.();
        log(opts, 'B · 返回');
      }
    }
    if (pressed(3)) {
      commitSlider();
      if (sliderEditMode) {
        sliderEditMode = false;
        log(opts, 'Y · 应用并退出滑块编辑');
      } else {
        log(opts, 'Y · 应用滑块');
      }
    }
    if (pressed(2)) {
      const el = document.activeElement as HTMLElement | null;
      if (el?.dataset?.gpDropdown !== undefined) {
        el.dispatchEvent(new CustomEvent('gp:dropdown-cycle', { bubbles: true }));
      } else if (el?.tagName === 'SELECT') {
        activate(opts);
      }
      log(opts, 'X · 下拉');
    }

    // ── Start 键作为修饰键：Start + 方向键快捷调节 ──
    // ⚠️ 实际调节逻辑已移到 native gamepadSummonThread（XInput 后台线程），
    //    这样在游戏内全屏、窗口未聚焦时也能生效且零延迟。前端只负责：
    //    ① 按住期间屏蔽方向键的焦点移动/滑块微调；② 抑制调试面板（startUsedAsModifier）。
    const startHeld = b[9];
    if (startHeld) {
      startUsedAsModifier = gpSettings.tdpShortcut || gpSettings.fpsShortcut;
      lastNav = now;
    } else if (sliderEditMode && isRangeInput(document.activeElement as HTMLElement)) {
      // 滑块编辑模式：左右调整数值，上下/B退出（B 在上方处理）
      if (pressed(14) || pressed(15) || pressed(6) || pressed(7)) {
        const d = (pressed(6) || pressed(15)) ? 1 : -1;
        if (d !== 0) { adjustSlider(d); holdStart = now; }
      }
      if ((heldLeft || heldRight || b[6] || b[7]) && !(heldUp || heldDown) && now - holdStart > 200) {
        adjustSlider(heldRight || b[6] ? 1 : -1);
        holdStart = now;
      }
      if (pressed(12) || pressed(13)) {
        sliderEditMode = false;
        log(opts, '方向键 · 退出滑块编辑');
      }
      lastNav = now;
    } else {
      // ── D-pad / 左摇杆 → 焦点移动（220ms 冷却防连跳）──
      const dirX = heldRight ? 1 : heldLeft ? -1 : 0;
      const dirY = heldDown ? 1 : heldUp ? -1 : 0;
      if ((dirX !== 0 || dirY !== 0) && now - lastNav > 220) {
        if (dirX !== 0 && dirY === 0) moveFocus(dirX, 0);
        else if (dirY !== 0 && dirX === 0) moveFocus(0, dirY);
        else if (dirX !== 0 && dirY !== 0) {
          // 对角线优先水平
          moveFocus(dirX, 0);
        }
        lastNav = now;
      }

      // ── 滑块微调：LT/RT(±1 step) 或 ←→(±1 step) ──
      if (pressed(14) || pressed(15) || pressed(6) || pressed(7)) {
        const d = (pressed(6) || pressed(15)) ? 1 : (pressed(7) || pressed(14)) ? -1 : 0;
        if (d !== 0) { adjustSlider(d); holdStart = now; }
      }
      // 按住 ←→ 或 LT/RT 重复微调（200ms 间隔）
      if ((heldLeft || heldRight || b[6] || b[7]) && !(heldUp || heldDown) && now - holdStart > 200) {
        adjustSlider(heldRight || b[6] ? 1 : -1);
        holdStart = now;
      }
    }

    prevButtons = b;
    prevAxes = a;
  }
  rafId = requestAnimationFrame(() => tick(opts));
}

export function startGamepad(opts: GamepadEngineOptions): () => void {
  if (rafId) return () => stopGamepad();
  prevButtons = [];
  prevAxes = [];
  summonGet().then((s) => { gpSettings = { ...gpSettings, ...s }; });
  log(opts, '手柄引擎已启动');
  rafId = requestAnimationFrame(() => tick(opts));
  return () => stopGamepad();
}
export function stopGamepad() {
  if (rafId) cancelAnimationFrame(rafId);
  rafId = 0;
}
