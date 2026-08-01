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
//   LT(6)/RT(7)/←→ → 滑块微调（仅 A 进入“编辑模式”后；未进入时方向键只移动焦点）
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
let lastNav = 0;
let lastPageSwitch = 0; // 切页冷却（防止连按卡顿）
const PAGE_SWITCH_COOLDOWN = 100; // ms，原 400ms 过严致连按 LB/RB 丢按；降到 100ms 使快速切页（含切到 TDP）每次都生效
// 滑块线性加速：按住越久步长越大、间隔越短
let sliderAccelStart = 0;  // 本次按住起始时间（用于线性加速）
let sliderLastApply = 0;   // 上次应用加速步进的时间
const SLIDER_ACCEL_DELAY = 350; // ms：起跳后多久才开始加速
const SLIDER_ACCEL_STEP  = 280; // ms：每过这么久步长 +1
const SLIDER_ACCEL_CAP   = 20;  // 单帧最大步长（避免一步跨太多）
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
  killGame: false,
  openKeyboard: false,
};
let startUsedAsModifier = false; // Start 键是否在本轮按住中被用作快捷调节修饰键

let engineOpts: GamepadEngineOptions | null = null; // 供连接/可见/唤醒事件重启循环使用
let noPadStreak = 0;       // 连续无手柄帧计数（Grace 防瞬时断连误杀）
const NO_PAD_GRACE = 30;   // 约 0.5s@60fps：连续这么多帧无手柄才停止循环
let summonActive = false;  // 呼出后置 true：强制视为可见，直到窗口再次隐藏（绕开 WebView2 可见态未及时刷新）

// 是否有任意已连接手柄
function hasConnectedPad(): boolean {
  try {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) if (p && p.connected) return true;
  } catch { /* 某些 WebView2 版本 getGamepads() 可能抛异常 */ }
  return false;
}

// 若循环未运行则启动（幂等）：用于连接/可见/唤醒事件拉起
function ensureLoop(opts: GamepadEngineOptions) {
  if (rafId) return;
  noPadStreak = 0;
  rafId = requestAnimationFrame(() => tick(opts));
}

