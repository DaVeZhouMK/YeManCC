import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { readRtssSharedFlags, rtssIniValue, setRtssIniValue } from '@/bridge/rtssOverlay';

const dir = 'C:\\TestRTSS';
const cfg = `${dir}\\Plugins\\Client\\OverlayEditor.cfg`;
const sharedConfig = `${dir}\\Profiles\\Config`;
const baseShared = '[Plugins]\r\nOverlayEditor.dll=1\r\n[Shared]\r\nFlags=80000005\r\n[Other]\r\nFlags=FFFFFFFF\r\n';
let shared = baseShared, failVisibility = false, skipVisibilityPersistence = false;
let running = true;
let rtssInstalled = false;
let failPrepare = false, failLoad = false;
let prepareFailure: Record<string, unknown> | null = null, loadFailure: Record<string, unknown> | null = null;
let dropPrepareReply = false, dropLoadReply = false;
let compatibleLoadedBridge = false;
let requireWindowsConfigRead = false, rejectWrite = false, staleWrite = false, configDisappears = false, ignoreWrite = false;
let failReload = false, reloadWritesBack = false;
let atomicReplyHook: null | (() => Promise<void>) = null;
let rtssOwnerReplyHook: null | ((cmd:string,args:any)=>Promise<void>) = null;
let completedReloads = 0;
let omitZoomOnReload = false, blockReload: Promise<void> | null = null, configOperationActive = 0, configOperationMax = 0;
// Exercise the real IPC timeout/cleanup code with accelerated test timers.
const realSetTimeout = globalThis.setTimeout;
const nativeEvents = new EventTarget();
const documentEvents = new EventTarget();
(globalThis as any).document = {visibilityState:'hidden',createElement:()=>({content:{}}),addEventListener:documentEvents.addEventListener.bind(documentEvents),removeEventListener:documentEvents.removeEventListener.bind(documentEvents)};
(globalThis as any).setTimeout = (fn: (...args: any[]) => void, ms?: number, ...args: any[]) =>
  realSetTimeout(fn, ms === 15000 || ms === 6000 || ms === 10000 ? 40 : ms, ...args);
