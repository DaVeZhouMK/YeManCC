// OpenSpeedy safety-chain self-test. No process injection is performed.

type Msg = { id: number; cmd: string; args: any };

const listeners: Array<(e: { data: any }) => void> = [];
const shellScripts: string[] = [];
const writtenLogs: string[] = [];
let shellStdout = '';
let valvePid = 111;

function dispatch(cmd: string, args: any): any {
  if (cmd === 'game.detect') {
    return {
      pid: valvePid,
      name: valvePid === 111 ? 'Dungeons-Win64-Shipping.exe' : 'safe-game.exe',
      title: valvePid === 111 ? 'Minecraft Dungeons' : 'Safe Game',
      path: valvePid === 111 ? 'C:\\Games\\Minecraft Dungeons\\Dungeons-Win64-Shipping.exe' : 'C:\\Games\\safe-game.exe',
      processCreated: String(valvePid * 1000 + 7),
      source: 'memory',
    };
  }
  if (cmd === 'fs.exists') return false;
  if (cmd === 'fs.writeTextFile') {
    writtenLogs.push(String(args?.content || ''));
    return true;
  }
  if (cmd === 'shell.run') {
    shellScripts.push(String(args?.args?.[2] || ''));
    return { exitCode: shellStdout.includes('RESULT:failed') ? 2 : 0, stdout: shellStdout, stderr: '' };
  }
  throw new Error(`Unhandled mock command: ${cmd}`);
}

(globalThis as any).window = {
  setTimeout(callback: () => void) {
    return setTimeout(callback, 0);
  },
  addEventListener() {},
  removeEventListener() {},
  dispatchEvent() { return true; },
  chrome: {
    webview: {
      postMessage(msg: Msg) {
        Promise.resolve().then(() => {
          try {
            const result = dispatch(msg.cmd, msg.args || {});
            listeners.forEach((listener) => listener({ data: { id: msg.id, result } }));
          } catch (error: any) {
            listeners.forEach((listener) => listener({ data: { id: msg.id, error: String(error?.message || error) } }));
          }
        });
      },
      addEventListener(_type: string, listener: (e: { data: any }) => void) {
        listeners.push(listener);
      },
    },
  },
};
(globalThis as any).document = {
  visibilityState: 'visible',
  addEventListener() {},
  removeEventListener() {},
};

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

async function waitUntil(predicate: () => boolean, message: string): Promise<void> {
  for (let attempt = 0; attempt < 50; attempt++) {
    if (predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 0));
  }
  throw new Error(message);
}