// 监听手柄连接/断开事件（某些 WebView2 版本需要此触发轮询）
if (typeof window !== 'undefined') {
  // 插入/切换手柄 → 立即重启循环
  window.addEventListener('gamepadconnected', () => {
    gamepadConnected = true;
    if (engineOpts) ensureLoop(engineOpts);
  });
  // 断开 → 若已无任何手柄仍连着，停止循环（省 CPU）；仍有其他手柄则保留
  window.addEventListener('gamepaddisconnected', () => {
    if (!hasConnectedPad()) {
      gamepadConnected = false;
      stopGamepad();
    }
  });
  // 睡眠守护唤醒后，native 通知重启手柄引擎：重置边沿/时间戳状态，并尝试重启循环
  window.addEventListener('ipc:gamepad.restart', () => {
    restartGamepadState();
    if (engineOpts && hasConnectedPad()) ensureLoop(engineOpts);
  });
  // R1L1 呼出（native 后台线程 bringToFront 后发出）：强制接管手柄导航。
  // ① 置 summonActive 让循环即便 WebView2 可见态未刷新也照常处理输入；
  // ② 重置边沿基线，避免呼出瞬间把“仍按住的 R1L1”误判成新的按下边沿（误切页）；
  // ③ 重启循环（若此前因无手柄被停 / 隐藏态未运行，现在必定拉起）。
  window.addEventListener('ipc:gamepad.summon', () => {
    summonActive = true;
    restartGamepadState();
    if (engineOpts) ensureLoop(engineOpts);
  });
  // 回到可见（含系统唤醒窗口恢复可见）：兜底探测手柄并启动循环
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      if (engineOpts && hasConnectedPad()) ensureLoop(engineOpts);
    } else {
      // 窗口再次隐藏：清除呼出强制态，循环回到隐藏行为（仍保持运行以记录边沿）
      summonActive = false;
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

  if (dy !== 0) {
    // ── 上下导航：行优先 ──
    // 用户希望方向键按“内容行”移动。例如从“帧数目标 30”按上，应跳到
    // 同一卡片内上一行的“使用电池”下拉，而不是跳到更靠左的“野蛮系统电源”。
    const ROW_BAND = 28; // 同一行内 center-y 容差（px）
    type Cand = { el: HTMLElement; y: number; x: number; dyAbs: number };
    const cands: Cand[] = [];
    for (const el of els) {
      if (el === base) continue;
      const r = el.getBoundingClientRect();
      const ey = r.top + r.height / 2;
      const ddy = ey - by;
      if (dy > 0 && ddy <= 0) continue;
      if (dy < 0 && ddy >= 0) continue;
      cands.push({ el, y: ey, x: r.left + r.width / 2, dyAbs: Math.abs(ddy) });
    }
    if (cands.length === 0) return;
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
  } else {
    // ── 左右导航：沿用空间优先（同行内移动）──
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

// ── 滑块微调（线性加速）：按住越久，单步跨度越大、触发越密 ──
// 仅当焦点在滑块（engine 调用前已判定）时由调用方传入左右/LT/RT 状态。
function microAdjustSlider(now: number, heldL: boolean, heldR: boolean, lt: boolean, rt: boolean) {
  if (!(heldL || heldR || lt || rt)) { sliderAccelStart = 0; sliderLastApply = 0; return; }
  const dir = heldR || rt ? 1 : -1;
  if (sliderAccelStart === 0) sliderAccelStart = now;
  const held = now - sliderAccelStart;
  let step = 1;
  let interval = 45; // 基础重复间隔（ms），加速后变密
  if (held > SLIDER_ACCEL_DELAY) {
    step = Math.min(1 + Math.floor((held - SLIDER_ACCEL_DELAY) / SLIDER_ACCEL_STEP), SLIDER_ACCEL_CAP);
    interval = Math.max(16, 45 - Math.floor(held / 300) * 8);
  }
  if (now - sliderLastApply >= interval) {
    adjustSlider(dir * step);
    sliderLastApply = now;
  }
}

// ── 主循环 ──
function tick(opts: GamepadEngineOptions) {
  const now = performance.now();
  const pad = getPad();
  // 窗口隐藏（在托盘）时不处理手柄输入：避免后台空转，也避免“非活动窗口仍监听”
  // 呼出期间 (summonActive) 强制视为可见，绕开 WebView2 可见态未及时刷新的窗口
  if (document.visibilityState !== 'visible' && !summonActive) {
    if (pad) { prevButtons = pad.buttons.map((x) => x.pressed); prevAxes = Array.from(pad.axes); }
    rafId = requestAnimationFrame(() => tick(opts));
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
    const b = pad.buttons.map((x) => x.pressed);
    const a = Array.from(pad.axes);

    // 状态重置后首帧（呼出/唤醒/连接后边沿基线被清空）：仅重建基线，不触发边沿，避免误触
    if (prevButtons.length !== b.length) {
      prevButtons = b; prevAxes = a;
      rafId = requestAnimationFrame(() => tick(opts));
      return;
    }

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
      // 先给当前页面/弹层一个拦截机会：例如快捷应用菜单打开时，B 只执行取消。
      // 未被拦截时再走通用返回/失焦逻辑，避免弹层和全局返回双重响应。
      const backEvent = new CustomEvent('ipc:gamepad-back', { cancelable: true });
      window.dispatchEvent(backEvent);
      if (backEvent.defaultPrevented) {
        log(opts, 'B · 当前页面处理');
      } else if (sliderEditMode) {
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
          if (dirX !== 0 && dirY === 0) moveFocus(dirX, 0);
          else if (dirY !== 0 && dirX === 0) moveFocus(0, dirY);
          else if (dirX !== 0 && dirY !== 0) {
            // 对角线优先水平
            moveFocus(dirX, 0);
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
  if (rafId) return () => stopGamepad();
  prevButtons = [];
  prevAxes = [];
  summonGet().then((s) => { gpSettings = { ...gpSettings, ...s }; });
  // 仅检测到手柄时才启动 60fps 循环；无手柄保持停止，由连接/可见/唤醒事件拉起（省 CPU）
  if (hasConnectedPad()) {
    log(opts, '手柄引擎已启动（检测到手柄）');
    ensureLoop(opts);
  } else {
    log(opts, '手柄引擎就绪（无手柄，待连接）');
  }
  return () => stopGamepad();
}
export function stopGamepad() {
  if (rafId) cancelAnimationFrame(rafId);
  rafId = 0;
}
