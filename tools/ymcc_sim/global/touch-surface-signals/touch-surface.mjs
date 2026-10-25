import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import readline from 'node:readline';
import assert from 'node:assert/strict';
import {compileTouchFixture,collectTouchSources,runTouchExecutable,digest} from './native-toolchain.mjs';
import {generateTouchExcerpts} from './source-excerpts.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),defaultRepo=path.resolve(here,'../../../..');
const json=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function bundle(repoRoot,entry,contents){const require=createRequire(path.join(repoRoot,'package.json'));return require('esbuild').build({...(contents?{stdin:{contents,sourcefile:'touch-memory-entry.ts',loader:'ts',resolveDir:repoRoot}}:{entryPoints:[path.join(repoRoot,entry)]}),absWorkingDir:repoRoot,bundle:true,write:false,platform:'node',format:'cjs',metafile:true,logLevel:'silent'});}
async function originalTs(repoRoot){
 const compiled=await bundle(repoRoot,'tools/screen_touchpads_selftest.ts'),module={exports:{}},messages=[];
 const context=vm.createContext({module,exports:module.exports,require(name){assert.equal(name,'node:assert/strict');return assert;},console:{log:(...items)=>messages.push(items.join(' '))}},{codeGeneration:{strings:false,wasm:false}});
 new vm.Script(compiled.outputFiles[0].text).runInContext(context,{timeout:10000});return {status:'PASS',messages};
}
async function bridgeRegression(repoRoot,native){
 const compiled=await bundle(repoRoot,null,`export * from './src/bridge/screenTouchpads';`);
 const child=spawn(native.executable,['--config'],{stdio:['pipe','pipe','pipe'],windowsHide:true});child.stderr.resume();
 const queue=[],records=[],messages=[];let pendingReply;
 const reader=readline.createInterface({input:child.stdout});
 reader.on('line',line=>{const pending=queue.shift();if(!pending)return;try{pending.resolve(JSON.parse(line));}catch(error){pending.reject(error);}});
 child.on('error',error=>{for(const p of queue.splice(0))p.reject(error);});child.on('exit',()=>{for(const p of queue.splice(0))p.reject(Error('TOUCH_MEMORY_HELPER_EXITED'));});
 const call=message=>new Promise((resolve,reject)=>{queue.push({resolve,reject});child.stdin.write(JSON.stringify(message)+'\n');});
 const window={chrome:{webview:{addEventListener(type,listener){assert.equal(type,'message');pendingReply=listener;},postMessage(message){records.push(structuredClone(message));void call(message).then(reply=>pendingReply({data:{id:message.id,...reply}}),()=>pendingReply({data:{id:message.id,error:'TOUCH_MEMORY_IO_CLOSED'}}));}}}};
 const module={exports:{}},context=vm.createContext({module,exports:module.exports,window,console:{log(){},warn(){}},Date,setTimeout,clearTimeout,queueMicrotask},{codeGeneration:{strings:false,wasm:false}});
 new vm.Script(compiled.outputFiles[0].text).runInContext(context,{timeout:10000});const product=module.exports;
 async function run(id,input,invoke,verify){const at=records.length;let value,error;try{value=await invoke();}catch(e){error=e.message;}verify(value,error,records.slice(at));messages.push({id,status:'PASS',provenance:'source-derived-fixture',input,consumer:'screenTouchpads.ts -> ipc.ts -> actual C++ ProfileBank/parseConfig',state:value??null,feedback:{error:error??null,ipc:records.slice(at),physicalOutputVerified:false}});}
 try{
  await run('bridge-native-get-default',{persona:'steamdeck'},()=>product.screenTouchpadsGet('steamdeck'),v=>{assert.equal(v.layout,'dual');assert.equal(v.settingsSaved,false);assert.equal(v.steamDeckAvailable,false);});
  await run('bridge-native-disable',{layout:'off'},()=>product.screenTouchpadsSet({layout:'off'},'steamdeck'),v=>{assert.equal(v.enabled,false);assert.equal(v.layout,'off');});
  await run('bridge-native-enable-single',{layout:'single',singleMode:'mouse'},()=>product.screenTouchpadsSet({layout:'single',singleMode:'mouse'},'disabled'),v=>{assert.equal(v.enabled,true);assert.equal(v.layout,'single');});
  await run('bridge-invalid-field-rejected-before-IPC',{scale:52},()=>product.screenTouchpadsSet({scale:52},'steamdeck'),(v,e,c)=>{assert(e);assert.equal(c.length,0);});
  await run('bridge-persona-ownership-rejected-before-IPC',{persona:'disabled',leftMode:'steamdeck'},()=>product.screenTouchpadsSet({leftMode:'steamdeck'},'disabled'),(v,e,c)=>{assert(e);assert.equal(c.length,0);});
  await run('bridge-rear-output-ownership-rejected',{persona:'elite',rearMask:1},()=>product.screenTouchpadsSet({rearMask:1},'elite'),(v,e,c)=>{assert(e);assert.equal(c.length,0);});
  await run('bridge-native-contradictory-enabled-layout-rejected',{layout:'off',enabled:true},()=>product.screenTouchpadsSet({layout:'off',enabled:true},'steamdeck'),(v,e,c)=>{assert.equal(e,'TOUCH_CONFIG_REJECTED');assert.equal(c.length,1);});
  await run('bridge-feedback-off-acknowledged-in-memory',{touchFeedback:'off'},()=>product.screenTouchpadsSet({touchFeedback:'off'},'dualsense-edge'),v=>{assert.equal(v.touchFeedback,'off');assert.equal(v.settingsSaved,false);});
  return {status:'PASS',checks:messages.length,inputConsumerStateFeedback:messages};
 }finally{child.stdin.end();await new Promise(resolve=>child.once('exit',resolve));reader.close();}
}
export async function runTouchSurfaceRegressions(options={}){
 const repoRoot=fs.realpathSync(options.repoRoot??defaultRepo),workDir=path.resolve(options.workDir??path.join(process.cwd(),'work/touch-surface'));
 const before=collectTouchSources(repoRoot),generated=generateTouchExcerpts(repoRoot),suites=[];
 const custom=await compileTouchFixture({repoRoot,workDir,name:'touch-surface',source:path.join(here,'touch-fixture.cpp'),generated:generated.generated});
 const run=await runTouchExecutable(custom),direct=JSON.parse(run.stdout);
 suites.push({id:'formal-native-four-stage',status:run.exitCode===0&&direct.status==='PASS'?'PASS':'FAIL',result:direct,importAudit:custom.importAudit,executableSha256:custom.executableSha256});
 for(const [name,source]of [['event-time','tools/ymcc_sim/global/touch-surface-signals/legacy-event-time.cpp'],['feedback-lease','tools/screen_touchpads_feedback_lease_selftest.cpp'],['partial-send','tools/screen_touchpad_partial_send_selftest.cpp']]){
  const compiled=await compileTouchFixture({repoRoot,workDir,name,source:path.join(repoRoot,source),generated:name==='partial-send'?generated.generated:{}});
  const execution=await runTouchExecutable(compiled,name==='partial-send'?[]:[path.join(compiled.dir,'result.json')]);
  const result=name==='partial-send'?JSON.parse(execution.stdout.match(/SUMMARY (\{[^\n]+\})/)?.[1]??'{}'):json(path.join(compiled.dir,'result.json'));
  suites.push({id:'original-'+name,status:execution.exitCode===0&&(name==='partial-send'?result.failed===0:result.ok===true)?'PASS':'FAIL',result,importAudit:compiled.importAudit,executableSha256:compiled.executableSha256});
 }
 suites.push({id:'original-frontend-TS',...await originalTs(repoRoot)});
 const bridge=await bridgeRegression(repoRoot,custom);suites.push({id:'formal-bridge-native-config',...bridge});
 const after=collectTouchSources(repoRoot),sourceDrift=before.filter(s=>after.find(a=>a.path===s.path)?.sha256!==s.sha256).map(s=>s.path);
 return {schemaVersion:1,kind:'touch-surface-signals',status:suites.every(s=>s.status==='PASS')&&!sourceDrift.length?'PASS':'FAIL',
  summary:{suites:suites.length,passed:suites.filter(s=>s.status==='PASS').length,nativeScenarios:direct.checks,bridgeScenarios:bridge.checks,originalEventTimeChecks:suites[1].result.cases.length,originalLeaseChecks:suites[2].result.cases.length,originalPartialSendChecks:suites[3].result.checks,originalPartialSendFaultCases:suites[3].result.faultScenarios},
  sourceChanges:[],sourceHashes:before,sourceDrift,
  executedProductModules:[{path:'native/screen_touchpads.h',execution:'actual-header-in-original-event-time-test;byte-identical-Core-config-transport-output-configure-shutdown-excerpts'},
   {path:'native/screen_touchpad_owner.h',execution:'byte-identical-owner-thread-resume-epoch-shutdown-functions'},
   ...['native/screen_touchpad_auto_hide.h','native/screen_touchpad_visibility.h','native/screen_touchpad_display_policy.h','native/touch_haptic_dispatch.h','native/touch_feedback_lease.h','native/msi_dinput_acquire_recovery.h','src/bridge/screenTouchpads.ts','src/bridge/ipc.ts'].map(p=>({path:p,execution:'actual-header-or-unmodified-TS-consumer'}))].map(s=>({...s,sha256:before.find(h=>h.path===s.path)?.sha256})),
  inputConsumerStateFeedback:[...direct.inputConsumerStateFeedback,...bridge.inputConsumerStateFeedback],suites,
  extractedSources:generated.spans.map(({text,...s})=>s),compatibility:{schemaVersion:1,requestContract:'screenTouchpads.get/set;production-validator-and-native-parser',hcVersionPinned:false,hcRequired:false,unknownSourceShape:'FAIL'},
  boundaries:{actualInput:false,actualMouseOrTouchInjection:false,actualWindowsOrHooks:false,actualDeviceOrRumble:false,userSettingsReadOrWritten:false,normalAppStarted:false,hostStarted:false,realOsSleep:false},
  remainingGaps:[{stage:'input-source',code:'WINRT_INDEPENDENT_POINTER_SOURCE_NOT_EXECUTED'},{stage:'physical',code:'MEMORY_LEAF_ACK_IS_NOT_PHYSICAL_INPUT_OR_RUMBLE'},{stage:'ui-native',code:'REAL_HWND_COMPOSITION_HITTEST_MOUSEPASS_NOT_EXECUTED'},{stage:'scope',code:'BUTTON_SURFACES_AND_CURSOR_RENDERING_ARE_LEAF_BOUNDARIES'}]};
}
async function main(){const args=process.argv.slice(2),options={};let output;for(let i=0;i<args.length;i++){const k=args[i],v=args[++i];if(k==='--work-dir')options.workDir=v;else if(k==='--output')output=v;else throw Error('UNKNOWN_TOUCH_ARGUMENT:'+k);}const result=await runTouchSurfaceRegressions(options);if(output){fs.mkdirSync(path.dirname(path.resolve(output)),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');}console.log(JSON.stringify({status:result.status,summary:result.summary,output}));if(result.status!=='PASS')process.exitCode=1;}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await main();

