import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createMemoryHost} from './memory-boundary.mjs';
import {classifyBundledInput, lockBundledNodeTools, verifyBundledImplementation, verifyBundledToolScope} from '../bundled-node-tool-authority.mjs';
import {readBudgetWorker,runWithWallBudget,validateWallBudget} from '../../optimization/execution-budget.mjs';

const SELF=fileURLToPath(import.meta.url);
const REPO=path.resolve(import.meta.dirname,'../../../..');
const CORE=path.resolve(REPO,'../../../Testing/YMCC');
const MEMORY=path.join(import.meta.dirname,'memory-boundary.mjs');
const APP=path.join(REPO,'src/App.vue');
const PROJECTION=path.join(import.meta.dirname,'app-unmount-projection.mjs');
const NODE=path.resolve(CORE,'../../Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe');
const sha=b=>createHash('sha256').update(b).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>key(a)===key(b);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const evidence=(dir,file,value)=>fs.writeFileSync(path.join(dir,file),JSON.stringify(value,null,2)+'\n',{flag:'wx',flush:true});
const recoveryName='outer-hud-coalesced-ready-unblocks-failed-switch';
const scenarioNames=['outer-hud-stop-owns-notice-retirement','outer-hud-stop-rejects-delayed-settings-admission',recoveryName];
const appScenarioNames=['app-unmount-retires-visible-owned-notice','app-unmount-rejects-delayed-settings-admission'];
const ENTRY=`export {startMonitorHudHost} from './src/bridge/monitorHudHost';
export {setLogSink} from './src/bridge/ipc';
export {SHORTCUT_FEEDBACK_EVENT} from './src/bridge/shortcutActionFeedback';
export {getSharedPowerLifecycleDiagnostics} from './src/bridge/powerLifecycleState';
export {normalizeSettings} from './src/bridge/settingsRepository';
export {registerYmccHudOutputAdapter,monitorOutputCoordinator,onMonitorOutputApplied} from './src/bridge/monitorOutput';
export {registerDeckyMirrorExtension} from './src/bridge/deckyMirrorExtensions';
export {registerUpdateReadyPresenter,presentUpdateReady} from './src/bridge/updateReadyNotice';`;

function parseArgs(args){
  assert(same(process.execPath,NODE) && !process.env.NODE_OPTIONS && !process.env.NODE_PATH,'BUNDLED_NODE_ONLY');
  const appUnmount=args.length===6 && args[4]==='--scope' && args[5]==='app-unmount';
  assert((args.length===4||appUnmount) && args[0]==='--evidence-dir' && args[2]==='--budget-ms' && /^\d+$/.test(args[3]),'EXPLICIT_OUTER_HUD_ARGUMENTS');
  assert(path.isAbsolute(args[1]),'EXPLICIT_ABSOLUTE_EVIDENCE');
  const outDir=path.resolve(args[1]),r=path.relative(path.join(CORE,'Evidence'),outDir);
  assert(r&&!r.startsWith('..')&&!path.isAbsolute(r),'CORE_EVIDENCE_ONLY');
  return {outDir,budgetMs:validateWallBudget(Number(args[3])),appUnmount};
}

