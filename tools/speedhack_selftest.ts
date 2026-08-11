// OpenSpeedy safety-chain self-test. No process injection is performed.

type Msg = { id: number; cmd: string; args: any };

const listeners: Array<(e: { data: any }) => void> = [];
const shellScripts: string[] = [];
let shellStdout = '';

function dispatch(cmd: string, args: any): any {
  if (cmd === 'fs.exists') return false;
  if (cmd === 'fs.writeTextFile') return true;
  if (cmd === 'shell.run') {
    shellScripts.push(String(args?.args?.[2] || ''));
    return { exitCode: shellStdout.includes('RESULT:failed') ? 2 : 0, stdout: shellStdout, stderr: '' };
  }
  throw new Error(`Unhandled mock command: ${cmd}`);
}

(globalThis as any).window = {
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

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
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

  shellStdout = 'ARCH:x64\nSAFE_SKIP:bridge_conflict\nSAFE_FALLBACK:1\n';
  const conflict = await speedhack.applyGameSpeed(222, 2, { pid: 222, name: 'safe-game.exe' });
  assert(conflict.ok && conflict.skipped && conflict.reason === 'bridge_conflict', 'foreign bridge must cause safe skip');
  passed++;

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
  assert(script.indexOf("Send-BridgeCommand 'SETSPEED 1'") < script.indexOf("Send-BridgeCommand 'INJECT 333'"), '1x reset must happen before injection');
  assert(script.includes("Send-BridgeCommand 'GETSPEED'"), 'transaction must verify speed by readback');
  assert(script.includes('function Invoke-SafeRollback'), 'transaction must contain rollback');
  passed++;

  shellStdout = 'ARCH:x64\nRESP:OK\nRESP:ERROR injection failed\nRESP:OK\nSAFE_FALLBACK:1\nRESULT:failed\n';
  const failed = await speedhack.applyGameSpeed(444, 8, { pid: 444, name: 'safe-game.exe' });
  assert(!failed.ok && failed.safeFallback && failed.reason === 'operation_failed', 'failed injection must report 1x fallback');
  passed++;

  console.log(`OpenSpeedy safety self-test: ${passed}/5 passed`);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
