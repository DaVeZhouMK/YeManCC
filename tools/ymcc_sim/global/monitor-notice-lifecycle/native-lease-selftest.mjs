import {createHash} from 'node:crypto';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {begin,compile,verify,receipt,sha,save,run,source,generated} from '../../../fixtures/radial_private/scope.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..'),args=process.argv.slice(2);
const shutdown=args.length===4&&args[2]==='--scope'&&args[3]==='shutdown';
const intent=args.length===4&&args[2]==='--scope'&&args[3]==='shutdown-intent';
const dispatch=intent||(args.length===4&&args[2]==='--scope'&&args[3]==='shutdown-dispatch');
assert([2,4].includes(args.length)&&args[0]==='--out'&&path.isAbsolute(args[1])&&(args.length===2||args[2]==='--mutation'||shutdown||dispatch));
const mutation=shutdown||dispatch?null:args[3]??null;
assert([null,'skip-retired-create-admission','ignore-destroy-failure'].includes(mutation));
const out=path.resolve(args[1]),scope=begin(repo,out,'tools/ymcc_sim/global/monitor-notice-lifecycle/native-lease-selftest.mjs'),started=new Date().toISOString();let state={startedAt:started,status:'OWNED_NATIVE_LEASE_FIXTURE_COMPILING',out,normalYmccStarted:false,manualSleepAcceptanceReady:false,wholeGoalComplete:false};const record=path.join(out,'NATIVE-LEASE-PROCESS.json'),saveState=()=>save(record,state);saveState();try{const fixture=path.join(import.meta.dirname,'native-lease-selftest.cpp'),beforeNative=new Map(),mainFile=path.join(repo,'native/main.cpp'),mainBytes=fs.readFileSync(mainFile),mainText=mainBytes.toString('utf8');const mainSha256=source(scope,mainFile,mainBytes).row.sha256;
 if(shutdown||dispatch){
  const projectionFile=path.join(import.meta.dirname,'notice-root-exit-projection.mjs');
  source(scope,projectionFile);
  const {projectNoticeRootExit}=await import('./notice-root-exit-projection.mjs');
  const projection=projectNoticeRootExit(mainText,{dispatch,intent});
  state.exitOwnerBinding={source:mainFile,sha256:mainSha256,call:projection.call,line:projection.line,mainMessageDispatchExecuted:false};
  const emit=(name,text,role)=>{
   const file=path.join(out,name);
   fs.writeFileSync(file,text,{flag:'wx'});
   generated(scope,file,role);
   return file;
  };
  if(dispatch){
   const original=emit('notice-root-destroy-case.inc',projection.destroy,'exact current production WndProc WM_DESTROY case');
   const omitted=emit('notice-root-destroy-omitted.inc',projection.omittedDestroy,'single notice shutdown omission from that same case');
   Object.assign(state.exitOwnerBinding,{projectedCase:original,projectedCaseSha256:sha(original),omissionCase:omitted,omissionCaseSha256:sha(omitted),reviewedSha:projection.reviewedSha});
   if(intent){
    const intentCases=emit('notice-root-intent-cases.inc',projection.intentCases,'exact current WndProc message definitions and WM_APP_EXIT, WM_APP_EXIT_READY and WM_CLOSE cases');
    const exitIpc=emit('notice-root-exit-ipc.inc',projection.ipc,'exact current app.exit registration and handler body');
    const exitReceiver=emit('notice-root-exit-receiver.inc',projection.receiver.text,'production IPC parse, permission, lookup, async selection and synchronous reply projected for app.exit');
    const webMessageCallback=emit('notice-root-web-message.inc',projection.callback,'exact callback registered on the main g_view, with a private WebMessage argument');
    Object.assign(state.exitOwnerBinding,{intentCases,intentCasesSha256:sha(intentCases),exitIpc,exitIpcSha256:sha(exitIpc),reviewedIpcSha:projection.reviewedIpcSha,
     exitReceiver,exitReceiverSha256:sha(exitReceiver),receiverOmissions:projection.receiver.omissions,
     webMessageCallback,webMessageCallbackSha256:sha(webMessageCallback),webMessageCallbackSourceOffset:projection.callbackOffset});
   }
  }
 }
 const first=mainText.indexOf('    ipc_on("monitorNotice.reserve"'),last=mainText.indexOf('    ipc_on("monitorNotice.frame"',first);assert(first>=0&&last>first);const handlers=path.join(out,'notice-ipc-handlers.inc');fs.writeFileSync(handlers,mainText.slice(first,last),{flag:'wx'});generated(scope,handlers,'exact reserve/create/retire handlers extracted verbatim from actual main');state.exactHandlerSlice={source:mainFile,sourceSha256:mainSha256,start:first,end:last,generated:handlers,sha256:sha(handlers)};
 if(mutation){const original=path.join(repo,'native/monitor_desktop_hud.h'),originalBytes=fs.readFileSync(original),originalText=originalBytes.toString('utf8'),originalSha256=source(scope,original,originalBytes).row.sha256;const needle=mutation==='skip-retired-create-admission'?'   if(config.noticeOnly&&(!lease||lease!=noticeLeaseSerial||nonce!=noticeLeaseNonce||noticeLeaseRetired))throw std::runtime_error("Notice owner lease retired or replaced");':'   if(s->hwnd&&IsWindow(s->hwnd)||s->inputWindow&&IsWindow(s->inputWindow)){s->reason="owned-window-destruction-unconfirmed";return false;}';assert.equal(originalText.split(needle).length,2);const target=path.join(out,'monitor_desktop_hud.h');fs.writeFileSync(target,originalText.replace(needle,'   /* exact single negative-control guard omitted */'),{flag:'wx'});generated(scope,target,'explicit exact single-guard native semantic rollback');state.mutation={kind:mutation,original:{path:original,sha256:originalSha256},changed:{path:target,sha256:sha(target)},needle};}

 const helperMarker='// Fixed diagnostic state shared by every template instantiation; no resource ownership.';assert.equal(mainText.split(helperMarker).length,2);const helperStart=mainText.indexOf(helperMarker),helperTemplate=mainText.indexOf('template <typename BuildFields>',helperStart),helperEnd=mainText.indexOf('\n}',helperTemplate);assert(helperStart>=0&&helperTemplate>helperStart&&helperEnd>helperTemplate);const helper=path.join(out,'notice-log-helper.inc');fs.writeFileSync(helper,(dispatch?'#define YMCC_TEST_ROOT_DESTROY_DISPATCH 1\n':'')+(intent?'#define YMCC_TEST_ROOT_EXIT_INTENT 1\n':'')+mainText.slice(helperStart,helperEnd+2),{flag:'wx'});generated(scope,helper,'exact main diagnostic helper; actual native retirement semantics remain under original guards');state.exactDiagnosticHelperSlice={source:mainFile,sourceSha256:mainSha256,start:helperStart,end:helperEnd+2,generated:helper,sha256:sha(helper),...(dispatch?{dispatchSelector:true}:{})};
 const intentIncludes=new Set(['notice-root-intent-cases.inc','notice-root-exit-ipc.inc','notice-root-exit-receiver.inc','notice-root-web-message.inc']);
 function bindQuoted(file){file=fs.realpathSync(file);const key=file.toLowerCase();if(beforeNative.has(key))return;const bytes=fs.readFileSync(file);const sha256=file.startsWith(out+path.sep)?createHash('sha256').update(bytes).digest('hex'):source(scope,file,bytes).row.sha256;beforeNative.set(key,{path:file,sha256});for(const match of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"]+)"/gm)){if(match[1]==='notice-root-web-registration.inc'||(!dispatch&&match[1]==='notice-root-destroy-fixture.h')||(!intent&&intentIncludes.has(match[1])))continue;const choices=[path.resolve(path.dirname(file),match[1]),path.resolve(out,match[1]),path.resolve(repo,'native',match[1]),path.resolve(repo,'deps/json',match[1]),path.resolve(repo,'deps/webview2/build/native/include',match[1])];const chosen=choices.find(fs.existsSync);assert(chosen,'QUOTED_NATIVE_INPUT_NOT_FOUND '+match[1]);bindQuoted(chosen);}}
 bindQuoted(fixture);state.beforeNativeInputs=[...beforeNative.values()];saveState();
 const exe=await compile(scope,fixture,'NativeNoticeLease.exe');
 const graph=JSON.parse(fs.readFileSync(path.join(out,'native-build-graph.json'),'utf8'));
 for(const pin of graph.nativeInputs.filter(x=>x.role==='source')){assert(beforeNative.has(fs.realpathSync(pin.path).toLowerCase()),'COMPILER_SOURCE_NOT_BOUND_BEFORE '+pin.path);assert.equal(beforeNative.get(fs.realpathSync(pin.path).toLowerCase()).sha256,pin.sha256);}
 for(const pin of beforeNative.values())assert.equal(sha(pin.path),pin.sha256,'POST_COMPILE_SOURCE_CHANGED '+pin.path);
 if(intent){
  for(const name of ['exitIpc','exitReceiver','webMessageCallback']){
   const file=fs.realpathSync(state.exitOwnerBinding[name]).toLowerCase(),expected=state.exitOwnerBinding[name+'Sha256'];
   const pin=graph.nativeInputs.find(row=>row.role==='generated'&&path.resolve(row.path).toLowerCase()===file);
   assert.equal(beforeNative.get(file)?.sha256,expected,`EXIT_IPC_INPUT_NOT_BOUND ${name}`);
   assert.equal(pin?.sha256,expected,`COMPILER_DID_NOT_OBSERVE_EXIT_IPC_INPUT ${name}`);
  }
 }
 state.status='OWNED_NATIVE_LEASE_FIXTURE_EXECUTING';state.exe={path:exe,sha256:sha(exe)};saveState();
 const raw=path.join(out,'native-result.json');let execution,rejected=false;
 try{execution=await run(exe,dispatch||shutdown?[raw,`--${args[3]}`]:[raw],{cwd:out,windowsHide:true,timeout:15000,maxBuffer:2e6});}catch(error){execution=error;rejected=true;}
 fs.writeFileSync(path.join(out,'run.log'),(execution.stdout??'')+(execution.stderr??''),{flag:'wx'});
 state.execution={exitCode:rejected?(Number.isInteger(execution.code)?execution.code:null):0,signal:execution.signal??null,killed:execution.killed??false,
  ...(rejected&&typeof execution.code==='string'?{errorCode:execution.code}:{})};
 assert.equal(state.execution.killed,false,'NATIVE_LEASE_EXECUTION_KILLED');
 assert.equal(state.execution.signal,null,'NATIVE_LEASE_EXECUTION_SIGNALED');
 assert.equal(rejected,Boolean(mutation),'NATIVE_LEASE_EXECUTION_UNEXPECTED_OUTCOME');
 assert.equal(state.execution.exitCode,mutation?2:0,'NATIVE_LEASE_EXECUTION_UNEXPECTED_EXIT');
 const result=JSON.parse(fs.readFileSync(raw,'utf8'));
 const expectedCount=intent?1:dispatch?3:shutdown?2:10;
 assert.equal(result.counts.cases,expectedCount);
 if(!mutation){
  assert.equal(result.status,'PASS');assert.equal(result.counts.passed,expectedCount);
  assert.equal(result.remainingOwnedWindows,0);assert.equal(result.controllerLive,0);assert.equal(result.unownedDestroyAttempts,0);
  if(shutdown){
   assert.deepEqual(result.cases.map(c=>c.id),['native-shutdown-destroys-active-owned-notice-before-destructor','omitting-native-shutdown-leaves-active-notice-until-fixture-rescue']);
   assert.equal(result.actualDestroyWindowSuccesses,4);assert.equal(result.controllerCloseCalls,2);
   assert.equal(result.realWebView2ControllerExecuted,false);
  }
  if(intent){
   assert.deepEqual(result.cases.map(c=>c.id),['resident-close-preserves-notice-explicit-exit-retires-after-ready']);
   assert.deepEqual(result.intentStages,['tray-hide','taskbar-hide','ready-without-intent','explicit-exit','unsignaled-cleanup','local-owner-pending','ready-destroys']);
   assert.equal(result.appExitIpc.handler,'app.exit');assert.equal(result.appExitIpc.reply,true);
   assert.equal(result.appExitIpc.queuedMessages,1);assert.equal(result.appExitIpc.code,73);
   assert.deepEqual(result.appExitIpc.sourceCallback,{messagesRead:4,witnessConsumed:1,witnessPassThrough:1});
   assert(result.appExitIpc.workerThreadId>0&&result.appExitIpc.windowThreadId>0&&result.appExitIpc.workerThreadId!==result.appExitIpc.windowThreadId);
   assert.deepEqual(result.appExitIpc.diagnostic,{event:'app-exit-ipc',code:73,threadId:result.appExitIpc.workerThreadId,windowAlive:true});
   const ui=result.appExitIpc.syncReceiver;
   assert.deepEqual(ui,{
    reply:{id:803,result:true},permissionReply:{id:801,error:'permission denied: app.exit'},malformedReplyCount:0,
    childId:0,threadId:result.appExitIpc.windowThreadId,rootAliveAtReply:true,exitRequestedAtReply:false,
    diagnostic:{event:'app-exit-ipc',code:73,threadId:result.appExitIpc.windowThreadId,windowAlive:true},
   });
   assert.equal(result.rootDestroyDeliveries,1);assert.equal(result.ownedRootWindowsRemaining,0);
   assert.equal(result.actualDestroyWindowSuccesses,2);assert.equal(result.controllerCloseCalls,1);
   assert.equal(result.realWebView2ControllerExecuted,false);
   state.exitOwnerBinding.mainMessageDispatchExecuted=true;
   state.exitOwnerBinding.dispatchScope='Exact main WebView callback and projected synchronous IPC receiver on the private HWND thread, exact app.exit handler and WM_CLOSE, WM_APP_EXIT, WM_APP_EXIT_READY and WM_DESTROY cases. Direct worker call retained as a queue-only control. WebView and its event args, wallpaper witness, registration storage, reply transport, async cleanup and other owners are fixture substitutes.';
  }else if(dispatch){
   assert.deepEqual(result.cases.map(c=>c.id),['wm-destroy-normal-retires-active-notice','wm-destroy-unexpected-retires-active-notice','wm-destroy-omitted-call-leaves-active-notice']);
   assert.equal(result.rootDestroyDeliveries,3);assert.equal(result.ownedRootWindowsRemaining,0);
   assert.equal(result.actualDestroyWindowSuccesses,6);assert.equal(result.controllerCloseCalls,3);
   assert.equal(result.realWebView2ControllerExecuted,false);
   state.exitOwnerBinding.mainMessageDispatchExecuted=true;
   state.exitOwnerBinding.dispatchScope='Exact projected WM_DESTROY case on three real private root HWNDs; other owners mocked; installed app not started';
  }
 }else{
  assert.equal(result.status,'FAIL');const ids=result.cases.filter(c=>!c.passed).map(c=>c.id);
  assert(mutation==='skip-retired-create-admission'?ids.includes('retirement-before-create-revokes-native-admission-without-a-window'):ids.includes('first-destroywindow-failure-keeps-owner-for-exact-retirement-retry'));state.expectedNegativeFailures=ids;
 }
 state.status=intent?'PROJECTED_PRODUCTION_EXIT_INTENT_TO_NOTICE_DESTROY_PASS':dispatch?'PROJECTED_PRODUCTION_WM_DESTROY_DISPATCH_PASS':shutdown?'ACTUAL_CARRIER_SHUTDOWN_OWNED_WINDOWS_TIMER_AND_COM_ABI_PASS':mutation?'NATIVE_LEASE_SINGLE_GUARD_NEGATIVE_DETECTED_ORIGINAL_FAILURES_RETAINED':'ACTUAL_PRODUCTION_CARRIER_HIDDEN_HWND_TIMER_COM_ABI_AND_EXACT_MAIN_LEASE10_PASS';
 state.finishedAt=new Date().toISOString();state.result={path:raw,sha256:sha(raw)};state.counts=result.counts;state.toolProof=verify(scope);state.receipt=receipt(scope,{status:state.status,boundary:result});saveState();
 console.log(JSON.stringify({status:state.status,counts:state.counts,actualDestroyWindowSuccesses:result.actualDestroyWindowSuccesses,toolProof:state.toolProof,out}));
}catch(error){state.status='NATIVE_LEASE_FIXTURE_FAILURE_RETAINED';state.error=String(error.stack??error);state.finishedAt=new Date().toISOString();saveState();throw error;}