async function buildOuterBundle(runDir,appUnmount){
  const authority=lockBundledNodeTools(REPO);
  const helpers=verifyBundledImplementation(REPO);
  const require=createRequire(path.join(REPO,'package.json'));
  const cacheBefore=new Set(Object.keys(require.cache));
  const inputs=new Map(), services=[];
  function track(file, contents){
    const row=classifyBundledInput(REPO,file,contents,authority);
    const old=inputs.get(key(file));
    assert(!old||old.sha256===row.row.sha256,'BUNDLE_INPUT_DRIFT:'+file);
    inputs.set(key(file),{actualPath:fs.realpathSync(file),role:row.role,...row.row});
  }
  assert(same(require.resolve('esbuild'),authority.compilerEntry),'COMPILER_ENTRY');
  const esbuild=require('esbuild');
  assert.equal(esbuild.version,authority.compilerVersion);
  let projection;
  if(appUnmount){
    const pinImport=id=>{
      const file=require.resolve(id);
      track(file,fs.readFileSync(file));
      return require(file);
    };
    const appBytes=fs.readFileSync(APP);
    track(APP,appBytes);
    track(PROJECTION,fs.readFileSync(PROJECTION));
    const {projectAppUnmount}=await import('./app-unmount-projection.mjs');
    const vueRequire=createRequire(require.resolve('vue'));
    const rendererEntry=fs.realpathSync(path.join(path.dirname(vueRequire.resolve('@vue/runtime-core')),'dist/runtime-core.esm-bundler.js'));
    assert.equal(classifyBundledInput(REPO,rendererEntry,fs.readFileSync(rendererEntry),authority).role,'tool');
    projection=projectAppUnmount(appBytes,pinImport('vue/compiler-sfc'),pinImport('typescript'),rendererEntry.replaceAll('\\','/'));
    fs.writeFileSync(path.join(runDir,'App.onUnmounted.original.ts'),projection.raw,{flag:'wx',flush:true});
    fs.writeFileSync(path.join(runDir,'App.onUnmounted.missing-stop.ts'),projection.mutated,{flag:'wx',flush:true});
    evidence(runDir,'app-unmount-projection.json',projection.projection);
  }
  const spawn=childProcess.spawn;
  childProcess.spawn=function(program,args,options){
    assert(same(fs.realpathSync(program),authority.compilerBinary),'UNEXPECTED_COMPILER_PROGRAM');
    assert.deepEqual(args,['--service='+authority.compilerVersion,'--ping']);
    const child=spawn.call(this,program,args,options);
    services.push({pid:child.pid,program:fs.realpathSync(program),args});
    return child;
  };
  let build;
  try {
    build=await esbuild.build({stdin:{contents:ENTRY+(projection?.entry??''),sourcefile:'monitor-outer-hud-harness.ts',resolveDir:REPO,loader:'ts'},
      tsconfig:path.join(REPO,'tsconfig.json'),bundle:true,write:false,metafile:true,target:'es2022',platform:'browser',format:'iife',globalName:'NoticeLifecycleActual',
      plugins:[{name:'capture-exact-consumed-input',setup(b){b.onLoad({filter:/.*/},args=>{
        const actual=fs.realpathSync(args.path), contents=fs.readFileSync(actual);
        const loader={'.ts':'ts','.js':'js','.mjs':'js','.cjs':'js','.json':'json'}[path.extname(actual)];
        assert(loader,'UNREVIEWED_OUTER_HUD_LOADER:'+actual);
        track(args.path,contents);
        return {contents,loader,resolveDir:path.dirname(args.path)};
      });}}]});
  } finally {childProcess.spawn=spawn;await esbuild.stop();}
  assert.equal(services.length,1);
  assert(Number.isInteger(services[0].pid));
  for(const file of Object.keys(require.cache)) if(!cacheBefore.has(file)) track(file,fs.readFileSync(file));
  const sourcePaths=[...inputs.values()].filter(x=>x.role==='source').map(x=>x.path);
  if(appUnmount)assert(Object.keys(build.metafile.inputs).some(file=>/\/runtime-core\/dist\/runtime-core\.esm-bundler\.js$/.test(file.replaceAll('\\','/'))),'ACTUAL_VUE_CORE_REQUIRED');
  for(const expected of ['monitorHudHost','monitorNoticeHost','ipc','desktopHudOwner','monitorHudClock','settingsRepository'])
    assert(sourcePaths.includes('src/bridge/'+expected+'.ts'),'ACTUAL_OUTER_INPUT_REQUIRED:'+expected);
  evidence(runDir,'outer-hud-build-metafile.json',build.metafile);
  const bundle=build.outputFiles[0].text;
  return {bundle,verify(){
    verifyBundledToolScope(authority,REPO);
    assert.deepEqual(verifyBundledImplementation(REPO),helpers);
    const ordered=[...inputs.values()].sort((a,b)=>a.actualPath.localeCompare(b.actualPath));
    for(const row of ordered)assert.equal(sha(fs.readFileSync(row.actualPath)),row.sha256,'LOADED_INPUT_DRIFT:'+row.path);
    return {bundleSha256:sha(bundle),services,sourceInputs:ordered.filter(x=>x.role==='source'),
      toolInputs:ordered.filter(x=>x.role==='tool'),toolGraphSha256:authority.graphSha256,helperInputs:helpers,
      ...(projection?{appUnmountProjection:projection.projection}:{})};
  }};
}

