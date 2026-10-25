// GameTargetArbiter race self-test. No process control is performed.
// The IPC mock models a valve switch while the previous target is paused.
import './mock-shell';
import { refreshGameStatus, subscribeGameStatus, clearGameCache } from '@/bridge/gamedetect';

(globalThis as any).window.setInterval = setInterval;
(globalThis as any).window.clearInterval = clearInterval;

type Target = {
  pid: number;
  name: string;
  title: string;
  path: string;
  processCreated: string;
  source: string;
  ts: number;
};

const statePath = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\quickapp_suspended.json';
let valvePid = 111;
let virtualSuspendState: string | null = null;
let resumeArgs: any = null;
const controlCalls: any[] = [];
let invalidateDuringIdentity = false;

function target(pid: number): Target {
  return {
    pid,
    name: `game-${pid}.exe`,
    title: `Game ${pid}`,
    path: `C:\\Games\\game-${pid}.exe`,
    processCreated: `${pid}007`,
    source: 'memory',
    ts: Date.now(),
  };
}

(globalThis as any).__mockIpcResponder = (command: string, args: any) => {
  if (command === 'game.detect') return target(valvePid);
  if (command === 'process.identity') {
    const identity = { valid: true, pid: args.pid, processCreated: target(args.pid).processCreated };
    if (invalidateDuringIdentity) { invalidateDuringIdentity = false; clearGameCache(); valvePid = 444; }
    return identity;
  }
  if (command === 'shell.run') {
    assert(args.program === 'taskkill', 'unexpected control command');controlCalls.push(args);
    return { exitCode: 0, stdout: '', stderr: '' };
  }
  if (command === 'game.resume') {
    resumeArgs = args;
    return { resumed: 1, failedPids: [], stalePids: [] };
  }
  if (command === 'fs.exists' && args?.path === statePath) return virtualSuspendState !== null;
  if (command === 'fs.readTextFile' && args?.path === statePath) return virtualSuspendState || '';
  if (command === 'fs.remove' && args?.path === statePath) {
    virtualSuspendState = null;
    return true;
  }
  throw new Error('Unexpected IO in valve diagnostic: ' + command);
};

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

async function main(): Promise<void> {
  const stop = subscribeGameStatus(() => {});
  try {
    await refreshGameStatus();
    virtualSuspendState = JSON.stringify({
      root: 111,
      pids: [111],
      processes: [{ pid: 111, processCreated: '111007' }],
      ts: Date.now(),
    });

    // A quick switch elects B, but it must not release a user-owned pause.
    // The new single-lease policy releases it only on explicit resume or
    // before a later pause chooses a different PID.
    valvePid = 222;
    await refreshGameStatus();
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert(resumeArgs === null, 'target switch must not auto-resume a manual pause');
    assert(virtualSuspendState !== null, 'target switch must retain the pause lease');

    const { closeGame } = await import('@/bridge/gameproc');
    const stale = await closeGame(111, 'A', target(111).processCreated);
    assert(!stale.ok && controlCalls.length === 0, 'stale A cannot close a process after B is elected');
    const reused = await closeGame(222, 'B', '999999');
    assert(!reused.ok && controlCalls.length === 0, 'wrong process birth cannot issue taskkill');
    invalidateDuringIdentity = true;
    const uncertain = await closeGame(222, 'B', target(222).processCreated);
    assert(!uncertain.ok && controlCalls.length === 0, 'authority invalidation during process identity read cannot authorize taskkill');
    const current = await closeGame(444, 'C', target(444).processCreated);
    assert(current.ok && controlCalls.length === 1, 'confirmed current target issues exactly one mocked taskkill');
    assert(resumeArgs === null && virtualSuspendState !== null, 'closing another target cannot consume the old manual pause lease');
    console.log('GameTargetArbiter switch/close race self-test: 6/6 PASS; all IO mocked');
  } finally {
    stop();
    delete (globalThis as any).__mockIpcResponder;
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