let text = '[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\nSMART=0\r\n[Other]\r\nLayout=untouched.ovl\r\n[OSD]\r\nEnableOSD=1';
const calls: Array<{cmd: string; args: any}> = [];
let handler: ((e: {data: unknown}) => void) | null = null;
let active = 0, maxActive = 0;
let rtssOwnerWireContext:any;let rtssOwnerNativeWireChecks=0;
(globalThis as any).window = {
  chrome: {webview: {
    addEventListener: (_type: string, fn: typeof handler) => { handler = fn; },
    postMessage: (message: any) => {
      calls.push({cmd: message.cmd, args: message.args});
      if(rtssOwnerWireContext&&(['rtss.prepareOverlay','rtss.loadOverlay','rtss.reloadProfiles','rtss.setOsdVisible','fs.writeTextFileAtomic'].includes(message.cmd)||message.cmd==='shell.run'&&(message.args.program==='cmd'||message.args.program==='powershell.exe'&&message.args.args.includes('-File')))){
        assert.deepEqual(message.args.bootContext,rtssOwnerWireContext,'Actual API must forward this request owner at '+message.cmd);rtssOwnerNativeWireChecks++;
      }
      if ((message.cmd === 'rtss.prepareOverlay' && dropPrepareReply) ||
          (message.cmd === 'rtss.loadOverlay' && dropLoadReply)) return;
      void (async () => {
        let result: unknown;
        switch (message.cmd) {
          case 'shell.run': if(message.args.program==='powershell.exe'&&message.args.args.includes('-File'))running=true;result = {exitCode: 0, stdout: rtssInstalled ? dir : '', stderr: ''}; break;
          case 'shell.hidden': running = true; result = {exitCode:0,stdout:'',stderr:''}; break;
          case 'proc.identities': result = {known:true,processes:[],reason:'',error:0}; break;
          case 'fs.exists': result = rtssInstalled; break;
          case 'proc.running': result = {RTSS: running}; break;
          case 'power.lifecycle': result = {known:true,phase:'ready',generation:1,inputReady:true,hardwareWritesAllowed:true,publicationRevision:1,observedNotificationSequence:0,notificationPending:false}; break;
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
          case 'rtss.setOsdVisible':
            if (!failVisibility && !skipVisibilityPersistence) {
              const flags = (((readRtssSharedFlags(shared) ?? 0) & ~1) | Number(message.args.visible)) >>> 0;
              shared = setRtssIniValue(shared, 'Shared', 'Flags', flags.toString(16).toUpperCase().padStart(8, '0'));
            }
            result = {ok: !failVisibility, error:'visibility-unconfirmed'};
            break;
          case 'fs.readTextFile':
            if (message.args.path === sharedConfig) { result = shared; break; }
            if (message.args.path !== cfg && message.args.path !== dir+'\\Profiles\\Global') { handler!({data:{id:message.id,error:'unexpected RTSS config path'}});return; }
            if (requireWindowsConfigRead && message.args.textEncoding !== 'windows-config') { handler!({data:{id:message.id,error:'NATIVE_RESPONSE_SERIALIZATION_FAILED'}});return; }
            result = text; break;
          case 'fs.writeTextFileAtomic':
            if (message.args.path === sharedConfig) {
              assert.equal(message.args.expectedContent, shared);
              if (!rejectWrite && !ignoreWrite) shared = message.args.content;
              result = !rejectWrite; break;
            }
            if (message.args.path !== cfg && message.args.path !== dir+'\\Profiles\\Global') { handler!({data:{id:message.id,error:'unexpected RTSS config path'}});return; }
            if (staleWrite || configDisappears) { handler!({data:{id:message.id,error:staleWrite?'CONFIG_TEXT_CHANGED':'CONFIG_TEXT_SOURCE_MISSING'}});return; }
            if (!rejectWrite && !ignoreWrite) text = message.args.content; result = !rejectWrite; if(atomicReplyHook){const hook=atomicReplyHook;atomicReplyHook=null;await hook();} break;
          case 'rtss.reloadProfiles': completedReloads++;configOperationActive++;configOperationMax=Math.max(configOperationMax,configOperationActive);if(blockReload)await blockReload;configOperationActive--;if(omitZoomOnReload)text='[Other]\r\nZoomRatio=5\r\n';if(reloadWritesBack)text=setRtssIniValue(text,'OSD','ZoomRatio','2');result={ok:!failReload,error:'reload-unconfirmed'};break;
          default: throw new Error(`unexpected IPC ${message.cmd}`);
        }
        if(rtssOwnerReplyHook)await rtssOwnerReplyHook(message.cmd,message.args);
        handler!({data: {id: message.id, result}});
      })();
    },
  }},
  addEventListener: nativeEvents.addEventListener.bind(nativeEvents),
  removeEventListener: nativeEvents.removeEventListener.bind(nativeEvents),
  dispatchEvent: nativeEvents.dispatchEvent.bind(nativeEvents),
};