function runCase(bundle,holdSettings,appUnmount=false,skipMonitorStop=false,scenario){
  const dir='M:\\ymcc-fixture-memory-settings';
  const file=dir+'\\yeman-settings.json';
  const rejectedCommands=[];
  let settingsContent, sharedPowerBaseline, view, pulseOwner=null;
  const memory=createMemoryHost(bundle,{decide:request=>holdSettings&&request.cmd==='settings.read'?'hold':'success'}, {
    start(api){
      sharedPowerBaseline=structuredClone(api.getSharedPowerLifecycleDiagnostics());
      settingsContent=JSON.stringify(api.normalizeSettings({rtss:{monitorOutput:{enabled:false,popupEnabled:true,output:'ymcc'}}}));
      const host=api.startMonitorHudHost();
      if(appUnmount)view=api.mountAppUnmountProjection(host,skipMonitorStop);
      return host;
    },
    reply(cmd,args){
      if(scenario?.ports.has(cmd))return scenario.ports.get(cmd)(args);
      switch(cmd){
        case 'sys.info':return {cpuName:'Synthetic CPU'};
        case 'settings.location':return {file,directory:dir,backup:file+'.bak'};
        case 'settings.read':assert.equal(args.path,file);return {stamp:'100',unchanged:false,content:settingsContent};
        case 'desktopHud.status':return {id:0,available:true};
        case 'monitor.hudPulse.ack':return pulseOwner!==null&&args.nonce===pulseOwner;
        case 'monitor.hudPulse.set':
          assert.match(args.nonce,/^[a-zA-Z0-9-]{16,80}$/);
          assert.equal(typeof args.enabled,'boolean');
          if(args.update===true&&args.nonce!==pulseOwner)return false;
          if(args.enabled){pulseOwner=args.nonce;return true;}
          if(args.nonce!==pulseOwner)return false;
          pulseOwner=null;return true;
        default:rejectedCommands.push(cmd);throw Error('UNEXPECTED_OUTER_HUD_COMMAND:'+cmd);
      }
    },
  });
  return {memory,rejectedCommands,sharedPowerBaseline,view,readPulseOwner:()=>pulseOwner};
}

function ownedRetirement(post,ownedNotice){
  const [retire,...duplicate]=post.filter(r=>r.type==='native.request'&&r.cmd==='monitorNotice.retire');
  const retired=retire&&post.find(r=>r.type==='native.response'&&r.requestId===retire.requestId);
  return !!ownedNotice&&post.some(r=>r.type==='native.window.destroyed'&&r.id===ownedNotice.id)&&
    post.filter(r=>r.type==='native.request'&&r.cmd==='monitorNotice.close'&&r.args.hudId===ownedNotice.id&&r.args.nonce===ownedNotice.nonce).length===1&&
    duplicate.length===0&&retire?.args.leaseId===ownedNotice.leaseId&&retire.args.nonce===ownedNotice.nonce&&
    retired?.result?.leaseRetired===true&&retired.result.resourcesDestroyed===true&&retired.result.ownedOnly===true;
}

