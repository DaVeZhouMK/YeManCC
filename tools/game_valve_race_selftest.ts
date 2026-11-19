// GameTargetArbiter race self-test. No process control is performed.
// The IPC mock models a valve switch while the previous target is paused.
import './mock-shell';
import { readFileSync } from 'node:fs';
import { refreshGameStatus, refreshGameStatusStrict, subscribeGameStatus, clearGameCache, isGameStatusCurrent, getCurrentGame } from '@/bridge/gamedetect';

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
const configurationOnly = process.argv.includes("--config-failure-only");
const wireAt = process.argv.indexOf('--native-empty-wire');
if (wireAt >= 0 && (configurationOnly || process.argv.length !== wireAt + 2)) throw new Error('Exclusive native-empty-wire fixture path required');
const nativeWire: { name: string; snapshot: any }[] | null = wireAt < 0 ? null : JSON.parse(readFileSync(process.argv[wireAt + 1], 'utf8')).frames;
const wireNames = ['empty-first','empty-repeat','pending','complete-after-pending','game-start','game-exit'];
if (nativeWire && (nativeWire.length !== wireNames.length || nativeWire.some((row, i) => row.name !== wireNames[i]))) throw new Error('Exact native wire frame selection required');
let wireIndex = 0;
let ruleReadUnavailable = false;
let configGeneration = 0;

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
  if (command === 'game.detect') {
    if (nativeWire) return structuredClone(nativeWire[wireIndex].snapshot);
    if (ruleReadUnavailable) throw new Error('GAME_TARGET_OBSERVATION_UNAVAILABLE');
    return configurationOnly ? { ...target(valvePid), valid: true, generation: ++configGeneration, identityGeneration: 1 } : target(valvePid);
  }
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
    if (nativeWire) {
      const cases: string[] = [];
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame() === null, 'initial native empty observation not current');
      cases.push('initial native empty becomes current');
      wireIndex = 1;
      assert(nativeWire[0].snapshot.generation === nativeWire[1].snapshot.generation, 'native producer did not reuse empty generation');
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame() === null, 'same-generation native empty was rejected');
      cases.push('same-generation native empty stays current');
      wireIndex = 2;
      let rejected = false;
      try { await refreshGameStatusStrict(); } catch { rejected = true; }
      assert(rejected && !isGameStatusCurrent(), 'pending native discovery became complete idle');
      cases.push('pending native discovery revokes readiness');
      wireIndex = 3;
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame() === null, 'complete native empty did not restore readiness');
      cases.push('fresh complete native empty restores readiness');
      wireIndex = 4;
      const game = await refreshGameStatusStrict();
      assert(game && isGameStatusCurrent() && getCurrentGame()?.pid === game.pid, 'native game start did not replace empty target');
      cases.push('native game start replaces empty target');
      wireIndex = 5;
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame() === null, 'native game exit did not replace game target');
      cases.push('native game exit becomes current empty target');
      const { closeGame } = await import('@/bridge/gameproc');
      const result = await closeGame(game.pid, game.name, game.processCreated);
      assert(!result.ok && controlCalls.length === 0 && resumeArgs === null, 'old game identity retained process control after native empty target');
      cases.push('exited identity cannot control a process');
      console.log(JSON.stringify({ status: 'PASS', selector: 'native-empty-wire', cases, nativeFrames: nativeWire.length, actualGamedetectConsumer: true, realControls: 0 }));
      return;
    }
    if (configurationOnly) {
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame()?.pid === valvePid, 'healthy initial game authority missing');
      ruleReadUnavailable = true;
      let rejected = false;
      try { await refreshGameStatusStrict(); } catch { rejected = true; }
      assert(rejected && !isGameStatusCurrent(), 'configuration failure became confirmed no-game');
      assert(getCurrentGame()?.pid === valvePid, 'configuration failure destroyed the last display snapshot');
      for (let index = 0; index < 12; index += 1) {
        await refreshGameStatus();
        assert(!isGameStatusCurrent() && getCurrentGame()?.pid === valvePid, 'soft retry restored unconfirmed authority');
      }
      const { closeGame } = await import('@/bridge/gameproc');
      let controlAllowed = false;
      try { controlAllowed = (await closeGame(valvePid, 'saved display', target(valvePid).processCreated)).ok; } catch { /* Controlled failure is not permission. */ }
      assert(!controlAllowed && controlCalls.length === 0 && resumeArgs === null, 'configuration failure permitted process control');
      ruleReadUnavailable = false;
      await refreshGameStatusStrict();
      assert(isGameStatusCurrent() && getCurrentGame()?.pid === valvePid, 'fixed rules did not recover same authoritative game');
      assert(controlCalls.length === 0 && resumeArgs === null, 'repair performed unsolicited process control');
      console.log(JSON.stringify({ status: 'PASS', selector: 'config-failure-only', explicitFailedReads: 14, softwareControls: 0, retainedDisplay: true, unknownReadiness: true, repaired: true }));
      return;
    }
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
