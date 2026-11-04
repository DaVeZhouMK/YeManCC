// OpenSpeedy safety-chain self-test. No process injection is performed.

type Msg = { id: number; cmd: string; args: any };

// Explicit opt-in: the default safety chain and its assertions stay unchanged.
const blocklistStorm = process.argv.includes('--blocklist-storm');
const uncertainOwnershipOnly=process.argv.includes('--uncertain-ownership');
let blocklistPort: (cmd: string, args: any) => any = () => { throw new Error('Blocklist port not ready'); };

const listeners: Array<(e: { data: any }) => void> = [];
const shellScripts: string[] = [];
const writtenLogs: string[] = [];
let shellStdout = '';
let valvePid = 111;
let valveGeneration = 0;
let valveIdentityGeneration = 1;
let observedValvePid = 111;

function dispatch(cmd: string, args: any): any {
  if (blocklistStorm && cmd !== 'game.detect') return blocklistPort(cmd, args);
  if (cmd === 'game.detect') {
    valveGeneration++;
    if (observedValvePid !== valvePid) { observedValvePid = valvePid; valveIdentityGeneration = valveGeneration; }
    return {
      generation: valveGeneration, identityGeneration: valveIdentityGeneration,
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

  const oldLease = { pid: valvePid, processCreated: String(valvePid * 1000 + 7), identityGeneration: valveIdentityGeneration };
  valveIdentityGeneration = ++valveGeneration;
  const scriptsBefore = shellScripts.length;
  const reselected = await speedhack.applyGameSpeed(valvePid, 2, oldLease);
  assert(reselected.skipped && reselected.reason === 'stale_target', 'same PID and birth from an old selection lease must be rejected');
  assert(shellScripts.length === scriptsBefore, 'old selection cannot create a PowerShell transaction');
  assert(script.includes('$script:ymccExpectedIdentityGeneration') && script.includes('$snapshot.identityGeneration'), 'external transaction must validate the same selection lease');
  passed++;
  console.log(`OpenSpeedy safety self-test: ${passed}/10 passed`);
}


async function mainBlocklistStorm() {
  const nodeFs = await import('node:fs');
  const path = await import('node:path');
  const crypto = await import('node:crypto');
  const rootArg = process.argv.find(arg => arg.startsWith('--artifact-root='))?.slice('--artifact-root='.length);
  assert(rootArg && path.isAbsolute(rootArg), '--artifact-root must be an absolute owned evidence directory');
  const root = nodeFs.realpathSync(rootArg);
  assert(!nodeFs.lstatSync(rootArg).isSymbolicLink(), 'artifact root must not be a link');
  const marker = JSON.parse(nodeFs.readFileSync(path.join(root, 'OWNED.json'), 'utf8'));
  assert(marker.consumer === 'tools/speedhack_selftest.ts' && marker.member === 'speedhack-blocklist.txt', 'owned marker mismatch');
  const selected = process.argv.find(arg => arg.startsWith('--case='))?.slice('--case='.length);
  const virtualPath = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack-blocklist.txt';
  const logPath = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack.log';
  const limit = 1 << 20; // Existing fs.readTextFile default, now checked by the selected bounded path.
  type Spec = {
    name: string; text?: string | Uint8Array; expected: 'block' | 'allow'; minecraft?: boolean;
    fault?: 'exists' | 'read' | 'lock' | 'partial' | 'receipt-type' | 'receipt-offset' | 'receipt-size' | 'receipt-large' | 'receipt-null';
    existsValue?: any; decoded?: string; changeAfterRead?: string; appearAfterMissing?: string; disappearAfterExists?: boolean;
    directory?: boolean; retry?: { text?: string; expected: 'block' | 'allow' };
  };
  const specs: Spec[] = [
    { name: 'missing-list', expected: 'allow' },
    { name: 'empty-list', text: '', expected: 'allow' },
    { name: 'comments-only', text: '# custom exclusions\r\n \t# retained\n', expected: 'allow' },
    { name: 'matching-name', text: 'safe-game.exe\n', expected: 'block' },
    { name: 'matching-title-case', text: '  SAFE GAME # user comment\r\n', expected: 'block' },
    { name: 'nonmatching-entry', text: 'another-game.exe\n', expected: 'allow' },
    { name: 'bom-crlf', text: '\ufeffSAFE-GAME.EXE\r\n', expected: 'block' },
    { name: 'final-line-without-newline', text: 'safe-game.exe', expected: 'block' },
    { name: 'unicode-unknown', text: '未安装游戏\nunknown-entry.exe\n', expected: 'allow' },
    { name: 'duplicates-and-unknown', text: 'unknown.exe\nsafe-game.exe\nSAFE-GAME.EXE\nunknown.exe\n', expected: 'block' },
    { name: 'unknown-is-literal', text: '.*\nallow=true\nschema=999\n!safe-game.exe\n', expected: 'allow' },
    { name: 'utf8-invalid-byte', text: Uint8Array.from([0xff]), expected: 'block', retry: { text: 'safe-game.exe\n', expected: 'block' } },
    { name: 'utf8-truncated-sequence', text: Uint8Array.from([0xe4, 0xb8]), expected: 'block' },
    { name: 'utf16-bytes', text: Uint8Array.from([0xff, 0xfe, 0x61, 0]), expected: 'block' },
    { name: 'nul-byte', text: 'unknown\0entry\n', expected: 'block' },
    { name: 'control-byte', text: 'unknown\u0007entry\n', expected: 'block' },
    { name: 'replacement-character', text: 'unknown\ufffdentry\n', expected: 'block' },
    { name: 'unpaired-surrogate-response', text: 'another-game.exe\n', decoded: '\ud800', expected: 'block' },
    { name: 'oversize', text: 'x'.repeat(limit + 1), expected: 'block', retry: { text: 'another-game.exe\n', expected: 'allow' } },
    { name: 'at-existing-limit', text: '#'.repeat(limit), expected: 'allow' },
    { name: 'exists-denied', text: 'another-game.exe\n', fault: 'exists', expected: 'block', retry: { text: 'safe-game.exe\n', expected: 'block' } },
    { name: 'exists-string-response', text: 'another-game.exe\n', existsValue: 'false', expected: 'block' },
    { name: 'exists-null-response', text: 'another-game.exe\n', existsValue: null, expected: 'block' },
    { name: 'read-denied', text: 'another-game.exe\n', fault: 'read', expected: 'block', retry: { text: 'another-game.exe\n', expected: 'allow' } },
    { name: 'share-lock-release', text: 'another-game.exe\n', fault: 'lock', expected: 'block', retry: { text: 'safe-game.exe\n', expected: 'block' } },
    { name: 'short-read', text: 'another-game.exe\nsafe-game.exe\n', fault: 'partial', expected: 'block', retry: { text: 'safe-game.exe\n', expected: 'block' } },
    { name: 'receipt-nonstring', text: 'another-game.exe\n', fault: 'receipt-type', expected: 'block' },
    { name: 'receipt-offset', text: 'another-game.exe\n', fault: 'receipt-offset', expected: 'block' },
    { name: 'receipt-size-type', text: 'another-game.exe\n', fault: 'receipt-size', expected: 'block' },
    { name: 'receipt-oversize', text: 'another-game.exe\n', fault: 'receipt-large', expected: 'block' },
    { name: 'receipt-null', text: 'another-game.exe\n', fault: 'receipt-null', expected: 'block' },
    { name: 'changed-between-reads', text: 'another-game.exe\n', changeAfterRead: 'safe-game.exe\n', expected: 'block', retry: { text: 'another-game.exe\n', expected: 'allow' } },
    { name: 'missing-then-published', appearAfterMissing: 'another-game.exe\n', expected: 'block', retry: { text: 'another-game.exe\n', expected: 'allow' } },
    { name: 'disappeared-before-read', text: 'another-game.exe\n', disappearAfterExists: true, expected: 'block', retry: { text: 'safe-game.exe\n', expected: 'block' } },
    { name: 'directory-instead-of-list', directory: true, expected: 'block' },
    { name: 'builtin-precedes-unavailable-list', text: 'another-game.exe\n', fault: 'read', minecraft: true, expected: 'block' },
  ];
  const chosen = specs.filter(spec => !selected || spec.name === selected);
  assert(chosen.length > 0, 'unknown --case selection');
  const speedhack = await import('@/bridge/speedhack');
  const cases: any[] = [];
  let assertions = 0;
  let fixtureErrors = 0;
  for (const spec of chosen) {
    const errors: string[] = [];
    const steps: any[] = [];
    const check = (condition: unknown, message: string) => { assertions++; if (!condition) errors.push(message); };
    const file = path.join(root, `${spec.name}.txt`);
    assert(!nodeFs.existsSync(file), 'case files must be fresh');
    if (spec.directory) nodeFs.mkdirSync(file);
    else if (spec.text !== undefined) nodeFs.writeFileSync(file, spec.text);
    let active = spec;
    let existsCalls = 0;
    let readCalls = 0;
    let shellCalls = 0;
    let logWrites = 0;
    const requests: any[] = [];
    const digest = () => !nodeFs.existsSync(file) ? 'missing' : nodeFs.statSync(file).isDirectory() ? 'directory' : crypto.createHash('sha256').update(nodeFs.readFileSync(file)).digest('hex');
    let externallyPublished: string | undefined;
    blocklistPort = (cmd, args) => {
      if (cmd === 'fs.exists' && args.path === logPath) return false;
      if (cmd === 'fs.writeTextFile' && args.path === logPath) { logWrites++; return true; }
      if (cmd === 'shell.run') {
        shellCalls++;
        // Inert endpoint: the generated script is never evaluated or started.
        return { exitCode: 0, stdout: 'SAFE_SKIP:bridge_conflict\nSAFE_FALLBACK:1\n', stderr: '' };
      }
      assert(args.path === virtualPath, `unexpected or unowned path at ${cmd}`);
      requests.push({ cmd, maxBytes: args.maxBytes, offset: args.offset });
      if (cmd === 'fs.exists') {
        existsCalls++;
        if (active.fault === 'exists') throw new Error('OWNED_EXISTS_ACCESS_DENIED');
        if (Object.hasOwn(active, 'existsValue')) return active.existsValue;
        const exists = nodeFs.existsSync(file);
        if (!exists && active.appearAfterMissing && existsCalls === 1) {
          nodeFs.writeFileSync(file, active.appearAfterMissing); externallyPublished = digest();
        }
        if (exists && active.disappearAfterExists && existsCalls === 1) {
          nodeFs.unlinkSync(file); externallyPublished = 'missing';
        }
        return exists;
      }
      if (cmd !== 'fs.readTextFile' && cmd !== 'fs.readTextRange') throw new Error(`FORBIDDEN_PORT:${cmd}`);
      readCalls++;
      if (active.fault === 'read' || active.fault === 'lock') throw new Error(active.fault === 'lock' ? 'OWNED_SHARING_VIOLATION' : 'OWNED_READ_IO_ERROR');
      const bytes = nodeFs.readFileSync(file);
      let content = active.decoded ?? bytes.subarray(0, cmd === 'fs.readTextRange' ? args.maxBytes : bytes.length).toString('utf8');
      if (active.fault === 'partial') content = 'another';
      if (active.changeAfterRead && readCalls === 1) {
        nodeFs.writeFileSync(file, active.changeAfterRead); externallyPublished = digest();
      }
      if (cmd === 'fs.readTextFile') return content; // Original native reader ignores maxBytes outside windows-config.
      if (active.fault === 'receipt-null') return null;
      return {
        content: active.fault === 'receipt-type' ? {} : content,
        offset: active.fault === 'receipt-offset' ? 1 : 0,
        size: active.fault === 'receipt-size' ? String(bytes.length) : active.fault === 'receipt-large' ? limit + 1 : active.decoded !== undefined ? Buffer.byteLength(active.decoded) : bytes.length,
      };
    };
    try {
      const consume = async (expected: 'block' | 'allow', label: string) => {
        existsCalls = 0; readCalls = 0; shellCalls = 0; logWrites = 0; externallyPublished = undefined; requests.length = 0;
        valvePid = spec.minecraft ? 111 : 222;
        const original = digest();
        const result = await speedhack.applyGameSpeed(valvePid, 2, { pid: valvePid, name: 'caller-cannot-authorize.exe' });
        if (expected === 'block') {
          check(result.ok && result.skipped && result.safeFallback && result.reason === 'blocked_target', `${label}: unknown/blocked list must return safe compatibility skip`);
          check(shellCalls === 0, `${label}: blocked list must not reach the shell port`);
        } else {
          check(result.ok && result.skipped && result.safeFallback && result.reason === 'bridge_conflict', `${label}: valid/missing list must reach only the inert compatibility endpoint`);
          check(shellCalls === 1, `${label}: valid nonmatch must be consumed by the existing apply chain`);
        }
        check(digest() === (externallyPublished ?? original), `${label}: consumer must preserve the original list`);
        if (spec.minecraft) check(readCalls === 0 && existsCalls === 0, 'builtin Minecraft/java gate must remain first');
        steps.push({ label, expected, result, existsCalls, readCalls, shellCalls, logWrites, requests: [...requests], originalSha256: original, afterSha256: digest(), externalPublication: externallyPublished ?? null });
      };
      await consume(spec.expected, 'initial');
      if (spec.retry) {
        active = { name: spec.name, text: spec.retry.text, expected: spec.retry.expected };
        assert(!spec.directory, 'retry directory unsupported');
        if (spec.retry.text === undefined) { if (nodeFs.existsSync(file)) nodeFs.unlinkSync(file); }
        else nodeFs.writeFileSync(file, spec.retry.text);
        await consume(spec.retry.expected, 'explicit-retry-after-repair');
      }
    } catch (error: any) { fixtureErrors++; errors.push(`FIXTURE_ERROR:${String(error?.stack || error)}`); }
    cases.push({ name: spec.name, file, pass: errors.length === 0, errors, steps });
  }
  const result = {
    status: cases.every(item => item.pass) ? 'PASS_LIMITED_BLOCKLIST_SOFTWARE' : 'FAIL',
    consumer: 'tools/speedhack_selftest.ts --blocklist-storm', selection: selected || 'all blocklist cases',
    pid: process.pid, cases: cases.length, passed: cases.filter(item => item.pass).length,
    failed: cases.filter(item => !item.pass).length, assertions, fixtureErrors, details: cases,
    realActions: { files: 'owned artifact directory only', shellExecution: 0, processControl: 0, injection: 0, network: 0 },
    limits: ['Win32 IPC and native decoder not executed; native read/size receipt modeled at the original IPC seam.', 'Sharing/read failures are injected rejection ports; physical disk/full and actual native file locks are not accepted.', 'No blocklist writer or format version exists here; unknown entries remain literal. A stable syntactically valid partial publication and changes after final comparison are not detectable as complete CAS.'],
  };
  nodeFs.writeFileSync(path.join(root, 'RESULT.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(`OpenSpeedy blocklist selection: ${result.passed}/${result.cases} passed; ${assertions} assertions; fixtureErrors=${fixtureErrors}`);
  if (result.failed) process.exitCode = 1;
}


// Original speed safety consumer, optional changed-source ownership leaf only.
async function mainUncertainOwnership(){
 // The original speed consumer installs an inert document. Vue's module load
 // only needs an inert template element here; no DOM mount/input is executed.
 (globalThis as any).document.createElement=(tag:string)=>({tagName:tag.toUpperCase(),content:{}});
 const nodeFs=await import('node:fs'),nodePath=await import('node:path'),nodeVm=await import('node:vm'),nodeModule=await import('node:module'),nodeAssert=(await import('node:assert/strict')).default;
 const req=nodeModule.createRequire(nodePath.join(process.cwd(),'package.json')),esbuild=req('esbuild');
 const resetOnly=process.argv.includes('--ownership-case=reset-after-uncertain');
 if(process.argv.some(a=>a.startsWith('--ownership-case=')&&a!=='--ownership-case=reset-after-uncertain'))throw Error('Unknown exact original ownership leaf');
 const results:any[]=[];
 function load(name:string,deps:any={}){const module={exports:{}};nodeVm.runInNewContext(esbuild.transformSync(nodeFs.readFileSync(nodePath.join(process.cwd(),name),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,TextEncoder,TextDecoder,require:(name:string)=>{nodeAssert(Object.hasOwn(deps,name),'Unexpected dependency '+name);return deps[name];}});return module.exports as any;}
 function create(){
  let generation=1,selected:any={pid:222,name:'safe-game.exe',title:'Fixture',path:'C:/Fixture/safe-game.exe',processCreated:'222007',identityGeneration:1},owned=structuredClone(selected),inventoryKnown=true,inventoryPresent=true,invalidated=false,makeUncertain=true,cleanupFails=false,hooked=false,globalFactor=1;
  const gameObservers=new Set<any>(),wakeObservers=new Set<any>(),files=new Map<string,string>(),scripts:any[]=[];
  const api=load('src/bridge/api.ts',{'./ipc':{invoke:async(command:string,args:any)=>{
   if(command==='fs.exists')return files.has(args.path)||args.path.toLowerCase().includes('\\openspeedy\\');
   if(command==='fs.readTextFile'){nodeAssert(files.has(args.path));return files.get(args.path);}
   if(command==='fs.writeTextFile'){nodeAssert(args.path.endsWith('speedhack.log'));files.set(args.path,args.content);return true;}
   if(command==='power.lifecycle')return {generation,phase:'ready',hardwareWritesAllowed:true};
   if(command==='proc.identities')return {known:inventoryKnown,processes:inventoryKnown&&inventoryPresent?[structuredClone(owned)]:[]};
   nodeAssert.equal(command,'shell.run');const script=args.args[2],cleanup=script.includes('# Owned cleanup'),apply=script.includes('action=apply:')||script.includes('TARGET_MAPPING_BEFORE:');
   if(cleanup){const tx=script.slice(script.indexOf('# Owned cleanup'),script.indexOf('$pipe.Close()'));nodeAssert(tx.includes("'DISABLE 222'"));nodeAssert(!tx.includes('SETSPEED')&&!tx.includes('INJECT'));scripts.push({kind:'cleanup',globalSet:false});if(cleanupFails)return {exitCode:2,stdout:'RESULT:failed',stderr:''};hooked=false;}
   else if(apply){scripts.push({kind:'apply',factor:2});hooked=true;globalFactor=2;if(makeUncertain&&!invalidated){generation++;invalidated=true;}}
   else{scripts.push({kind:'reset'});globalFactor=1;}
   return {exitCode:0,stdout:'RESULT:ok',stderr:''};
  }}});
  const owner=load('src/bridge/speedhack.ts',{'./api':api,'./ipc':{on:(_event:string,fn:any)=>{wakeObservers.add(fn);return()=>wakeObservers.delete(fn);}},'./gamedetect':{detectGame:async()=>selected?structuredClone(selected):null,subscribeGameStatus:(fn:any)=>{gameObservers.add(fn);fn(selected);return()=>gameObservers.delete(fn);}}});
  const key=load('src/bridge/gamePolicyHysteresis.ts'),game=load('src/bridge/deckyGameActions.ts',{'./gamePolicyHysteresis':key,'./virtualTargetPolicyState':{},'./virtualTargetPolicy':{}}),adapter=load('src/bridge/deckySpeedActions.ts',{'./deckyGameActions':game});let releases=0;
  const actions=adapter.createSpeedMirrorActions({featureAllowed:()=>true,game:()=>selected,power:()=>api.powerLifecycle.get(),nativeState:async()=>({active:true,connected:true,connection:'73',runId:'c'.repeat(32),connectionRevision:73,powerGeneration:generation,hardwareWritesAllowed:true}),acquire:()=>()=>releases++,presets:owner.SPEED_PRESETS,blocked:owner.isMinecraftTarget,state:owner.getGameSpeedState,apply:(target:any,factor:number,admission:any)=>owner.applyGameSpeed(target.pid,factor,target,'user-factor',admission),clear:(target:any,admission:any)=>owner.clearGameSpeed(target.pid,'user-reset',admission)});
  const intent=(factor:string)=>actions.execute({field:'speedFactor',value:factor},{generation,revision:1,nativePeer:{connection:'73',runId:'c'.repeat(32),connectionRevision:73},gameIdentity:game.gameMirrorIdentity(selected),gameAdmission:{speed:JSON.stringify(owner.getGameSpeedState())},checkpoint:()=>{}});
  return {owner,intent,scripts,get hooked(){return hooked;},get factor(){return globalFactor;},get releases(){return releases;},gameObservers,wakeObservers,setUncertain:(value:boolean)=>makeUncertain=value,setInventory:(known:boolean,present=true)=>{inventoryKnown=known;inventoryPresent=present;},setCleanupFails:(value:boolean)=>cleanupFails=value,replaceBirth:()=>{owned={...owned,processCreated:'replacement'};},switchTarget:()=>{selected={...selected,pid:333,processCreated:'333007',path:'C:/Fixture/other.exe',identityGeneration:2};for(const fn of [...gameObservers])fn(selected);},exitTarget:()=>{selected=null;for(const fn of [...gameObservers])fn(null);}};
 }
 const flush=async()=>{for(let i=0;i<500;i++)await Promise.resolve();};
 async function pending(f:any){const r=await f.intent('2');nodeAssert.equal(r.applied,false);nodeAssert.equal(r.pending,true);nodeAssert.equal(f.hooked,true);nodeAssert.equal(f.factor,2);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,true);nodeAssert.equal(f.owner.getGameSpeedState().enabled,false);nodeAssert.equal(f.releases,1);}
 {
  const f=create();await pending(f);const before=f.scripts.length;const blocked=await f.owner.applyGameSpeed(222,4,{pid:222,processCreated:'222007'});nodeAssert.equal(blocked.ok,false);nodeAssert.equal(f.scripts.length,before);const url=await import('node:url');const topFixture=await import(url.pathToFileURL(nodePath.join(process.cwd(),'tools/fixtures/decky_lossless_memory_owner.mjs')).href);
  const ui=topFixture.createLosslessTopActions(topFixture.createOriginalLosslessMemoryOwner(),{pid:222,name:'safe-game.exe',title:'Fixture',path:'C:/Fixture/safe-game.exe',processCreated:'222007',identityGeneration:1},f.owner);
  try{await ui.raw.refreshSpeedState(ui.raw.policyGame.value);nodeAssert.equal(ui.raw.speedFactor.value,'unknown','Pending is not a confirmed1x');await ui.raw.runSpeed(1);nodeAssert.equal(ui.raw.speedFactor.value,1);}finally{ui.stop();}
  nodeAssert.equal(f.scripts.filter((s:any)=>s.kind==='reset').length,1,'Actual Y handler reaches original reset exactly once');nodeAssert.equal(f.owner.getGameSpeedState().factor,1);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,undefined);nodeAssert.equal(f.factor,1);nodeAssert.equal(f.gameObservers.size,1,'Confirmed1x retains hook ownership');
  f.switchTarget();await flush();nodeAssert.equal(f.scripts.filter((s:any)=>s.kind==='cleanup').length,1);nodeAssert.equal(f.factor,1);nodeAssert.equal(f.owner.getGameSpeedState(),null);nodeAssert.equal(f.gameObservers.size,0);nodeAssert.equal(f.wakeObservers.size,0);results.push('uncertain target retained; actual Y1x resets once, hook ownership survives until global switch DISABLE');
 }
 if(!resetOnly){
  const f=create();await pending(f);f.switchTarget();await flush();nodeAssert.equal(f.hooked,false);nodeAssert.equal(f.scripts.filter((s:any)=>s.kind==='cleanup').length,1);nodeAssert.equal(f.factor,2,'Target cleanup never changes shared factor');nodeAssert.equal(f.owner.getGameSpeedState(),null);nodeAssert.equal(f.gameObservers.size,0);results.push('global game switch consumes exact uncertain target with DISABLE only');
 }
 if(!resetOnly){
  const f=create();await pending(f);f.setInventory(true,false);f.exitTarget();await flush();nodeAssert.equal(f.scripts.length,1);nodeAssert.equal(f.owner.getGameSpeedState(),null);nodeAssert.equal(f.gameObservers.size,0);results.push('verified target exit returns ownership without sending a command');
 }
 if(!resetOnly){
  const f=create();await pending(f);f.replaceBirth();f.switchTarget();await flush();nodeAssert.equal(f.scripts.length,1);nodeAssert.equal(f.owner.getGameSpeedState(),null);results.push('PID reuse cannot disable a different process instance');
 }
 if(!resetOnly){
  const f=create();await pending(f);f.setInventory(false);f.switchTarget();await flush();nodeAssert.equal(f.scripts.length,1);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,true);for(let i=0;i<100;i++)f.switchTarget();await flush();nodeAssert.equal(f.scripts.length,1);results.push('unreadable inventory retains ownership; repeated notifications do not retry');
 }
 if(!resetOnly){
  const f=create();await pending(f);f.setCleanupFails(true);f.switchTarget();await flush();nodeAssert.equal(f.scripts.length,2);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,true);for(let i=0;i<100;i++)f.switchTarget();await flush();nodeAssert.equal(f.scripts.length,2);f.setCleanupFails(false);nodeAssert.equal((await f.owner.cleanupOwnedSpeedTarget()).ok,true);nodeAssert.equal(f.owner.getGameSpeedState(),null);results.push('uncertain cleanup refusal retains owner and explicit retry never repeats old apply');
 }
 if(!resetOnly){
  const f=create();f.setUncertain(false);const normal=await f.intent('2');nodeAssert.equal(normal.applied,true);nodeAssert.equal(f.owner.getGameSpeedState().factor,2);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,undefined);nodeAssert.equal((await f.owner.cleanupOwnedSpeedTarget()).ok,true);nodeAssert.equal(f.hooked,false);nodeAssert.equal(f.owner.getGameSpeedState(),null);nodeAssert.equal(f.gameObservers.size,0);nodeAssert.equal(f.wakeObservers.size,0);results.push('normal successful session uses original targeted cleanup and releases observers');
 }
 if(!resetOnly){
  const f=create();await pending(f);const before=f.scripts.length;nodeAssert.equal((await f.owner.clearGameSpeed(999,'user-reset')).skipped,true);nodeAssert.equal(f.scripts.length,before);nodeAssert.equal(f.owner.getGameSpeedState().cleanupPending,true);results.push('wrong target reset preserves uncertain ownership and never dispatches');
 }
 const report={status:'PASS_UNCERTAIN_OWNERSHIP_ORIGINAL_OWNER_LEAF',canonicalConsumer:'tools/speedhack_selftest.ts',selection:resetOnly?'uncertain-ownership/reset-after-uncertain':'uncertain-ownership',skippedOwnershipCases:resetOnly?7:0,cases:results.length,results,realWindowsShell:0,realInjection:0,newPollingTimers:0,limits:['Original production owner/adapter/API executed; file/process/native ports inert.','Global YMCC native exit cleanup has no implemented registration/consumer here; all exits remain unaccepted.','No full CPP/type/Full/build/package/install or old6/13 group rerun.']};
 const artifact=process.argv.find(a=>a.startsWith('--artifact-root='))?.slice('--artifact-root='.length);nodeAssert(artifact);nodeFs.writeFileSync(nodePath.join(artifact,'RESULT.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}

(uncertainOwnershipOnly ? mainUncertainOwnership() : blocklistStorm ? mainBlocklistStorm() : main()).catch((error) => {
  console.error(error);
  process.exit(1);
});