function verifyVueDispatch(view,before){
  const after=view.inspect();
  assert(view.mounted&&before.nodeCount===1&&before.calls.length===0&&!before.hostReferenceCleared,'PRIMARY_CALLBACK_NOT_DEFERRED_TO_UNMOUNT');
  const readinessStop=after.calls.indexOf('stopResumeGameReadinessWatch');
  assert(after.nodeCount===0&&after.hostReferenceCleared&&after.backgroundPresentationDisposed&&!after.resumeLifecycleActive&&
    after.calls[0]==='stopUndervoltRuntime'&&after.calls.includes('radialHost')&&after.calls.includes('stopDeckySafetyNotifications')&&
    readinessStop>=0&&after.calls.lastIndexOf('stopResumeGameReadinessWatch')===readinessStop&&readinessStop<after.calls.indexOf('resumeQueue')&&
    after.calls.at(-1)==='stopStartupPerformanceWatch'&&after.errors.length===0,'INCOMPLETE_PRIMARY_VUE_CALLBACK:'+JSON.stringify(after));
  return {before,after};
}

async function exercise(bundle,holdSettings,runDir,appUnmount=false){
  const id=(appUnmount?appScenarioNames:scenarioNames)[Number(holdSettings)],checks=[];
  const check=(name,value,detail=null)=>checks.push({name,passed:Boolean(value),detail});
  let memory, faults;
  try{
    const c=runCase(bundle,holdSettings,appUnmount);memory=c.memory;faults=c.rejectedCommands;
    const {host,api,clock}=memory;
    assert.throws(()=>api.registerYmccHudOutputAdapter({}),/already has an owner/);
    assert.throws(()=>api.registerDeckyMirrorExtension({prefix:'hud.'}),/already owned/);
    check('outer adapter and mirror extension are exclusive before stop',true);
    const ready=host.ready();
    await clock.advance(0);
    check('actual settings read was requested',memory.count('settings.read')===1);
    let ownedNotice, pendingSettings;
    if(holdSettings){
      pendingSettings=[...memory.operations.values()].find(r=>r.cmd==='settings.read');
      check('settings read is pending with no notice window before stop',memory.windows.size===0&&!!pendingSettings);
    }else{
      await clock.advance(1200);
      ownedNotice=[...memory.windows.values()][0];
      check('outer ready owns one real-source notice window at the memory port',memory.windows.size===1 && memory.count('monitorNotice.create')===1,
        memory.outerResources());
      memory.feedback();await clock.advance(0);
      check('actual shortcut feedback reached the owned notice',memory.count('monitorNotice.frame')>0 && [...memory.windows.values()].some(w=>w.visible));
    }
    const beforeStopPower=structuredClone(api.getSharedPowerLifecycleDiagnostics());
    const marker=memory.trace.length;
    if(appUnmount){
      const before=c.view.inspect();
      c.view.unmount();
      check('real Vue unmount dispatched the complete primary App callback',true,verifyVueDispatch(c.view,before));
      await clock.advance(0);
      if(!holdSettings)check('Vue unmount retired the visible window before advancing the clock',memory.windows.size===0&&ownedRetirement(memory.trace.slice(marker),ownedNotice));
    }else{memory.stop();memory.stop();}
    if(holdSettings)await memory.lateReplies();
    await ready;
    memory.nextGeneration();memory.emit('power.resumed',{generation:2});memory.theme();memory.feedback();
    // The ordinary WebView listener is module infrastructure, not a host lease.
    await clock.advance(30000);
    const terminal=memory.outerResources();
    const power=structuredClone(api.getSharedPowerLifecycleDiagnostics());
    check('window, host-owned subscriptions, timers and native requests are all retired',terminal.windows===0&&
      Object.values(terminal.domSubscriptions).every(row=>row.host.length===0)&&terminal.timers===0&&terminal.nativeOperations===0,terminal);
    check('document-level visibility infrastructure is observed separately',
      terminal.domSubscriptions.document.module.includes('visibilitychange'),terminal.domSubscriptions);
    check('shared power subscriptions return to the pre-host baseline and HUD resources are released',
      beforeStopPower.subscriberCount>c.sharedPowerBaseline.subscriberCount&&power.active===c.sharedPowerBaseline.active&&
      power.subscriberCount===c.sharedPowerBaseline.subscriberCount&&!power.read.inFlight&&!power.read.retryTimerActive&&
      terminal.host.sampleUsers===0&&terminal.host.clock.users===0&&!terminal.host.desktop.owned&&!terminal.host.desktop.pendingCalls,
      {baseline:c.sharedPowerBaseline,beforeStop:beforeStopPower,afterStop:power,host:terminal.host});
    for(const release of [api.registerYmccHudOutputAdapter({}),api.registerDeckyMirrorExtension({prefix:'hud.'}),api.registerUpdateReadyPresenter(async()=>false)])release();
    check('the three singleton registration slots can be acquired after stop',true);
    const post=memory.trace.slice(marker);
    const pulseRequests=memory.trace.filter(r=>r.type==='native.request'&&r.cmd==='monitor.hudPulse.set');
    function acknowledged(row){return memory.trace.some(r=>r.type==='native.response'&&r.requestId===row.requestId&&r.result===true);}
    const acquired=pulseRequests.filter(r=>r.args.enabled&&!r.args.update&&acknowledged(r));
    const released=pulseRequests.filter(r=>!r.args.enabled&&acknowledged(r));
    check('each acquired native clock lease has a matching release receipt',acquired.length>0&&c.readPulseOwner()===null&&
      acquired.every(row=>released.some(r=>r.requestId>row.requestId&&r.args.nonce===row.args.nonce)),{acquired,released});
    check('no post-stop create, frame replay or settings mutation',!post.some(r=>r.type==='native.request'&&
      ['monitorNotice.create','monitorNotice.reserve','monitorNotice.frame','settings.write','desktopHud.create'].includes(r.cmd)));
    check('the late-read or exact owned-retirement operation was observed',holdSettings ?
      !!pendingSettings&&post.some(r=>r.type==='late.native.reply.delivered'&&r.requestId===pendingSettings.id)&&
      post.some(r=>r.type==='native.response'&&r.requestId===pendingSettings.id&&typeof r.result?.content==='string') :
      memory.count('monitorNotice.close',marker)===1&&ownedRetirement(post,ownedNotice));
  }catch(error){check('fixture completes without infrastructure fault',false,String(error.stack??error));}
  if(memory)evidence(runDir,id+'.trace.json',memory.trace);
  check('no unhandled native command',faults?.length===0,faults);
  return {id,checks,passed:checks.every(row=>row.passed)};
}

