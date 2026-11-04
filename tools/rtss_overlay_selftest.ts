import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { rtssIniValue, setRtssIniValue } from '@/bridge/rtssOverlay';

const dir = 'C:\\TestRTSS';
const cfg = `${dir}\\Plugins\\Client\\OverlayEditor.cfg`;
let running = true;
let rtssInstalled = false;
let failPrepare = false, failLoad = false;
let prepareFailure: Record<string, unknown> | null = null, loadFailure: Record<string, unknown> | null = null;
let dropPrepareReply = false, dropLoadReply = false;
let compatibleLoadedBridge = false;
let requireWindowsConfigRead = false, rejectWrite = false, staleWrite = false, configDisappears = false, ignoreWrite = false;
let failReload = false, reloadWritesBack = false;
let atomicReplyHook: null | (() => Promise<void>) = null;
let completedReloads = 0;
let omitZoomOnReload = false, blockReload: Promise<void> | null = null, configOperationActive = 0, configOperationMax = 0;
// Exercise the real IPC timeout/cleanup code with accelerated test timers.
const realSetTimeout = globalThis.setTimeout;
const nativeEvents = new EventTarget();
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
            ...(compatibleLoadedBridge ? {bridgeState: 'loaded_compatible', bridgeMaintenanceDeferred: true} : {}), ...(prepareFailure ?? {})}; break;
          case 'rtss.loadOverlay':
            active++; maxActive = Math.max(maxActive, active);
            await new Promise(resolve => setTimeout(resolve, 5));
            active--;
            result = {ok: !failLoad, error: 'load-unconfirmed', ...(loadFailure ?? {})};
            if (!failLoad) text = setRtssIniValue(text, 'Settings', 'Layout', message.args.layout);
            break;
          case 'fs.readTextFile':
            if (message.args.path !== cfg && message.args.path !== dir+'\\Profiles\\Global') { handler!({data:{id:message.id,error:'unexpected RTSS config path'}});return; }
            if (requireWindowsConfigRead && message.args.textEncoding !== 'windows-config') { handler!({data:{id:message.id,error:'NATIVE_RESPONSE_SERIALIZATION_FAILED'}});return; }
            result = text; break;
          case 'fs.writeTextFileAtomic':
            if (message.args.path !== cfg && message.args.path !== dir+'\\Profiles\\Global') { handler!({data:{id:message.id,error:'unexpected RTSS config path'}});return; }
            if (staleWrite || configDisappears) { handler!({data:{id:message.id,error:staleWrite?'CONFIG_TEXT_CHANGED':'CONFIG_TEXT_SOURCE_MISSING'}});return; }
            if (!rejectWrite && !ignoreWrite) text = message.args.content; result = !rejectWrite; if(atomicReplyHook){const hook=atomicReplyHook;atomicReplyHook=null;await hook();} break;
          case 'rtss.reloadProfiles': completedReloads++;configOperationActive++;configOperationMax=Math.max(configOperationMax,configOperationActive);if(blockReload)await blockReload;configOperationActive--;if(omitZoomOnReload)text='[Other]\r\nZoomRatio=5\r\n';if(reloadWritesBack)text=setRtssIniValue(text,'OSD','ZoomRatio','2');result={ok:!failReload,error:'reload-unconfirmed'};break;
          default: throw new Error(`unexpected IPC ${message.cmd}`);
        }
        handler!({data: {id: message.id, result}});
      })();
    },
  }},
  addEventListener: nativeEvents.addEventListener.bind(nativeEvents),
  removeEventListener: nativeEvents.removeEventListener.bind(nativeEvents),
  dispatchEvent: nativeEvents.dispatchEvent.bind(nativeEvents),
};