async function main() {
  const scopes=['--boot-output-owner-only','--bridge-readiness-only','--atomic-reload-cancellation-only'].filter(flag=>process.argv.includes(flag));
  assert(scopes.length<=1,'Choose one explicit RTSS scope');
  assert(!process.argv.some(value=>value.startsWith('--rtss-owner-'))||process.argv.includes('--boot-output-owner-only'),'RTSS owner selectors require their explicit scope');
  if(process.argv.includes('--boot-output-owner-only')){
    const bridge:any=await import('@/bridge/yeman'),{captureBootPreferenceWrite,assertBootPreferenceOwner}=await import('@/bridge/bootModeGlobalPreferences');
    const output=process.env.YEMAN_RTSS_AWAIT_TEST_OUT;if(!output)throw Error('OWNED_TEST_OUTPUT_REQUIRED');mkdirSync(output,{recursive:true});
    const choose=(name:string)=>process.argv.find(value=>value.startsWith('--rtss-owner-'+name+'='))?.split('=').slice(1).join('=');
    const modes=['global','xbox','steam','xbox-steam'],kinds=['revision','hash','mode','result','observed','ack','unrelated','current'];
    const plans=[{name:'toggle-on',stages:['directory','prepare','launch','running']},{name:'toggle-off',stages:['stop']},{name:'online',stages:['directory','prepare','running','load','zoom-exists','zoom-read-1','zoom-write','reload','zoom-read-2']},{name:'online-off',stages:['directory','prepare','running','load']},{name:'saved',stages:['directory','prepare','running','layout-exists','layout-read','layout-write','layout-readback']},{name:'queued',stages:['queued-entry']},{name:'pre-entry',stages:['pre-entry']}];
    const selected={mode:choose('mode'),scenario:choose('scenario'),stage:choose('stage'),kind:choose('kind')};
    for(const [name,values]of Object.entries({mode:modes,scenario:plans.map(p=>p.name),stage:[...new Set(plans.flatMap(p=>p.stages))],kind:kinds}))assert(!selected[name as keyof typeof selected]||values.includes(selected[name as keyof typeof selected]!), 'Unknown RTSS owner '+name);
    const defaults=JSON.parse(readFileSync('PowerControl/settings-defaults-v1.json','utf8')),rows:any[]=[],ordinary:any[]=[];
    let settings:any,ownerReads=0,readCounts:Record<string,number>={};
    const reset=(mode:string,scenario:string)=>{bridge.setPowerControlDir('C:/TestYMCC/PowerControl');settings=structuredClone(defaults);settings.input.revision=7;settings.extensions={bootModes:{schema:1,revision:3,current:mode}};running=scenario!=='toggle-on'&&scenario!=='saved';rtssInstalled=true;calls.length=0;ownerReads=0;readCounts={};rtssOwnerWireContext=undefined;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nZoomRatio=5\r\n';return captureBootPreferenceWrite({origin:'boot-mode',bootContext:{input:structuredClone(settings.input),extensions:structuredClone(settings.extensions)}});};
    const mutate=(kind:string)=>{if(kind==='revision')settings.input.revision++;if(kind==='hash')settings.input.diagnostics.inputLoggingEnabled=!settings.input.diagnostics.inputLoggingEnabled;if(kind==='mode')settings.extensions.bootModes.revision++;if(kind==='result')settings.extensions.bootModesStartupResult={schema:1,revision:99};if(kind==='observed')settings.input.startupObservedBoot='other-boot';if(kind==='ack')settings.input.hostAcknowledgedRevision=17;if(kind==='unrelated')settings.extensions.peer=true;};
    const label=(cmd:string,args:any)=>{if(cmd==='rtss.prepareOverlay')return 'prepare';if(cmd==='rtss.loadOverlay')return 'load';if(cmd==='rtss.reloadProfiles')return 'reload';if(cmd==='shell.hidden')return 'launch';if(cmd==='shell.run')return args.program==='cmd'?'stop':args.program==='powershell.exe'&&args.args.includes('-File')?'launch':'directory';if(cmd==='proc.running')return 'running';if(cmd==='fs.exists')return args.path===cfg?'layout-exists':args.path===dir+'\\Profiles\\Global'?'zoom-exists':'directory';if(cmd==='fs.writeTextFileAtomic')return args.path===cfg?'layout-write':'zoom-write';if(cmd==='fs.readTextFile'){const n=readCounts[args.path]=(readCounts[args.path]??0)+1;return args.path===cfg?(n===1?'layout-read':'layout-readback'):'zoom-read-'+n;}return cmd;};
    const effects=(list:typeof calls)=>list.filter(c=>['rtss.prepareOverlay','rtss.loadOverlay','rtss.reloadProfiles','shell.hidden','fs.writeTextFileAtomic'].includes(c.cmd)||c.cmd==='shell.run'&&(c.args.program==='cmd'||c.args.program==='powershell.exe'&&c.args.args.includes('-File'))).map(c=>c.cmd);
    const run=(scenario:string,current?:any)=>{rtssOwnerWireContext=current?.bootContext;return scenario==='toggle-on'?bridge.toggleRtss(true,current):scenario==='toggle-off'?bridge.toggleRtss(false,current):bridge.setOverlayLayout(scenario==='online-off'?'off':'L',scenario==='online'?4:undefined,current);};
    const append=(row:any)=>writeFileSync(output+'/rtss-owner-observations.jsonl',JSON.stringify(row)+'\n',{flag:'a'});
    writeFileSync(output+'/rtss-owner-observations.jsonl','',{flag:'wx'});
    const wait=async(promise:Promise<any>,label:string)=>{let timer:ReturnType<typeof realSetTimeout>|undefined;try{return await Promise.race([promise,new Promise((_,reject)=>{timer=realSetTimeout(()=>reject(Error('RTSS fixture boundary not reached '+label)),1200);})]);}finally{if(timer)clearTimeout(timer);}};
    try{
      for(const mode of modes)for(const plan of plans)for(const stage of plan.stages)for(const kind of kinds){
        if(Object.entries(selected).some(([key,value])=>value&&value!==({mode,scenario:plan.name,stage,kind} as any)[key]))continue;
        const options=reset(mode,plan.name),allowed=['ack','unrelated','current'].includes(kind);let changed=false,at=0,result:any,error='';
        const current=async()=>{ownerReads++;assertBootPreferenceOwner(settings,options);return true;};Object.defineProperty(current,'bootContext',{value:structuredClone(options.bootContext)});
        if(plan.name==='queued'){
          let enter:()=>void=()=>{},release:()=>void=()=>{};const entered=new Promise<void>(r=>enter=r),hold=new Promise<void>(r=>release=r);rtssOwnerReplyHook=async cmd=>{if(cmd==='rtss.prepareOverlay'){rtssOwnerReplyHook=null;enter();await hold;}};
          const first=bridge.ensureRtssOverlayInstalled();let second:Promise<any>|undefined;
          try{await wait(entered,'first preparation');second=run('online-off',current).then((value:any)=>({value}),(error:any)=>({error:String(error)}));await Promise.resolve();changed=true;at=calls.length;mutate(kind);release();await first;const settled=await second;result=settled.value;error=settled.error??'';}
          finally{release();await first.catch(()=>{});if(second)await second;}
        }else{
          rtssOwnerReplyHook=async(cmd,args)=>{const found=label(cmd,args);if(!changed&&found===stage){changed=true;at=calls.length;mutate(kind);}};
          if(plan.name==='pre-entry'){changed=true;at=0;mutate(kind);}
          try{result=await run(plan.name==='pre-entry'?'online':plan.name,current);}catch(e){error=String(e);}
        }
        rtssOwnerReplyHook=null;const row={mode,scenario:plan.name,stage,kind,allowed,changed,error,result,lateEffects:effects(calls.slice(at)),ownerReads};append(row);assert(changed,'RTSS owner boundary not reached '+JSON.stringify(row));
        if(allowed){assert(!error,JSON.stringify(row));if(!plan.name.startsWith('toggle'))assert.equal(result?.mode,plan.name==='saved'?'saved':'live',JSON.stringify(row));}
        else{assert.equal(row.lateEffects.length,0,'Expired RTSS request reached later command '+JSON.stringify(row));assert.match(error,/BOOT_PREFERENCE_REQUEST_EXPIRED|RTSS_OUTPUT_REQUEST_EXPIRED/,'Expired RTSS request returned success '+JSON.stringify(row));}
        rows.push(row);
      }
      assert(rows.length,'Empty RTSS owner selection');
      for(const plan of (process.argv.includes('--rtss-owner-defer-ordinary')?[]:plans.filter(p=>p.name!=='queued'&&p.name!=='pre-entry'))){reset('global',plan.name);await run(plan.name);assert.equal(ownerReads,0);ordinary.push({scenario:plan.name,ownerReads:0});}
      const result={status:'PASS',nativeWireChecks:rtssOwnerNativeWireChecks,actualRows:rows.length,expectedAllRows:modes.length*kinds.length*plans.reduce((n,p)=>n+p.stages.length,0),selected,plans,ordinary,scope:'Actual yeman/API/IPC and real BootPreference owner assertion; original in-memory WebView transport; no native, filesystem, process or RTSS operation. Already-dispatched native admission remains OPEN.'};writeFileSync(output+'/rtss-owner-result.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:result.status,actualRows:result.actualRows,ordinary:ordinary.length}));
    }finally{rtssOwnerReplyHook=null;}
    return;
  }
  if(process.argv.includes('--bridge-readiness-only')){
    const {toggleRtss,setOverlayLayout,setPowerControlDir}=await import('@/bridge/yeman');
    setPowerControlDir('C:\\TestYMCC\\PowerControl');rtssInstalled=true;running=false;
    const results:any[]=[];
    await toggleRtss(true);
    const launch=calls.find(c=>c.cmd==='shell.hidden');
    const startIndex=launch?.args.args.indexOf('-RtssDirectory')??-1;
    results.push({name:'startup script uses exactly the already resolved RTSS directory',ok:startIndex>=0&&launch?.args.args[startIndex+1]===dir,launch});
    for(const [code,state] of [['RTSS_BRIDGE_UNAVAILABLE','enabled_not_loaded'],['RTSS_BRIDGE_INCOMPATIBLE','loaded_incompatible'],['RTSS_BRIDGE_ACCESS_DENIED','access_denied']]){
      const before=text,start=calls.length;failLoad=true;loadFailure={error:'bridge not ready',errorCode:code,bridgeState:state};let message='';
      try{await setOverlayLayout('L');}catch(error){message=String(error);}
      const relevant=calls.slice(start);results.push({name:code+' keeps original layout and sends no duplicate Load or restart',ok:text===before&&message.includes(code)&&relevant.filter(c=>c.cmd==='rtss.loadOverlay').length===1&&!relevant.some(c=>c.cmd==='shell.hidden'||c.cmd==='rtss.reloadProfiles'||c.cmd==='fs.writeTextFileAtomic'),message});
    }
    failLoad=false;loadFailure=null;const start=calls.length;await setOverlayLayout('L');results.push({name:'later explicit selection recovers the same serial queue',ok:rtssIniValue(text,'Settings','Layout')==='YeManOBS-L-1.ovl'&&calls.slice(start).filter(c=>c.cmd==='rtss.loadOverlay').length===1});
    const ok=results.every(c=>c.ok);if(process.env.YMCC_RTSS_CONFIG_RESULT)writeFileSync(process.env.YMCC_RTSS_CONFIG_RESULT,JSON.stringify({ok,cases:results},null,2),{flag:'wx'});console.log(JSON.stringify({ok,cases:results}));assert(ok,'bridge startup routing regression');return;
  }
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

  const {setOverlayLayout, readOverlayLayout, readRtssZoom, setRtssZoom, setPowerControlDir, ensureRtssOverlayInstalled, monitorOn} = await import('@/bridge/yeman');
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
  shared=baseShared;
  const coldStart=calls.length;
  assert.deepEqual(await setOverlayLayout('off'), {mode: 'saved'});
  assert.equal(readRtssSharedFlags(shared),0x80000004);
  assert.equal(rtssIniValue(shared,'Other','Flags'),'FFFFFFFF');
  assert(!calls.slice(coldStart).some(c=>c.cmd==='rtss.setOsdVisible'||c.cmd==='rtss.reloadProfiles'));
  assert.equal(calls.filter(c => c.cmd === 'rtss.loadOverlay').length, loads);
  assert.equal(rtssIniValue(text, 'Settings', 'Layout'), 'Empty.ovl');
  assert.equal(rtssIniValue(text, 'OSD', 'EnableOSD'), '0');
  assert.equal(rtssIniValue(text, 'Other', 'Layout'), 'untouched.ovl');
  assert(calls.some(c => c.cmd === 'fs.writeTextFileAtomic' && c.args.path === cfg));
  assert(!calls.some(c => /triggerOverlayHotkey|shell.hidden|shell.execute/.test(c.cmd)), 'never restart/inject keys');
  const operationCases: Array<{name:string;ok:boolean;error?:string}> = [];
  const operationCase=async(name:string,run:()=>Promise<void>)=>{try{await run();operationCases.push({name,ok:true});}catch(error){operationCases.push({name,ok:false,error:String(error)});}finally{prepareFailure=null;loadFailure=null;failPrepare=false;failLoad=false;requireWindowsConfigRead=false;rejectWrite=false;staleWrite=false;configDisappears=false;ignoreWrite=false;failReload=false;reloadWritesBack=false;omitZoomOnReload=false;blockReload=null;}};
  await operationCase('RTSS output closes real OSD before template cleanup and reopens only after target ACK',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nEnableOSD=1\r\nZoomRatio=5\r\n[Framerate]\r\nLimit=120\r\n[Hooking]\r\nEnableHooking=1\r\n';
    shared=baseShared;
    const start=calls.length;
    await setOverlayLayout('off');
    const close=calls.slice(start).map(c=>c.cmd);
    assert(close.indexOf('fs.writeTextFileAtomic')>=0);
    assert(close.indexOf('rtss.reloadProfiles')>close.indexOf('fs.writeTextFileAtomic'));
    assert(close.indexOf('rtss.loadOverlay')>close.indexOf('rtss.reloadProfiles'));
    assert(close.indexOf('rtss.setOsdVisible')>close.indexOf('rtss.reloadProfiles'));
    assert(close.indexOf('rtss.loadOverlay')>close.indexOf('rtss.setOsdVisible'));
    assert.equal(readRtssSharedFlags(shared),0x80000004);
    assert(!calls.slice(start).some(c=>c.cmd==='fs.writeTextFileAtomic'&&c.args.path===sharedConfig));
    assert.equal(rtssIniValue(text,'OSD','EnableOSD'),'0');
    assert.equal(rtssIniValue(text,'OSD','ZoomRatio'),'5');
    assert.equal(await monitorOn(),false);
    const next=calls.length;
    await setOverlayLayout('J',4);
    const open=calls.slice(next);
    assert.deepEqual(open.filter(c=>c.cmd==='rtss.loadOverlay').map(c=>c.args.layout),['YeManOBS-JJ-1.ovl']);
    assert(open.findIndex(c=>c.cmd==='fs.writeTextFileAtomic')>open.findIndex(c=>c.cmd==='rtss.loadOverlay'));
    assert.equal(open.filter(c=>c.cmd==='rtss.reloadProfiles').length,1);
    assert(open.findIndex(c=>c.cmd==='rtss.setOsdVisible')>open.findIndex(c=>c.cmd==='rtss.reloadProfiles'));
    assert.equal(readRtssSharedFlags(shared),0x80000005);
    assert.equal(rtssIniValue(text,'OSD','EnableOSD'),'1');
    assert.equal(rtssIniValue(text,'OSD','ZoomRatio'),'4');
    assert.equal(rtssIniValue(text,'Framerate','Limit'),'120');
    assert.equal(rtssIniValue(text,'Hooking','EnableHooking'),'1');
    assert.equal(await monitorOn(),true);
  });
  await operationCase('both OSD gates admit visibility; load failure never opens either gate',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nEnableOSD=0\r\n';shared=baseShared;
    assert.equal(await monitorOn(),false);
    text=setRtssIniValue(text,'OSD','EnableOSD','1');shared=setRtssIniValue(shared,'Shared','Flags','80000004');
    assert.equal(await monitorOn(),false);
    text=setRtssIniValue(text,'OSD','EnableOSD','0');failLoad=true;
    const start=calls.length;
    await assert.rejects(setOverlayLayout('J'),/load-unconfirmed/);
    assert(!calls.slice(start).some(c=>c.cmd==='rtss.setOsdVisible'||c.cmd==='rtss.reloadProfiles'));
    assert.equal(readRtssSharedFlags(shared),0x80000004);
  });
  await operationCase('visibility failure or missing Config readback cannot count as live OFF success',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nEnableOSD=1\r\n';shared=baseShared;
    try {
      failVisibility=true;
      await assert.rejects(setOverlayLayout('off'),/visibility-unconfirmed/);
      assert.equal(rtssIniValue(text,'OSD','EnableOSD'),'0');
      failVisibility=false;skipVisibilityPersistence=true;
      await assert.rejects(setOverlayLayout('off'),/显示开关保存读回未确认/);
    } finally { failVisibility=false;skipVisibilityPersistence=false; }
  });
  await operationCase('Global reload failure does not prevent live visibility OFF',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nEnableOSD=1\r\n';shared=baseShared;failReload=true;
    await assert.rejects(setOverlayLayout('off'),/reload-unconfirmed/);
    assert.equal(readRtssSharedFlags(shared),0x80000004);
  });
  await operationCase('retired intent after profile ACK cannot enable the shared visibility bit',async()=>{
    running=true;text='[Settings]\r\nLayout=Empty.ovl\r\n[OSD]\r\nEnableOSD=0\r\n';shared=setRtssIniValue(baseShared,'Shared','Flags','80000004');
    let current=true;
    rtssOwnerReplyHook=async cmd=>{if(cmd==='rtss.reloadProfiles')current=false;};
    const start=calls.length;
    try { await assert.rejects(setOverlayLayout('L',undefined,async()=>current),/RTSS_OUTPUT_REQUEST_EXPIRED/); }
    finally { rtssOwnerReplyHook=null; }
    assert(!calls.slice(start).some(c=>c.cmd==='rtss.setOsdVisible'));
    assert.equal(readRtssSharedFlags(shared),0x80000004);
  });
  await operationCase('OSD OFF takes effect even when the old template cannot be cleared',async()=>{
    running=true;text='[Settings]\r\nLayout=YeManOBS-W-1.ovl\r\n[OSD]\r\nEnableOSD=1\r\n';failPrepare=true;
    shared=baseShared;
    await assert.rejects(setOverlayLayout('off'),/prepare-failed/);
    assert.equal(readRtssSharedFlags(shared),0x80000004);
    assert.equal(rtssIniValue(text,'Settings','Layout'),'YeManOBS-W-1.ovl');
    assert.equal(rtssIniValue(text,'OSD','EnableOSD'),'0');
    assert.equal(await monitorOn(),false);
  });
  await operationCase('native load failure preserves diagnostic code path and OS error without writing selection',async()=>{
    running=true;text='[Settings]\r\nLayout=Empty.ovl\r\n[OSD]\r\nEnableOSD=0\r\n';const before=text,start=calls.length;
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