async function exerciseMissingStop(bundle,runDir){
  const {memory,view,rejectedCommands}=runCase(bundle,false,true,true);
  const {clock,host}=memory;
  let result;
  try{
    const ready=host.ready();
    await clock.advance(1200);await ready;
    memory.feedback();await clock.advance(0);
    const [notice]=memory.windows.values();
    assert(memory.windows.size===1&&notice.visible,'NEGATIVE_CONTROL_NEEDS_VISIBLE_NOTICE');
    const marker=memory.trace.length,before=view.inspect();
    view.unmount();await clock.advance(0);
    const dispatch=verifyVueDispatch(view,before);
    const post=memory.trace.slice(marker);
    const retirementAccepted=ownedRetirement(post,notice);
    assert(!retirementAccepted&&memory.windows.get(notice.id)?.visible===true&&memory.count('monitorNotice.retire',marker)===0,'MISSING_STOP_CONTROL_NOT_DETECTED');
    result={status:'EXPECTED_RETIREMENT_ASSERTION_REJECTION',retirementAccepted,windowStillVisible:true,dispatch};
  }finally{
    const rescueMarker=memory.trace.length;
    memory.stop();await clock.advance(0);
    evidence(runDir,'app-unmount-missing-stop-control.trace.json',memory.trace);
    if(result){
      result.harnessRescue={marker:rescueMarker,remaining:memory.outerResources()};
      assert.equal(result.harnessRescue.remaining.windows,0,'NEGATIVE_CONTROL_RESCUE_FAILED');
      assert.equal(rejectedCommands.length,0,'NEGATIVE_CONTROL_UNHANDLED_NATIVE_COMMAND');
    }
  }
  return result;
}

