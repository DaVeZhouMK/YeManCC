// scheduler.ts - shared frontend scheduler for low-frequency state checks.
//
// 设计（191 §3.3，2026-09-20 重写；上一版为 1000ms 固定心跳 + 每任务 busy 标记）：
//   · **单一全局异步泵 + 单一 setTimeout**：定时器指向"最早 eligible 的截止时间"。
//     不用 1000ms 固定心跳（无法表达 500/1250ms 的声明周期），也不给每个任务各起
//     一条 setInterval（唤醒数随任务数增长）。
//   · **任务实例身份**：执行与注销都要求 `tasks.get(name) === 该实例`；同名替换后
//     旧对象不会再被执行，旧 disposer 也不会误删新注册（前者是旧实现的真实缺口：
//     due 列表持旧对象、执行前只查 name 是否存在）。
//   · **每次 await 之后复核身份与可见性**：等待期间被替换/卸载的任务不再参与本轮；
//     隐藏期间不会启动仅展示型（pauseWhenHidden）任务。
//   · **错过槽不补跑**：从原计划截止推进到未来（跳过整段错过的槽），可见性恢复或
//     长暂停后不会爆发式补跑，同时保留声明周期的相位。
//   · **无任务或全暂停 ⇒ 无 timer**：不空转唤醒；可见性/任务集变化时重新对账。
//
// 声明周期是前端刷新语义（可见时按声明周期唤醒），不承诺浏览器精确实时。
import { isUiVisible, onUiVisibilityChange } from './bridge/uiLifecycle';

export interface ScheduledTaskOptions {
  pauseWhenHidden?: boolean;
  runImmediately?: boolean;
}

interface ScheduledTask {
  name: string;
  periodMs: number;
  run: () => void | Promise<unknown>;
  pauseWhenHidden: boolean;
  nextDueAt: number;
  busy: boolean;
}

const tasks = new Map<string, ScheduledTask>();
let timer: number | null = null;
let pumping = false;
let visibilityBound = false;
let stopUiVisibilityBinding: (() => void) | null = null;

function ensureVisibilityBinding(): void {
  if (visibilityBound) return;
  stopUiVisibilityBinding = onUiVisibilityChange(onVisibilityChange);
  visibilityBound = true;
}

function removeVisibilityBindingIfIdle(): void {
  if (!visibilityBound || tasks.size > 0) return;
  stopUiVisibilityBinding?.();
  stopUiVisibilityBinding = null;
  visibilityBound = false;
}

function clearTimer(): void {
  if (timer === null) return;
  window.clearTimeout(timer);
  timer = null;
}

// 从原计划截止推进到未来：错过整段槽时直接跳过，不补跑（保留 period 相位）。
function advanceDeadline(planned: number, periodMs: number, now: number): number {
  if (planned > now) return planned;
  const missed = Math.floor((now - planned) / periodMs) + 1;
  return planned + missed * periodMs;
}

function taskRunnable(task: ScheduledTask): boolean {
  return !task.busy && (!task.pauseWhenHidden || isUiVisible());
}

// 单一最早截止 timer：无任务或全暂停时不布防（后者由可见性变化重新对账）。
function scheduleNext(): void {
  clearTimer();
  if (pumping) return;
  const now = performance.now();
  let earliest: number | null = null;
  for (const task of tasks.values()) {
    if (task.pauseWhenHidden && !isUiVisible()) continue;
    const deadline = task.nextDueAt > now ? task.nextDueAt : now;
    if (earliest === null || deadline < earliest) earliest = deadline;
  }
  if (earliest === null) return;
  timer = window.setTimeout(() => {
    timer = null;
    void runPump();
  }, Math.max(0, earliest - now));
}

function pickDueTask(): ScheduledTask | null {
  const now = performance.now();
  let picked: ScheduledTask | null = null;
  for (const task of tasks.values()) {
    if (!taskRunnable(task) || task.nextDueAt > now) continue;
    if (picked === null || task.nextDueAt < picked.nextDueAt) picked = task;
  }
  return picked;
}

// 全局异步泵：所有到期任务串行执行，避免若干 PowerShell/IPC 操作同时压到桥接层。
async function runPump(): Promise<void> {
  if (pumping) return;
  pumping = true;
  try {
    for (;;) {
      const task = pickDueTask();
      if (task === null) break;
      task.nextDueAt = advanceDeadline(task.nextDueAt, task.periodMs, performance.now());
      task.busy = true;
      try {
        await task.run();
      } catch {
        // 单项状态检查 best-effort：失败不产生未处理拒绝，下一槽照常重试。
      } finally {
        task.busy = false;
      }
      // await 之后复核身份：等待期间被同名替换/卸载 ⇒ 旧实例不再参与本轮调度。
      if (tasks.get(task.name) !== task) continue;
      // P0 慢任务闸门（193 §2）：任务可能比声明周期还慢（如周期 500ms、执行 800ms）。
      // 执行前的那次推进只"预定"了本轮占用的槽；若完成时刻已越过它，必须按**原周期
      // 相位**再跳过已错过的槽，使下一截止**严格晚于完成时刻**——否则会出现"刚做完
      // 立刻再做"的持续追跑（桥接查询被长期占满）。此步与长暂停跳槽共用同一函数，
      // 语义一致：只跳过、不补跑。
      task.nextDueAt = advanceDeadline(task.nextDueAt, task.periodMs, performance.now());
    }
  } finally {
    pumping = false;
    scheduleNext();
  }
}

function onVisibilityChange(): void {
  scheduleNext();
  if (isUiVisible()) void runPump();
}

function stopTimerIfIdle(): void {
  if (tasks.size > 0) return;
  clearTimer();
  removeVisibilityBindingIfIdle();
}

function reconcileTimer(): void {
  if (tasks.size === 0) {
    stopTimerIfIdle();
    return;
  }
  scheduleNext();
}

export function registerScheduledTask(
  name: string,
  periodMs: number,
  run: () => void | Promise<unknown>,
  options: ScheduledTaskOptions = {},
): () => void {
  if (!name) throw new Error('scheduler task name is required');
  if (!Number.isFinite(periodMs) || periodMs <= 0) {
    throw new Error('scheduler task period must be positive');
  }

  // 同名替换：直接换实例。旧实例的 in-flight await 由身份复核兜住；
  // 旧 disposer 只能注销它自己的实例（见 unregisterTaskInstance）。
  const task: ScheduledTask = {
    name,
    periodMs,
    run,
    pauseWhenHidden: options.pauseWhenHidden === true,
    nextDueAt: performance.now() + periodMs,
    busy: false,
  };
  tasks.set(name, task);
  ensureVisibilityBinding();
  if (options.runImmediately) task.nextDueAt = performance.now();
  reconcileTimer();
  if (options.runImmediately) void runPump();

  return () => unregisterTaskInstance(task);
}

// 实例化注销：只有"当前注册仍是该实例"时才删除，旧 disposer 不会误删新注册。
function unregisterTaskInstance(task: ScheduledTask): void {
  if (tasks.get(task.name) !== task) return;
  tasks.delete(task.name);
  reconcileTimer();
}

export function unregisterScheduledTask(name: string): void {
  tasks.delete(name);
  reconcileTimer();
}

export function hasScheduledTask(name: string): boolean {
  return tasks.has(name);
}

export function getScheduledTaskNames(): string[] {
  return [...tasks.keys()];
}