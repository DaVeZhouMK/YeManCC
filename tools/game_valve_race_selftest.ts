// GameTargetArbiter race self-test. No process control is performed.
// The IPC mock models a valve switch while the previous target is paused.
import './mock-shell';
import { refreshGameStatus, subscribeGameStatus } from '@/bridge/gamedetect';

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
  return undefined;
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

    console.log('GameTargetArbiter switch race self-test: PASS');
  } finally {
    stop();
    delete (globalThis as any).__mockIpcResponder;
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
