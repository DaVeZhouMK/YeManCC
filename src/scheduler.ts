// scheduler.ts - shared frontend scheduler for low-frequency state checks.
//
// Tasks share one low-frequency heartbeat, but keep independent periods and
// lifecycle. UI-only refresh tasks opt into pauseWhenHidden.
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

const BASE_TICK_MS = 1000;
const tasks = new Map<string, ScheduledTask>();
let heartbeat: number | null = null;
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

function onVisibilityChange(): void {
  reconcileHeartbeat();
  if (isUiVisible()) void schedulerTick();
}

function ensureHeartbeat(): void {
  if (heartbeat !== null) return;
  if (![...tasks.values()].some((task) => !task.pauseWhenHidden || isUiVisible())) return;
  heartbeat = window.setInterval(() => {
    void schedulerTick();
  }, BASE_TICK_MS);
}

function stopHeartbeatIfIdle(): void {
  if (tasks.size > 0 && [...tasks.values()].some((task) => !task.pauseWhenHidden || isUiVisible())) return;
  if (heartbeat === null) {
    if (tasks.size === 0) removeVisibilityBindingIfIdle();
    return;
  }
  window.clearInterval(heartbeat);
  heartbeat = null;
  if (tasks.size === 0) removeVisibilityBindingIfIdle();
}

function reconcileHeartbeat(): void {
  if (tasks.size === 0) {
    stopHeartbeatIfIdle();
    return;
  }
  const shouldRun = [...tasks.values()].some((task) => !task.pauseWhenHidden || isUiVisible());
  if (shouldRun) ensureHeartbeat();
  else stopHeartbeatIfIdle();
}

async function schedulerTick(): Promise<void> {
  const now = performance.now();
  const due = [...tasks.values()].filter((task) => {
    if (task.busy || now < task.nextDueAt) return false;
    if (task.pauseWhenHidden && !isUiVisible()) return false;
    return true;
  });

  // Run due tasks serially. This prevents several PowerShell/IPC operations
  // from hitting the WebView/native bridge at the same instant.
  for (const task of due) {
    if (!tasks.has(task.name) || task.busy) continue;
    task.busy = true;
    task.nextDueAt = performance.now() + task.periodMs;
    try {
      await task.run();
    } catch {
      // Individual state checks are best-effort; the next scheduled slot
      // retries without creating an async rejection in the UI.
    } finally {
      task.busy = false;
    }
  }
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

  unregisterScheduledTask(name);
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
  reconcileHeartbeat();

  if (options.runImmediately) {
    task.nextDueAt = performance.now();
    void schedulerTick();
  }

  return () => unregisterScheduledTask(name);
}

export function unregisterScheduledTask(name: string): void {
  tasks.delete(name);
  stopHeartbeatIfIdle();
}

export function hasScheduledTask(name: string): boolean {
  return tasks.has(name);
}

export function getScheduledTaskNames(): string[] {
  return [...tasks.keys()];
}
