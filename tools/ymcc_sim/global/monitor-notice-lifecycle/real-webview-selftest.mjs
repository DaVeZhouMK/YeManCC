import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {begin,compile,verify,receipt,sha,save,source,generated,unique,REAL_MONITOR_RUNTIME_LIMIT as LIMIT,executeReviewed,reviewedProcessOutput,saveReviewedProcess} from '../../../fixtures/radial_private/scope.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..'),entry='tools/ymcc_sim/global/monitor-notice-lifecycle/real-webview-selftest.mjs',argv=process.argv.slice(2),option=name=>{const i=argv.indexOf(name);return i<0?null:argv[i+1];},out=path.resolve(option('--out')??''),prepare=argv.includes('--prepare-only');
const scopeOption=option('--scope'),rootExit=scopeOption==='shutdown-intent';
assert(!argv.includes('--scope')||rootExit,'UNKNOWN_REAL_WEBVIEW_SCOPE');
assert(option('--out')&&path.isAbsolute(option('--out'))&&[true,false].includes(prepare));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'')),key=file=>path.resolve(file).toLowerCase(),runtimeRoot=path.resolve(option('--runtime')??''),runtimeVersion=option('--runtime-version');assert(option('--runtime')&&path.isAbsolute(option('--runtime'))&&/^\d+\.\d+\.\d+\.\d+$/.test(runtimeVersion??''));
function runtimePins(){assert.equal(key(fs.realpathSync(runtimeRoot)),key(runtimeRoot));const entries=fs.readdirSync(runtimeRoot,{recursive:true,withFileTypes:true});assert(entries.every(e=>!e.isSymbolicLink()),'RUNTIME_REPARSE_REFUSED');return entries.filter(e=>e.isFile()).map(e=>{const file=path.join(e.parentPath??e.path,e.name);assert.equal(key(fs.realpathSync(file)),key(file));return {path:path.relative(runtimeRoot,file).replaceAll('\\','/'),sha256:sha(file)};}).sort((a,b)=>a.path.localeCompare(b.path));}
async function prepareRootExit(web){
 const projectionFile=path.join(import.meta.dirname,'notice-root-exit-projection.mjs'),main=path.join(repo,'native/main.cpp');
 const bytes=fs.readFileSync(main);source(scope,main,bytes);source(scope,projectionFile);
 const {projectNoticeRootExit}=await import('./notice-root-exit-projection.mjs');
 const text=bytes.toString('utf8'),p=projectNoticeRootExit(text,{dispatch:true,intent:true});
 const first='// Fixed diagnostic state shared by every template instantiation; no resource ownership.';
 const start=text.indexOf(first),template=text.indexOf('template <typename BuildFields>',start),end=text.indexOf('\n}',template);
 assert(start>=0&&start===text.lastIndexOf(first)&&template>start&&end>template,'MISSING_PRODUCTION_NOTICE_LOG_HELPER');
 const files={
  'notice-root-destroy-case.inc':p.destroy,'notice-root-destroy-omitted.inc':p.omittedDestroy,
  'notice-root-intent-cases.inc':p.intentCases,'notice-root-exit-ipc.inc':p.ipc,
  'notice-root-exit-receiver.inc':p.receiver.text,'notice-root-web-registration.inc':p.registration+'\n',
  'notice-log-helper.inc':text.slice(start,end+2)+'\n',
 };
 for(const [name,content] of Object.entries(files)){
  const file=path.join(out,name);fs.writeFileSync(file,content,{flag:'wx'});generated(scope,file,'current exit projection');
 }
 const wrapper=path.join(out,'root-exit.cpp');
 const local=name=>path.join(import.meta.dirname,name).replaceAll('\\','/');
 fs.writeFileSync(wrapper,`#define YMCC_TEST_ROOT_EXIT_INTENT 1\n#define YMCC_TEST_REAL_ROOT_EXIT 1\n#define YMCC_TEST_REAL_ROOT_EXIT_HEADER "${local('real-webview-root-exit.h')}"\n#include "${local('real-webview-selftest.cpp')}"\n`,{flag:'wx'});
 generated(scope,wrapper,'explicit root exit scope of the original private worker');
 const page=path.join(web,'root-exit.html');
 fs.writeFileSync(page,`<!doctype html><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; connect-src 'none'"><script>
const port=window.chrome.webview;
const echo=value=>port.postMessage({fixtureReply:value});
const request=id=>port.postMessage({id,cmd:'app.exit',args:{code:73}});
port.addEventListener('message',({data})=>{
 if(data.fixtureSend==='deny')request(801);
 else if(data.fixtureSend==='rejected'){port.postMessage(42);request(804);echo('fence');}
 else if(data.fixtureSend==='allow')request(803);
 else echo(data);
});
echo('ready');
</script>`,{flag:'wx'});
 generated(scope,page,'private fixed local IPC exchange; no product renderer or external resources');
 return wrapper;
}
const executionOut=reviewedProcessOutput({repo,preparedOut:out,argv,prepare,recordName:'REAL-WEBVIEW-EXECUTE-PROCESS.json',inputs:[runtimeRoot]});
const record=path.join(executionOut,prepare?'REAL-WEBVIEW-PREPARE-PROCESS.json':'REAL-WEBVIEW-EXECUTE-PROCESS.json'),scope=begin(repo,out,entry,{resume:!prepare});let state={startedAt:new Date().toISOString(),status:prepare?'REAL_WEBVIEW_NATIVE_PREPARING':'REAL_WEBVIEW_EXPLICIT_PARENT_REVIEW_ADMISSION',out,executionOut,runtimeRoot,runtimeVersion,normalYmccStarted:false,operatingSystemSleep:false,manualSleepAcceptanceReady:false,wholeGoalComplete:false};const saveState=(final=false)=>saveReviewedProcess(record,state,prepare,final);saveState();
try{
 if(prepare){
  const pins=runtimePins();assert(pins.some(x=>x.path==='msedgewebview2.exe'));scope.toolArtifacts=unique([...scope.toolArtifacts,...pins.map(x=>({path:path.join(runtimeRoot,x.path),sha256:x.sha256}))]);assert(scope.toolArtifacts.length<=LIMIT);state.runtimeFileCount=pins.length;saveState();
  const manifest=path.join(out,'explicit-runtime-manifest.json');save(manifest,{schemaVersion:1,runtimeRoot,runtimeVersion,runtimeFiles:pins,membership:'complete-recursive-files-no-reparse',runtimeDownloaded:false});generated(scope,manifest,'complete explicit existing WebView2 runtime membership');
  const web=path.join(out,'owned-web');fs.mkdirSync(web);const html=path.join(web,'notice-output.html');fs.writeFileSync(html,`<!doctype html><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; connect-src 'none'; img-src 'none'; style-src 'none'"><title>Private lifecycle handshake only</title><script>const params=new URLSearchParams(location.search);window.chrome.webview.postMessage({id:1,cmd:'desktopHud.ready',args:{hudId:Number(params.get('hudId')),nonce:params.get('nonce')}});</script>`,{flag:'wx'});generated(scope,html,'private synthetic local renderer ready handshake; no App or product renderer');const frame=path.join(out,'frame.json');save(frame,{syntheticLocalRendererHandshake:true,noDeviceData:true});generated(scope,frame,'explicit unused compatibility argument to existing private runner');
  const beforeNative=new Map();function bindQuoted(file){file=fs.realpathSync(file);if(beforeNative.has(key(file)))return;const nativePin=scope.native.pins.get(key(file));if(nativePin){assert.equal(sha(file),nativePin.sha256,'PREDECLARED_NATIVE_TOOL_CHANGED '+file);beforeNative.set(key(file),{path:file,sha256:nativePin.sha256,role:'already-bound-native-tool'});return;}
   const bytes=fs.readFileSync(file),compiled=scope.generated.find(row=>key(row.path)===key(file));
   if(compiled)assert.equal(sha(file),compiled.sha256,'GENERATED_NATIVE_INPUT_CHANGED '+file);else source(scope,file,bytes);
   beforeNative.set(key(file),{path:file,sha256:sha(file)});
   for(const match of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"]+)"/gm)){
    if(rootExit&&match[1]==='notice-root-web-message.inc')continue;
    const target=[path.resolve(path.dirname(file),match[1]),path.resolve(out,match[1]),path.resolve(repo,'native',match[1]),path.resolve(repo,'deps/json',match[1]),path.resolve(repo,'deps/webview2/build/native/include',match[1])].find(fs.existsSync);
    assert(target,'UNRESOLVED_QUOTED_NATIVE_INPUT '+match[1]);bindQuoted(target);
   }
  }
  const fixture=rootExit?await prepareRootExit(web):path.join(import.meta.dirname,'real-webview-selftest.cpp');
  state.scope=rootExit?'shutdown-intent':'original';bindQuoted(fixture);
  if(rootExit)bindQuoted(path.join(import.meta.dirname,'real-webview-root-exit.h'));
  state.beforeNativeInputs=[...beforeNative.values()];saveState();
  const exe=await compile(scope,fixture,'RealWebViewNotice.exe'),graph=read(path.join(out,'native-build-graph.json'));
  for(const input of graph.nativeInputs.filter(x=>['source','generated'].includes(x.role)))assert.equal(beforeNative.get(key(fs.realpathSync(input.path)))?.sha256,input.sha256,'COMPILER_SOURCE_NOT_BOUND_BEFORE '+input.path);
  if(rootExit)for(const name of ['notice-root-destroy-case.inc','notice-root-intent-cases.inc','notice-root-exit-ipc.inc','notice-root-exit-receiver.inc','notice-root-web-registration.inc'])
   assert(graph.nativeInputs.some(row=>row.role==='generated'&&key(row.path)===key(path.join(out,name))),'EXIT_INPUT_NOT_OBSERVED '+name);
  for(const pin of beforeNative.values())assert.equal(sha(pin.path),pin.sha256);
  const post=runtimePins();assert.deepEqual(post,pins,'RUNTIME_CHANGED_DURING_PREPARE');state.status='REAL_WEBVIEW_NATIVE_PREPARED_REQUIRES_EXPLICIT_EXACT_PARENT_REVIEW';state.exe={path:exe,sha256:sha(exe)};state.web=web;state.runtimeManifest={path:manifest,sha256:sha(manifest)};state.scopeReceipt=receipt(scope);state.scopeObserved=[...scope.observed];state.toolProof=verify(scope);state.finishedAt=new Date().toISOString();saveState(true);console.log(JSON.stringify({status:state.status,exe:state.exe,runtimeFiles:pins.length,toolProof:state.toolProof,record}));
 }else{
  const prepared=read(path.join(out,'REAL-WEBVIEW-PREPARE-PROCESS.json'));assert.equal(prepared.status,'REAL_WEBVIEW_NATIVE_PREPARED_REQUIRES_EXPLICIT_EXACT_PARENT_REVIEW');assert.equal(prepared.scope??'original',rootExit?'shutdown-intent':'original');assert.equal(sha(prepared.exe.path),prepared.exe.sha256);assert.equal(sha(prepared.runtimeManifest.path),prepared.runtimeManifest.sha256);const manifest=read(prepared.runtimeManifest.path);assert.equal(key(manifest.runtimeRoot),key(runtimeRoot));assert.equal(manifest.runtimeVersion,runtimeVersion);assert.deepEqual(runtimePins(),manifest.runtimeFiles);
  for(const [file,digest]of Object.entries(prepared.scopeReceipt.sourceHashes))assert.equal(sha(path.isAbsolute(file)?file:path.join(repo,file)),digest,'PREPARED_SOURCE_CHANGED '+file);scope.sourceHashes={...prepared.scopeReceipt.sourceHashes};scope.generated=prepared.scopeReceipt.compiledArtifacts;scope.observed=new Map(prepared.scopeObserved);scope.toolArtifacts=unique([...scope.toolArtifacts,...manifest.runtimeFiles.map(x=>({path:path.join(runtimeRoot,x.path),sha256:x.sha256}))]);assert(scope.toolArtifacts.length+scope.generated.length<=LIMIT);verify(scope);
  const reviewed=await executeReviewed(scope,prepared.exe.path,prepared.web,{executionOut});const raw=path.join(executionOut,'real-webview-result.json'),launch=path.join(executionOut,'real-webview-private-launch.json'),actual=read(raw),launcher=read(launch);state.actual=actual;state.launch=launcher;state.executionBoundary=reviewed;state.result={path:raw,sha256:sha(raw)};state.launchReceipt={path:launch,sha256:sha(launch)};saveState();assert.equal(actual.status,'PASS');assert.equal(launcher.status,'PASS');assert.equal(launcher.workerExitCode,0);assert.equal(launcher.forcedTermination,false);assert.equal(launcher.privateDesktopClosed,true);assert.equal(actual.controllerTestDouble,false);assert.equal(actual.startedActualCompositionControllers,rootExit?1:9);assert.equal(actual.completedActualCompositionControllers,rootExit?1:9);assert.equal(actual.browserNaturalExitBeforeJobClose,true);assert.equal(actual.remainingNativeHooks,0);assert.equal(actual.remainingNativeOwners,0);assert(actual.cases.length>10&&actual.cases.every(x=>x.passed));
  if(rootExit){assert.equal(launcher.jobZeroBeforeClose,true);assert.equal(actual.rootBrowserExchange.queuedExitMessages,1);assert.deepEqual(actual.rootBrowserExchange.receivedReplies,[{id:801,error:'permission denied: app.exit'},{id:803,result:true}]);}
  assert.deepEqual(runtimePins(),manifest.runtimeFiles);state.toolProof=verify(scope);state.scopeReceipt=receipt(scope);state.status=rootExit?'ACTUAL_WEBVIEW_ROOT_EXIT_AND_NOTICE_RETURN':'ACTUAL_REAL_WEBVIEW_NINE_CONTROLLERS_RETIRED_LATE_CALLBACKS_AND_NATURAL_BROWSER_EXIT_VERIFIED';state.finishedAt=new Date().toISOString();saveState(true);console.log(JSON.stringify({status:state.status,checks:actual.checks,actualCompositionControllers:actual.completedActualCompositionControllers,naturalBrowserExit:true,toolProof:state.toolProof,record}));
 }
}catch(error){state.status=prepare?'REAL_WEBVIEW_PREPARE_FAILURE_RETAINED':'REAL_WEBVIEW_EXECUTION_FAILURE_RETAINED';state.error=String(error.stack??error);state.finishedAt=new Date().toISOString();state.nativeLaunchAttempted=scope.nativeLaunchAttempted??false;state.nativeExecutionStarted=scope.nativeExecutionStarted??false;saveState(true);throw error;}
