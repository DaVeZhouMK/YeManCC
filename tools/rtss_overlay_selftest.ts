import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { rtssIniValue, setRtssIniValue } from '@/bridge/rtssOverlay';

const dir = 'C:\\TestRTSS';
const cfg = `${dir}\\Plugins\\Client\\OverlayEditor.cfg`;
let running = true;
let rtssInstalled = false;
let failPrepare = false, failLoad = false;
let dropPrepareReply = false, dropLoadReply = false;
let compatibleLoadedBridge = false;
// Exercise the real IPC timeout/cleanup code with accelerated test timers.
const realSetTimeout = globalThis.setTimeout;
(globalThis as any).setTimeout = (fn: (...args: any[]) => void, ms?: number, ...args: any[]) =>
  realSetTimeout(fn, ms === 15000 || ms === 6000 ? 40 : ms, ...args);
let text = '[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\nSMART=0\r\n[Other]\r\nLayout=untouched.ovl';
const calls: Array<{cmd: string; args: any}> = [];
let handler: ((e: {data: unknown}) => void) | null = null;
let active = 0, maxActive = 0;
(globalThis as any).window = {
  chrome: {webview: {
    addEventListener: (_type: string, fn: typeof handler) => { handler = fn; },
    postMessage: (message: any) => {
      calls.push({cmd: message.cmd, args: message.args});
      if ((message.cmd === 'rtss.prepareOverlay' && dropPrepareReply) ||
          (message.cmd === 'rtss.loadOverlay' && dropLoadReply)) return;
      void (async () => {
        let result: unknown;
        switch (message.cmd) {
          case 'shell.run': result = {exitCode: 0, stdout: rtssInstalled ? dir : '', stderr: ''}; break;
          case 'fs.exists': result = rtssInstalled; break;
          case 'proc.running': result = {RTSS: running}; break;
          case 'rtss.prepareOverlay': result = {ok: !failPrepare, error: 'prepare-failed',
            ...(failPrepare ? {win32Error: 5, path: dir + '\\Plugins\\Client\\YMCCOverlayBridge.dll'} : {}),
            ...(compatibleLoadedBridge ? {bridgeState: 'loaded_compatible', bridgeMaintenanceDeferred: true} : {})}; break;
          case 'rtss.loadOverlay':
            active++; maxActive = Math.max(maxActive, active);
            await new Promise(resolve => setTimeout(resolve, 5));
            active--;
            result = {ok: !failLoad, error: 'load-unconfirmed'};
            if (!failLoad) text = setRtssIniValue(text, 'Settings', 'Layout', message.args.layout);
            break;
          case 'fs.readTextFile': result = text; break;
          case 'fs.writeTextFileAtomic': text = message.args.content; result = true; break;
          default: throw new Error(`unexpected IPC ${message.cmd}`);
        }
        handler!({data: {id: message.id, result}});
      })();
    },
  }},
  dispatchEvent: () => {},
};