async function exerciseCoalescedReady(bundle,runDir){
  const checks=[];
  const check=(name,value,detail=null)=>{checks.push({name,passed:Boolean(value),detail});assert(value,name);};
  let owner=null,serial=100,staleReply=false,nativeVisible=true;
  const ports=new Map([
    ['desktopHud.status',()=>staleReply?{available:false,id:0,recoveryBlocked:true}:{available:true,id:owner?.id??0,nonce:owner?.nonce,
      ready:!!owner,armed:!!owner,presentationAllowed:!!owner,visible:nativeVisible,layoutReady:!!owner,controllerVisible:nativeVisible}],
    ['desktopHud.create',args=>{
      assert.equal(owner,null);nativeVisible=true;owner={id:++serial,nonce:args.nonce};
      memory.emit('desktopHud.ready',owner);return owner;
    }],
    ['desktopHud.frame',args=>{
      assert.equal(args.hudId,owner?.id);assert.equal(args.nonce,owner?.nonce);
      memory.emit('desktopHud.painted',{...owner,seq:args.frame.seq,visible:nativeVisible,armed:true,visibilityRevision:1});return true;
    }],
    ['desktopHud.close',args=>{
      if(args.hudId!==owner?.id||args.nonce!==owner?.nonce)return false;
      const closed=owner;owner=null;memory.emit('desktopHud.closed',closed);return true;
    }],
    ['monitor.snapshot',()=>null], ['monitor.start',()=>({ok:true})], ['monitor.stop',()=>({ok:true})],
    ['proc.identities',()=>({known:true,processes:[]})], ['proc.running',()=>({RTSS:false})],
    ['env.get',()=>null], ['rtss.findRegisteredInstall',()=>({known:true,directory:''})],
  ]);
  const {memory,rejectedCommands}=runCase(bundle,false,false,false,{ports});
  const {api,clock,host}=memory;
  const ready={phase:'ready',generation:1,known:true,inputReady:true,hardwareWritesAllowed:true,publicationRevision:2,observedNotificationSequence:1,notificationPending:false,suspendSerial:0};
  let off=()=>{};
  try{
    memory.publishPowerSnapshot(ready);await host.ready();await clock.advance(0);
    const prefs={...memory.preferences,output:'ymcc',enabled:true,ymccCarrier:'desktop',ymcc:{...memory.preferences.ymcc,visibility:'always'}};
    let statusReads=0;
    memory.policy.decide=r=>r.cmd==='desktopHud.status'&&++statusReads===3?'error':'success';
    off=api.onMonitorOutputApplied((_prefs,result)=>{
      if(result.ok)return;
      memory.policy.decide=r=>r.cmd==='desktopHud.status'?'hold':'success';
    });
    const failed=await api.monitorOutputCoordinator.apply(prefs);
    off();memory.policy.decide=undefined;await clock.advance(0);
    check('explicit ON waits for the close proof and records the failure',!failed.ok&&failed.phase==='hide-ymcc'&&memory.count('desktopHud.create')===0,failed);
    const oldReads=[...memory.operations.values()].filter(r=>r.cmd==='desktopHud.status');
    check('old desktop status transport is still withheld before Ready',oldReads.length>0);
    const boundary=memory.trace.length;
    const wake={...ready,publicationRevision:3,suspendSerial:1};
    memory.publishPowerSnapshot(wake);memory.publishPowerSnapshot({...wake,publicationRevision:4});
    memory.publishPowerSnapshot({...ready,publicationRevision:5},{store:false});
    for(let i=0;i<12;i++)await clock.advance(0);
    check('same-generation Ready restores once before any old status reply or timeout',clock.now===Date.UTC(2026,9,22)&&!!owner&&host.inspect().desktop.owned?.prepared&&memory.count('desktopHud.create')===1,host.inspect());
    const retired=memory.trace.slice(boundary).filter(r=>r.type==='ipc.complete'&&r.cmd==='desktopHud.status'&&r.error==='IPC request retired');
    check('old status waits returned before their withheld native reply',retired.length===oldReads.length&&oldReads.every(r=>memory.operations.has(r.id))&&host.inspect().desktop.pendingCalls===0,host.inspect());
    const restored=owner.nonce;
    staleReply=true;await memory.lateReplies();await clock.advance(0);staleReply=false;
    check('old unavailable reply cannot mark the new carrier blocked',host.inspect().desktop.nativeAvailable&&!host.inspect().desktop.recoveryBlocked,host.inspect());
    const next={...wake,publicationRevision:6,suspendSerial:2};
    memory.publishPowerSnapshot(next);await clock.advance(0);
    check('late and repeated Ready observations preserve the confirmed owner',owner?.nonce===restored&&memory.count('desktopHud.create')===1,host.inspect());

    async function pulse(delay=0){
      await clock.advance(delay);const nonce=host.inspect().clock.nonce;assert(nonce);
      memory.emit('monitor.hudPulse',{nonce,snapshot:null});
      for(let i=0;i<12;i++)await clock.advance(0);
    }
    nativeVisible=false;await pulse(10000);
    check('first pre-sleep mismatch uses only one of the two observations',host.inspect().desktop.visibilityHealth.failures===1&&memory.count('desktopHud.close')===0,host.inspect());
    memory.policy.decide=r=>r.cmd==='desktopHud.status'?'hold':'success';await pulse(10000);
    memory.policy.decide=undefined;
    const held=[...memory.operations.values()].filter(r=>r.cmd==='desktopHud.status');
    check('the periodic visibility read is held with a known prepared owner',held.length===1&&host.inspect().desktop.visibilityHealth.checking&&owner?.nonce===restored,host.inspect());
    const afterVisibilityWake={...next,publicationRevision:7,suspendSerial:3};
    memory.publishPowerSnapshot(afterVisibilityWake);memory.publishPowerSnapshot({...afterVisibilityWake,publicationRevision:8});
    memory.publishPowerSnapshot({...next,publicationRevision:9},{store:false});await clock.advance(0);
    const verifyBoundary=memory.trace.length;
    await pulse();
    check('the first wake pulse checks current visibility before the old reply',memory.count('desktopHud.status',verifyBoundary)===1&&memory.operations.has(held[0].id)&&host.inspect().desktop.visibilityHealth.failures===1&&owner?.nonce===restored,host.inspect());
    await memory.lateReplies();await clock.advance(0);
    check('the old matching hidden reply does not count or close the current window',owner?.nonce===restored&&host.inspect().desktop.visibilityHealth.failures===1&&memory.count('desktopHud.close')===0,host.inspect());
    await pulse(10000);
    const repair=memory.trace.find(r=>r.type==='native.request'&&r.cmd==='desktopHud.close');
    check('two current mismatches repair only the recorded window and recover once',!!owner&&owner.nonce!==restored&&repair?.args.nonce===restored&&memory.count('desktopHud.close')===1&&memory.count('desktopHud.create')===2&&host.inspect().desktop.owned?.prepared,host.inspect());

    const lost=owner;owner=null;
    memory.policy.decide=r=>r.cmd==='desktopHud.status'?'error':'success';
    memory.emit('desktopHud.closed',lost);
    await clock.advance(0);await clock.advance(1000);await clock.advance(3000);
    check('a failed carrier exhausts the bounded restore attempts',host.inspect().recovery.restoreAttempts===3&&host.inspect().recovery.retryAt===0&&memory.count('desktopHud.create')===2,host.inspect());
    memory.policy.decide=undefined;
    const sameBoundary={...afterVisibilityWake,publicationRevision:10,observedNotificationSequence:2};
    memory.publishPowerSnapshot(sameBoundary);await clock.advance(0);
    check('a later notification does not refund the same suspend boundary',host.inspect().recovery.restoreAttempts===3&&memory.count('desktopHud.create')===2,host.inspect());
    const nextBoundary={...sameBoundary,publicationRevision:11,suspendSerial:4};
    memory.publishPowerSnapshot(nextBoundary);
    for(let i=0;i<12;i++)await clock.advance(0);
    check('a newer committed suspend serial restores the exhausted carrier once',!!owner&&host.inspect().desktop.owned?.prepared&&memory.count('desktopHud.create')===3,host.inspect());
    memory.publishPowerSnapshot({...nextBoundary,publicationRevision:12});
    memory.publishPowerSnapshot({...sameBoundary,publicationRevision:13},{store:false});await clock.advance(0);
    check('duplicate and old Ready do not open another carrier',memory.count('desktopHud.create')===3,host.inspect());
    const disable=await api.monitorOutputCoordinator.apply({...prefs,enabled:false});
    memory.publishPowerSnapshot({...nextBoundary,publicationRevision:14,suspendSerial:5});await clock.advance(0);
    check('explicit OFF closes the owned window and Ready does not resurrect it',disable.ok&&disable.applied==='off'&&owner===null&&memory.count('desktopHud.create')===3,disable);
  }catch(error){checks.push({name:'directed recovery completed',passed:false,detail:String(error.stack??error)});}
  finally{
    off();memory.policy.decide=undefined;memory.stop();await memory.lateReplies();await clock.advance(30000);
    const resources=memory.outerResources();
    checks.push({name:'window, host resources and native operations return at stop',passed:!owner&&resources.windows===0&&resources.timers===0&&resources.nativeOperations===0&&resources.host.clock.users===0&&resources.host.sampleUsers===0&&!resources.host.desktop.pendingCalls&&Object.values(resources.domSubscriptions).every(r=>!r.host.length),detail:resources});
    checks.push({name:'no unhandled native command',passed:rejectedCommands.length===0,detail:rejectedCommands});
    evidence(runDir,recoveryName+'.trace.json',memory.trace);
  }
  return {id:recoveryName,checks,passed:checks.every(row=>row.passed)};
}