async function main() {
  if(process.argv.includes('--atomic-reload-cancellation-only')){
    const {setRtssLimit,setPowerControlDir}=await import('@/bridge/yeman');
    setPowerControlDir('C:/TestYMCC/PowerControl');rtssInstalled=true;
    const results:any[]=[];
    for(const kind of ['stable','cancel-after-atomic-commit-before-ack','cancel-before-queue']){
      text='[Framerate]\r\nLimit=75\r\n';completedReloads=0;calls.length=0;
      let admitted=kind!=='cancel-before-queue',released:()=>void=()=>{},reached:()=>void=()=>{};
      let observed:any,pending:Promise<void>|undefined;
      try{
        if(kind==='cancel-after-atomic-commit-before-ack'){
          const hold=new Promise<void>(resolve=>{released=resolve}),entered=new Promise<void>(resolve=>{reached=resolve});
          atomicReplyHook=async()=>{reached();await hold;};
          pending=setRtssLimit(30,()=>admitted);
          let boundaryTimer: ReturnType<typeof realSetTimeout> | undefined;
          try { await Promise.race([entered,pending.then(()=>{throw Error('ATOMIC_ACK_BOUNDARY_NOT_REACHED');}),new Promise((_,reject)=>{boundaryTimer=realSetTimeout(()=>reject(Error('ATOMIC_ACK_BOUNDARY_NOT_REACHED')),1200);})]); }
          finally { if(boundaryTimer!==undefined)clearTimeout(boundaryTimer); }
          assert(text.includes('Limit=30'),'atomic data committed before its held ACK');
          assert.equal(completedReloads,0,'reload cannot precede atomic ACK');
          admitted=false;released();await pending;
        }else await setRtssLimit(30,()=>admitted);
        observed={reloads:completedReloads,atomicWrites:calls.filter(call=>call.cmd==='fs.writeTextFileAtomic').length,text,commands:calls.map(call=>call.cmd)};
        if(kind==='stable'){assert.equal(completedReloads,1);assert.equal(observed.atomicWrites,1);assert(text.includes('Limit=30'));}
        else if(kind==='cancel-before-queue'){assert.equal(completedReloads,0);assert.equal(observed.atomicWrites,0);assert(text.includes('Limit=75'));}
        else {assert.equal(completedReloads,0,'revoked atomic ACK must not dispatch reload');assert.equal(observed.atomicWrites,1);assert(text.includes('Limit=30'),'accepted atomic data stays committed; no rollback');}
        results.push({kind,status:'PASS',observed});
      }catch(error:any){results.push({kind,status:'FAIL',observed,error:{name:error.name,message:error.message,code:error.code,stack:error.stack}});}
      finally{released();if(pending)await pending.catch(()=>{});atomicReplyHook=null;}
    }
    const result={suite:'rtss_overlay_selftest',option:'--atomic-reload-cancellation-only',selected:results.length,passed:results.filter(row=>row.status==='PASS').length,failed:results.filter(row=>row.status==='FAIL').length,cases:results,sourceBindings:['src/bridge/yeman.ts','src/bridge/api.ts','src/bridge/ipc.ts','src/bridge/rtssOverlay.ts'].map(file=>({file,sha256:createHash('sha256').update(readFileSync(file)).digest('hex')})),identityClosureComplete:false,actualYemanApiIpc:true,nativeBoundary:'original in-memory WebView IPC transport; no native shell or RTSS',fullOriginalSuiteExecuted:false};
    const output=process.env.YEMAN_RTSS_AWAIT_TEST_OUT;if(!output)throw Error('OWNED_TEST_OUTPUT_REQUIRED');mkdirSync(output,{recursive:true});writeFileSync(output+'/rtss-results.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({suite:result.suite,option:result.option,passed:result.passed,failed:result.failed}));if(result.failed)process.exitCode=1;return;
  }

  const {setOverlayLayout, readOverlayLayout, readRtssZoom, setRtssZoom, setPowerControlDir, ensureRtssOverlayInstalled} = await import('@/bridge/yeman');
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
  const configReads = calls.filter(c => c.cmd === 'fs.readTextFile' && c.args.path === cfg);
  assert(configReads.length > 0 && configReads.every(c => c.args.textEncoding === 'windows-config'), 'RTSS layout read opts into explicit bounded Windows text decode');
  const beforeZoom = text;
  text = '[OSD]\r\nZoomRatio=2\r\n';
  assert.equal(await readRtssZoom(), 2, 'decoded RTSS zoom still uses actual configured value');
  assert.equal(calls.filter(c => c.cmd === 'fs.readTextFile').at(-1)?.args.textEncoding, 'windows-config', 'RTSS zoom read opts into same decoder');
  text = beforeZoom;
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
  const operationCases: Array<{name:string;ok:boolean;error?:string}> = [];
  const operationCase=async(name:string,run:()=>Promise<void>)=>{try{await run();operationCases.push({name,ok:true});}catch(error){operationCases.push({name,ok:false,error:String(error)});}finally{prepareFailure=null;loadFailure=null;failPrepare=false;failLoad=false;requireWindowsConfigRead=false;rejectWrite=false;staleWrite=false;configDisappears=false;ignoreWrite=false;failReload=false;reloadWritesBack=false;omitZoomOnReload=false;blockReload=null;}};
  await operationCase('native load failure preserves diagnostic code path and OS error without writing selection',async()=>{
    running=true;text='[Settings]\r\nLayout=Empty.ovl\r\n';const before=text,start=calls.length;
    failLoad=true;loadFailure={error:'config preparation failed',errorCode:'RTSS_CONFIG_READ_FAILED',win32Error:32,path:cfg};
    let message='';try{await setOverlayLayout('W');assert.fail('expected load failure');}catch(error){message=String(error);}
    assert(message.includes('config preparation failed'));assert(message.includes('RTSS_CONFIG_READ_FAILED'));assert(message.includes('Win32 32'));assert(message.includes(cfg));assert.equal(text,before);assert.equal(calls.slice(start).filter(c=>c.cmd==='rtss.loadOverlay').length,1);assert(!calls.slice(start).some(c=>c.cmd==='fs.writeTextFileAtomic'||c.cmd==='rtss.reloadProfiles'));
    failLoad=false;loadFailure=null;assert.deepEqual(await setOverlayLayout('W'),{mode:'live'},'same serialized queue recovers after structured failure');
  });
  await operationCase('native post-ACK mismatch and bridge error keep distinct diagnostic details',async()=>{
    running=true;failLoad=true;loadFailure={error:'raw readback unconfirmed',errorCode:'RTSS_CONFIG_LAYOUT_MISMATCH',path:cfg};
    await assert.rejects(setOverlayLayout('L'),error=>String(error).includes('RTSS_CONFIG_LAYOUT_MISMATCH')&&String(error).includes(cfg));
    loadFailure={error:'bridge rejected',bridgeCode:11};await assert.rejects(setOverlayLayout('J'),error=>String(error).includes('bridge rejected')&&String(error).includes('11'));
  });
  await operationCase('native prepare failures retain protocol code and path before any load',async()=>{
    running=true;failPrepare=true;prepareFailure={error:'bridge preparation failed',errorCode:'RTSS_BRIDGE_UNRESPONSIVE',bridgeCode:10,win32Error:1460,path:cfg};const count=calls.filter(c=>c.cmd==='rtss.loadOverlay').length;
    await assert.rejects(setOverlayLayout('W'),error=>['RTSS_BRIDGE_UNRESPONSIVE','10','Win32 1460',cfg].every(part=>String(error).includes(part)));assert.equal(calls.filter(c=>c.cmd==='rtss.loadOverlay').length,count);
  });
  await operationCase('generic native failure remains readable without undefined metadata',async()=>{
    running=true;failLoad=true;loadFailure={error:'plain failure'};await assert.rejects(setOverlayLayout('W'),error=>String(error).includes('plain failure')&&!/undefined|NaN/.test(String(error)));
  });
  await operationCase('zoom edit decodes Windows config, preserves encoding intent and checks original content',async()=>{
    running=true;requireWindowsConfigRead=true;text='; 中文配置\r\n[OSD]\r\nZoomRatio=2\r\n[Other]\r\nKeep=原值\r\n';const original=text,start=calls.length;
    await setRtssZoom(4);assert.equal(await readRtssZoom(),4);
    const write=calls.slice(start).find(c=>c.cmd==='fs.writeTextFileAtomic');assert.equal(write?.args.textEncoding,'windows-config');assert.equal(write?.args.expectedContent,original);assert.equal(rtssIniValue(text,'Other','Keep'),'原值');
  });
  await operationCase('stopped RTSS layout save uses same decoded and encoding-preserving write path',async()=>{
    running=false;requireWindowsConfigRead=true;text='; 中文模板\r\n[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[Other]\r\nLayout=原值\r\n';const original=text,start=calls.length;
    assert.deepEqual(await setOverlayLayout('J'),{mode:'saved'});assert.equal(await readOverlayLayout(),'YeManOBS-JJ-1.ovl');const write=calls.slice(start).find(c=>c.cmd==='fs.writeTextFileAtomic');assert.equal(write?.args.textEncoding,'windows-config');assert.equal(write?.args.expectedContent,original);assert.equal(rtssIniValue(text,'Other','Layout'),'原值');
  });
  await operationCase('atomic zoom write failure does not reload profiles or report success',async()=>{
    running=true;text='[OSD]\r\nZoomRatio=2\r\n';const original=text,reloads=completedReloads;rejectWrite=true;await assert.rejects(setRtssZoom(6),/原子写入失败/);assert.equal(text,original);assert.equal(completedReloads,reloads);
  });
  await operationCase('changed or vanished source is rejected without stale overwrite or profile reload',async()=>{
    running=true;text='[OSD]\r\nZoomRatio=2\r\n';const original=text,reloads=completedReloads;staleWrite=true;await assert.rejects(setRtssZoom(6),/CONFIG_TEXT_CHANGED/);staleWrite=false;configDisappears=true;await assert.rejects(setRtssZoom(6),/CONFIG_TEXT_SOURCE_MISSING/);assert.equal(text,original);assert.equal(completedReloads,reloads);
  });
  await operationCase('saved layout cannot report success when the atomic writer did not persist the target',async()=>{
    running=false;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n';ignoreWrite=true;await assert.rejects(setOverlayLayout('J'),/RTSS.*读回/);assert.equal(rtssIniValue(text,'Settings','Layout'),'YeManOBS-W-1.ovl');
  });
  await operationCase('zoom cannot report success when reloaded configuration disagrees with requested ratio',async()=>{
    running=true;text='[OSD]\r\nZoomRatio=2\r\n';reloadWritesBack=true;await assert.rejects(setRtssZoom(6),/RTSS.*读回/);assert.equal(await readRtssZoom(),2);
  });
  await operationCase('profile reload failure is explicit and next queued edit still recovers',async()=>{
    running=true;text='[OSD]\r\nZoomRatio=2\r\n';failReload=true;await assert.rejects(setRtssZoom(4),/reload-unconfirmed/);failReload=false;await setRtssZoom(5);assert.equal(await readRtssZoom(),5);
  });
  await operationCase('default 100 percent cannot masquerade as a persisted ratio after the OSD key disappeared',async()=>{
    running=true;text='[OSD]\r\nZoomRatio=2\r\n';omitZoomOnReload=true;await assert.rejects(setRtssZoom(5),/RTSS.*读回/);
  });
  await operationCase('indented OSD section edits its own key without adding duplicate sections',async()=>{
    running=true;text='; configuration\r\n  [OSD]  \r\n  ZoomRatio = 2\r\n[Other]\r\nZoomRatio=7\r\n';await setRtssZoom(4);assert.equal(rtssIniValue(text,'OSD','ZoomRatio'),'4');assert.equal((text.match(/\[OSD\]/g)??[]).length,1);assert.equal(rtssIniValue(text,'Other','ZoomRatio'),'7');assert.equal(await readRtssZoom(),4);
  });
  await operationCase('rapid layout and zoom edits share one serial queue and recover to final requested layout',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nZoomRatio=2\r\n';let release:()=>void=()=>{};blockReload=new Promise<void>(resolve=>{release=resolve});const start=calls.length;
    const zoom=setRtssZoom(4);for(let i=0;i<100;i++)await Promise.resolve();const layout=setOverlayLayout('L');for(let i=0;i<100;i++)await Promise.resolve();const beforeRelease=calls.slice(start).filter(c=>c.cmd==='rtss.loadOverlay');assert.equal(beforeRelease.length,0,'later layout must wait for in-flight profile reload and readback');release();await Promise.all([zoom,layout]);assert.equal(configOperationMax,1);assert.deepEqual(calls.slice(start).filter(c=>c.cmd==='rtss.loadOverlay').map(c=>c.args.layout),['YeManOBS-L-1.ovl']);assert.equal(rtssIniValue(text,'Settings','Layout'),'YeManOBS-L-1.ovl');assert.equal(rtssIniValue(text,'OSD','ZoomRatio'),'4');
  });
  if(process.env.YMCC_RTSS_CONFIG_RESULT){const {writeFileSync}=await import('node:fs');writeFileSync(process.env.YMCC_RTSS_CONFIG_RESULT,JSON.stringify({ok:operationCases.every(c=>c.ok),cases:operationCases},null,2));}
  console.log(JSON.stringify({operationCases},null,2));
  assert(operationCases.every(c=>c.ok),'RTSS config operation regressions failed');

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