async function main() {
  const speedhack = await import('@/bridge/speedhack');
  let passed = 0;

  const dungeons = {
    pid: 111,
    name: 'Dungeons-Win64-Shipping.exe',
    title: 'Minecraft Dungeons',
    path: 'C:\\Games\\Minecraft Dungeons\\Dungeons-Win64-Shipping.exe',
  };
  assert(speedhack.isMinecraftTarget(dungeons), 'Minecraft Dungeons should be blocked');
  assert(speedhack.isMinecraftTarget({ pid: 112, name: 'javaw.exe' }), 'javaw should be blocked');
  passed++;

  const blocked = await speedhack.applyGameSpeed(dungeons.pid, 4, dungeons);
  assert(blocked.ok && blocked.skipped && blocked.safeFallback, 'blocked target must stay at 1x');
  assert(shellScripts.length === 0, 'blocked target must not invoke PowerShell or bridge');
  passed++;

  valvePid = 222;
  shellStdout = 'ARCH:x64\nSAFE_SKIP:bridge_conflict\nSAFE_FALLBACK:1\n';
  const conflict = await speedhack.applyGameSpeed(222, 2, { pid: 222, name: 'safe-game.exe' });
  assert(conflict.ok && conflict.skipped && conflict.reason === 'bridge_conflict', 'foreign bridge must cause safe skip');
  passed++;

  valvePid = 333;
  shellStdout = [
    'ARCH:x64',
    'RESP:OK',
    'RESP:OK',
    'RESP:OK',
    'RESP:OK',
    'RESP:OK 2.000000',
    'RESULT:ok',
  ].join('\n');
  const success = await speedhack.applyGameSpeed(333, 2, { pid: 333, name: 'safe-game.exe' });
  assert(success.ok && !success.skipped, 'successful transaction should be accepted');
  const script = shellScripts.at(-1) || '';
  const firstTransaction = script.slice(script.indexOf('# Target selection comes from the game recognition valve'));
  assert(!firstTransaction.includes("Send-BridgeCommand 'SETSPEED 1'"), 'first injection must not reset the shared factor before injection');
  assert(firstTransaction.includes("Send-BridgeCommand 'INJECT 333'"), 'first application must inject the target');
  assert(firstTransaction.includes("Send-BridgeCommand 'ENABLE 333'"), 'first application must enable the target');
  assert(firstTransaction.includes('Test-TargetEnabled 333'), 'first application must verify the target mapping');
  assert(script.includes('function Get-TargetMappingState'), 'target state must be verified by PID mapping');
  assert(!script.includes('Get-TargetDllState'), 'target matching must not inspect names or DLL paths');
  assert(firstTransaction.includes("Send-BridgeCommand 'GETSPEED'"), 'transaction must verify speed by readback');
  assert(script.includes('function Invoke-SafeRollback'), 'transaction must contain rollback');
  assert(script.includes('BRIDGE_EXPECTED:'), 'diagnostics must record the selected official bridge path');
  assert(script.includes('BRIDGE_CMD:'), 'diagnostics must record every bridge command response and duration');
  assert(writtenLogs.some((entry) => entry.includes('QUEUE diagVersion=4 appSession=')), 'diagnostics must record the request before it enters the operation queue');
  assert(writtenLogs.some((entry) => entry.includes('BEGIN diagVersion=4 appSession=')), 'diagnostics must record versioned transaction begin state');
  assert(writtenLogs.some((entry) => entry.includes('source=user-factor')), 'diagnostics must record the operation source');
  assert(writtenLogs.some((entry) => entry.includes('queueWaitMs=')), 'diagnostics must record queue wait time');
  assert(writtenLogs.some((entry) => entry.includes('sincePreviousMs=')), 'diagnostics must record time since the previous speed change');
  assert(writtenLogs.some((entry) => entry.includes('sinceFirstApplyMs=')), 'diagnostics must record time since the first factor application');
  assert(writtenLogs.some((entry) => entry.includes('END op=')), 'diagnostics must record transaction result state');
  assert(script.includes('TARGET_SNAPSHOT:'), 'diagnostics must record PID-only target process snapshots');
  assert(script.includes('COMPONENT:'), 'diagnostics must fingerprint the official bridge and speedpatch files');
  assert(script.includes('BRIDGE_PIPE_CONNECT:'), 'diagnostics must record named-pipe connection duration');
  assert(script.includes('BRIDGE_SELECTED_PID:'), 'diagnostics must record the actual bridge PID');
  passed++;

  valvePid = 333;
  shellStdout = 'ARCH:x64\nRESP:OK ALREADY_INJECTED\nRESP:OK 4.000000\nRESULT:ok\n';
  const second = await speedhack.applyGameSpeed(333, 4, { pid: 333, name: 'safe-game.exe' });
  assert(second.ok && !second.skipped, 'same target factor change should succeed');
  const secondScript = shellScripts.at(-1) || '';
  assert(secondScript.includes('Get-TargetMappingState 333'), 'same target factor change must use the PID mapping');
  assert(!secondScript.includes('Get-TargetDllState'), 'same target factor change must not inspect DLL paths');
  assert(secondScript.includes("Send-BridgeCommand 'SETSPEED 4'"), 'same target factor change must set only the new factor');
  assert(secondScript.includes('Test-TargetEnabled 333'), 'same target factor change must verify the target mapping');
  assert(secondScript.includes('Test-OptionalSpeedReadback'), 'speed change must tolerate bridge readback variants');
  passed++;

  valvePid = 333;
  shellStdout = 'ARCH:x64\nRESP:OK ALREADY_INJECTED\nRESP:OK\nRESULT:ok\n';
  const plainOkReadback = await speedhack.applyGameSpeed(333, 4, { pid: 333, name: 'safe-game.exe' });
  assert(plainOkReadback.ok && !plainOkReadback.skipped, 'plain OK GETSPEED readback should succeed');
  passed++;

  valvePid = 333;
  shellStdout = 'ARCH:x64\nRESP:OK\nRESP:OK\nSAFE_FALLBACK:1\nRESULT:ok\n';
  const reset = await speedhack.clearGameSpeed(333, 'user-reset');
  assert(reset.ok && reset.safeFallback, 'X1 reset should succeed');
  const resetScript = shellScripts.at(-1) || '';
  assert(resetScript.includes("Send-BridgeCommand 'SETSPEED 1'"), 'X1 must set speed to 1');
  assert(resetScript.includes("Send-BridgeCommand 'GETSPEED'"), 'X1 must verify the reset with GETSPEED');
  const resetTransaction = resetScript.slice(resetScript.lastIndexOf("$resp = Send-BridgeCommand 'SETSPEED 1'"));
  assert(!resetTransaction.includes('DISABLE 333') && !resetTransaction.includes('EJECT 333'), 'X1 must not unload the game hook');
  assert(writtenLogs.some((entry) => entry.includes('source=user-reset')), 'X1 diagnostics must distinguish a user reset');
  await waitUntil(
    () => shellScripts.some((entry) => entry.includes('POST_X1_SNAPSHOT:op=')),
    'X1 must schedule the delayed read-only diagnostic probe',
  );
  const postResetScript = shellScripts.find((entry) => entry.includes('POST_X1_SNAPSHOT:op=')) || '';
  assert(postResetScript.includes('Write-PostResetSnapshot 250'), 'X1 diagnostics must sample at 250ms');
  assert(postResetScript.includes('Write-PostResetSnapshot 1000'), 'X1 diagnostics must sample at 1s');
  assert(postResetScript.includes('Write-PostResetSnapshot 3000'), 'X1 diagnostics must sample at 3s');
  assert(postResetScript.includes('POST_X1_EVENTS:'), 'X1 diagnostics must capture nearby Windows error and hang events');
  assert(!/Send-BridgeCommand|\b(?:INJECT|ENABLE|DISABLE|EJECT|SETSPEED)\b/.test(postResetScript), 'post-X1 diagnostics must never send OpenSpeedy commands');
  assert(writtenLogs.some((entry) => entry.includes('POST_X1_SCHEDULE diagVersion=4')), 'X1 diagnostics must record its schedule without blocking the result');
  passed++;

  valvePid = 444;
  shellStdout = 'ARCH:x64\nRESP:OK\nRESP:OK\nRESP:OK\nRESP:OK 8.000000\nRESULT:ok\n';
  const switched = await speedhack.applyGameSpeed(444, 8, { pid: 444, name: 'other-game.exe' });
  assert(switched.ok, 'switching target should succeed');
  const switchedScript = shellScripts.at(-1) || '';
  assert(switchedScript.includes("Send-BridgeCommand 'DISABLE 333'"), 'switching target must disable the previous target');
  assert(switchedScript.includes("Send-BridgeCommand 'INJECT 444'"), 'switching target must inject the new target');
  passed++;

  valvePid = 555;
  shellStdout = 'ARCH:x64\nRESP:OK\nRESP:ERROR injection failed\nRESP:OK\nSAFE_FALLBACK:1\nRESULT:failed\n';
  const failed = await speedhack.applyGameSpeed(555, 8, { pid: 555, name: 'safe-game.exe' });
  assert(!failed.ok && failed.safeFallback && failed.reason === 'operation_failed', 'failed injection must report 1x fallback');
  passed++;

  console.log(`OpenSpeedy safety self-test: ${passed}/9 passed`);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