async function worker(args){
  const context=readBudgetWorker(args[1],args[2],{repoRoot:REPO,coreRoot:CORE});
  const {appUnmount}=parseArgs(context.args);
  const names=appUnmount?appScenarioNames:scenarioNames;
  const cases=[];let provenance,error,negativeControl;
  try {
    const build=await buildOuterBundle(context.runDir,appUnmount);
    for(const hold of [false,true]){
      assert(context.remainingMs()>0,'RUN_BUDGET_EXHAUSTED');
      cases.push(await exercise(build.bundle,hold,context.runDir,appUnmount));
    }
    if(!appUnmount)cases.push(await exerciseCoalescedReady(build.bundle,context.runDir));
    if(appUnmount&&cases.every(row=>row.passed))negativeControl=await exerciseMissingStop(build.bundle,context.runDir);
    provenance=build.verify();
  } catch(e) { error=String(e.stack??e); }
  const passed=cases.filter(row=>row.passed).length;
  const status=!error&&passed===names.length&&(!appUnmount||negativeControl)?'PASS_SELECTED_SCOPE':'FAIL_OUTER_HUD_SCENARIOS';
  evidence(context.runDir,'result.json',{schemaVersion:1,runDir:context.runDir,status,scope:appUnmount?'EXPLICIT_PRIMARY_APP_UNMOUNT_PROJECTION':'EXPLICIT_OUTER_HUD_SOFTWARE_ONLY',
    originalAdapter:'lifecycle.monitor-notice-host',summary:{selected:names.length,executedThisRun:cases.length,actualPassedThisRun:passed},
    cases,provenance,error,negativeControl,appUnmounted:false,defaultHostCasesExecuted:0,nativeCarrierExecuted:false,fullParentAcceptance:false,installedYmccStarted:false});
  return status==='PASS_SELECTED_SCOPE'?0:2;
}

if(process.argv[1]&&same(process.argv[1],SELF)){
  const args=process.argv.slice(2);
  try{
    if(args[0]==='--budget-worker')process.exitCode=await worker(args);
    else {
      const opts=parseArgs(args);
      const {result,exitCode}=await runWithWallBudget(args,{repoRoot:REPO,coreRoot:CORE,workerEntry:SELF,additionalInputs:opts.appUnmount?[MEMORY,APP,PROJECTION]:[MEMORY],...opts});
      process.exitCode=exitCode||(!result.resourceZeroVerified||result.ownedJob?.terminationRequested!==false?10:0);
    }
  } catch(error){console.error(error.stack??error);process.exitCode=2;}
}