async function main() {
  const {setOverlayLayout, readOverlayLayout, setPowerControlDir, ensureRtssOverlayInstalled} = await import('@/bridge/yeman');
  setPowerControlDir('C:\\TestYMCC\\PowerControl');
  assert.equal(await ensureRtssOverlayInstalled(), false, 'optional RTSS absence must not fail YMCC startup');
  assert(!calls.some(c => c.cmd === 'rtss.prepareOverlay'), 'no filesystem provisioning when RTSS is not installed');
  rtssInstalled = true;
  failPrepare = true;
  await assert.rejects(ensureRtssOverlayInstalled(), /prepare-failed/, 'real installation failures remain observable');
  failPrepare = false;
  const beforeStartup = text;
  assert.equal(await ensureRtssOverlayInstalled(), true, 'startup provisions bridge independently of monitoring toggle');
  assert.equal(text, beforeStartup, 'startup installation preserves selected Layout');
  const startupPrepare = calls.find(c => c.cmd === 'rtss.prepareOverlay');
  assert.deepEqual(startupPrepare?.args, {dir, powerControlDir: 'C:\\TestYMCC\\PowerControl'});
  assert(!calls.some(c => c.cmd === 'rtss.loadOverlay' || c.cmd === 'proc.running' || c.cmd === 'fs.writeTextFileAtomic'), 'startup installation never loads a layout or starts RTSS');
  running = false;
  assert.equal(await ensureRtssOverlayInstalled(), true, 'provisioning works while RTSS is stopped');
  running = true;
  await Promise.all([ensureRtssOverlayInstalled(), ensureRtssOverlayInstalled()]);
  const original = '[Settings]\nLayout=old\n; keep\n[Other]\nLayout=external\n';
  assert.equal(rtssIniValue(original, 'SETTINGS', 'layout'), 'old');
  const edited = setRtssIniValue(original, 'Settings', 'Layout', 'new');
  assert.equal(rtssIniValue(edited, 'Settings', 'Layout'), 'new');
  assert.equal(rtssIniValue(edited, 'Other', 'Layout'), 'external');
  assert(edited.includes('; keep'));
  assert.equal(rtssIniValue(setRtssIniValue('[Other]\nLayout=other', 'Settings', 'Layout', 'new'), 'Settings', 'Layout'), 'new');
  assert.equal((setRtssIniValue('[Settings]\nLayout=one\nLayout=two', 'Settings', 'Layout', 'new').match(/Layout=new/g) ?? []).length, 1);

  assert.deepEqual(await setOverlayLayout('L'), {mode: 'live'});
  assert.equal(await readOverlayLayout(), 'YeManOBS-L-1.ovl');
  assert.equal(calls.filter(c => c.cmd === 'fs.writeTextFileAtomic').length, 0);
  const snapshot = text;
  failLoad = true;
  await assert.rejects(setOverlayLayout('J'), /load-unconfirmed/);
  assert.equal(text, snapshot, 'failed live request must not persist selected Layout');
  failLoad = false;
  await Promise.all(['W', 'J', 'off', 'L'].map(layout => setOverlayLayout(layout as any)));
  assert.equal(maxActive, 1, 'rapid clicks must never overlap official Load queues');
  await Promise.all([ensureRtssOverlayInstalled(), setOverlayLayout('L')]);
  assert.equal(maxActive, 1, 'startup repair and user switch use the same serial queue');
  assert.deepEqual(calls.filter(c => c.cmd === 'rtss.loadOverlay').slice(-5, -1).map(c => c.args.layout),
    ['YeManOBS-W-1.ovl', 'YeManOBS-JJ-1.ovl', 'Empty.ovl', 'YeManOBS-L-1.ovl']);
  failPrepare = true;
  await assert.rejects(setOverlayLayout('W'), /prepare-failed/);
  failPrepare = false;
  assert.deepEqual(await setOverlayLayout('W'), {mode: 'live'}, 'rejected queue must recover');
  compatibleLoadedBridge = true;
  assert.deepEqual(await setOverlayLayout('L'), {mode: 'live'}, 'deferred DLL maintenance does not block compatible live bridge');
  compatibleLoadedBridge = false;
  failPrepare = true;
  await assert.rejects(setOverlayLayout('W'), /Win32 5.*YMCCOverlayBridge\.dll/, 'installation failures display real code and file path');
  failPrepare = false;
  let snapshotBeforeTimeout = text;
  dropPrepareReply = true;
  await assert.rejects(setOverlayLayout('W'), /timed out|timeout/i, 'unanswered preparation has bounded IPC timeout');
  dropPrepareReply = false;
  assert.equal(text, snapshotBeforeTimeout, 'preparation timeout does not change selection');
  assert.deepEqual(await setOverlayLayout('W'), {mode: 'live'}, 'switch queue recovers after preparation timeout');
  snapshotBeforeTimeout = text;
  dropLoadReply = true;
  await assert.rejects(setOverlayLayout('J'), /timed out|timeout/i, 'unanswered live load has bounded IPC timeout');
  dropLoadReply = false;
  assert.equal(text, snapshotBeforeTimeout, 'load timeout does not optimistically persist selection');
  assert.deepEqual(await setOverlayLayout('L'), {mode: 'live'}, 'switch queue recovers after live-load timeout');

  running = false;
  const loads = calls.filter(c => c.cmd === 'rtss.loadOverlay').length;
  assert.deepEqual(await setOverlayLayout('off'), {mode: 'saved'});
  assert.equal(calls.filter(c => c.cmd === 'rtss.loadOverlay').length, loads);
  assert.equal(rtssIniValue(text, 'Settings', 'Layout'), 'Empty.ovl');
  assert.equal(rtssIniValue(text, 'Other', 'Layout'), 'untouched.ovl');
  assert.equal(calls.find(c => c.cmd === 'fs.writeTextFileAtomic')?.args.path, cfg);
  assert(!calls.some(c => /triggerOverlayHotkey|shell.hidden|shell.execute/.test(c.cmd)), 'never restart/inject keys');
  const view = readFileSync('src/views/RtssView.vue', 'utf8');
  assert(/finally\s*\{\s*busy\.value = false;/.test(view), 'UI releases busy state after success, failure or timeout');
  assert(view.includes(':model-value="overlay"') && !view.includes('v-model="overlay"'), 'no optimistic template selection');
  assert(view.includes("enable ? overlay.value : 'off'"), 're-enable preserves selected template');
  assert(!readFileSync('native/main.cpp', 'utf8').includes('nativeRtssInvokeProfiles'), 'no hook SDK in YMCC');
  const app = readFileSync('src/App.vue', 'utf8');
  const startupCall = app.indexOf('void ensureRtssOverlayInstalled().catch(');
  assert(startupCall > app.indexOf('setPowerControlDir(dir);'), 'startup repair runs only after native PowerControl path resolution');
  assert(startupCall > app.indexOf('if (isStandaloneEditor.value)'), 'standalone renderer does not provision plugins');
  assert(app.includes("cmd: 'rtss.startup-install'"), 'optional startup failures reach the debug log');
  const packageScript = readFileSync('tools/package-release.ps1', 'utf8');
  assert(packageScript.includes("$StagingYeManCC 'YMCCOverlayBridge.dll'"), 'release retains bridge beside YMCC.exe');
  assert(/\$requiredProgram[^\n]*'YMCCOverlayBridge\.dll'/.test(packageScript), 'missing release DLL fails the package gate');
  assert(readFileSync('tools/build-workspace.ps1', 'utf8').includes("$NativeBuild 'YMCCOverlayBridge.dll'"), 'native build requires bridge artifact');
  assert(readFileSync('tools/deploy-installed.ps1', 'utf8').includes("$InstallRoot 'YMCCOverlayBridge.dll'"), 'local deployment retains automatic-repair source');
  console.log('RTSS_OVERLAY_SELFTEST_OK (compatible bridge maintenance deferral, bounded timeout recovery, startup auto-install, missing RTSS, failure recovery, package retention, INI scope, live ACK, serial clicks, process isolation)');
}
main().catch(e => { console.error(e); process.exitCode = 1; }).finally(() => { globalThis.setTimeout = realSetTimeout; });
