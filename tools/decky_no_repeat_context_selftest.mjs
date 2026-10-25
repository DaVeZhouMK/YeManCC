import {fileURLToPath} from 'node:url';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';
export function noRepeatContextTests(root,out){
 const source=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8');
 const match=source.match(/const std::string launchExpression = requireFreshContext\s*\? ("(?:[^"\\]|\\.)*") \+ expression \+ ("(?:[^"\\]|\\.)*")\s*: expression;/);
 assert(match,'actual production launch-expression gate missing');
 const prefix=JSON.parse(match[1]),suffix=JSON.parse(match[2]),expression='(() => { window.bootstrapWrites++; return "fixture-run"; })()';
 const cases=[];
 for(const row of [{name:'fresh document',loaded:false,loader:false,expected:1},{name:'official loaded flag',loaded:true,loader:false,expected:0},{name:'truthy upstream loaded flag',loaded:1,loader:false,expected:0},{name:'existing Loader object',loaded:false,loader:true,expected:0},{name:'both loaded signals',loaded:true,loader:true,expected:0},{name:'foreign origin',loaded:false,loader:false,foreign:true,expected:0},{name:'subframe',loaded:false,loader:false,frame:true,expected:0}]){
  const window={bootstrapWrites:0,deckyHasLoaded:row.loaded,...(row.loader?{DeckyPluginLoader:{}}:{})};window.top=row.frame?{}:window;
  let steamCommands=0;const context={window,location:{origin:row.foreign?'https://example.invalid':'https://steamloopback.host'},SteamClient:{Browser:{RestartJSContext(){steamCommands++;throw Error('forbidden Steam UI restart')}},UI:{SetUIMode(){steamCommands++;throw Error('forbidden mode switch')}}}};
  const result=vm.runInNewContext(prefix+expression+suffix,context);
  assert.equal(window.bootstrapWrites,row.expected,row.name);assert.equal(steamCommands,0,row.name);
  assert.equal(result,row.expected?'fixture-run':'ymcc-decky-context-already-loaded',row.name);cases.push(row.name);
 }
 // Replay the real, hash-attributed upstream injection on an already loaded
 // document. This proves the restart trigger and the stale-snapshot guard; it
 // deliberately does not invent a ResizeObserver stack for the historical log.
 const fixture=JSON.parse(fs.readFileSync(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/upstream-reinject.json'),'utf8'));
 const lock=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/decky-loader.lock.json'),'utf8'));
 assert.equal(fixture.version,lock.version,'review upstream reinjection contract on Loader upgrade');
 assert.equal(fixture.sourceCommit,lock.sourceCommit,'official helper must match the locked source commit');
 assert.equal(fixture.sourceFileSha256,'1380d802d6fd8bd8e803f5e634b8c74446c9b52bc26c3a21585ba2e15fa1a271');
 const restartCount=(guarded)=>{
  const window={deckyHasLoaded:true,DeckyPluginLoader:{}};window.top=window;
  const timers=[];let restarts=0;
  const context={window,location:{origin:'https://steamloopback.host'},console:{error(e){throw e;}},setTimeout(fn,ms){assert.equal(ms,100);timers.push(fn);},SteamClient:{Browser:{RestartJSContext(){restarts++;}}}};
  // The C++ gate wraps an expression, while the upstream helper is a statement.
  const upstream='(() => { '+fixture.expression+' })()';
  vm.runInNewContext(guarded?prefix+upstream+suffix:upstream,context);
  for(const run of timers)run();return restarts;
 };
 assert.equal(restartCount(false),1,'old policy must reproduce the official JS-context restart trigger');
 assert.equal(restartCount(true),0,'actual-target guard prevents official JS-context restart even after stale admission');
 assert.throws(()=>assert.equal(restartCount(false),0,'loaded-document restart forbidden'),/loaded-document restart forbidden/,'removing the production gate must fail at the restart assertion');
 cases.push('locked official helper reproduces exactly one delayed context restart without guard','actual native expression rejects the official reinjection after a stale snapshot','removed guard fails the no-restart behavioral assertion');
 // Build and execute the ACTUAL C++ retained-target gate, including its exact
 // module/version comparisons; a stale native read cannot authorize another page.
 const retainedLine=source.split('\n').find(line=>line.includes('const std::string retainedGate ='));
 assert(retainedLine,'production retained-target expression missing');
 const retainedCpp=retainedLine.slice(retainedLine.indexOf(' = ')+3).trim().replace(/;$/,'')
  .replace('nlohmann::json(bootstrap.value("frontendBuildId",std::string{})).dump()','JSON.stringify(expected)')
  .replace('nlohmann::json(std::string(ymcc::deckyidentity::version)).dump()','JSON.stringify(version)');
 const retainedExpression=vm.runInNewContext(retainedCpp,{expected:'e'.repeat(64),version:lock.version,expression});
 const retainedRows=[{name:'exact active frontend',writes:1},{name:'different module',module:'f'.repeat(64),writes:0},{name:'inactive module',inactive:true,writes:0},{name:'missing plugin registration',noPlugin:true,writes:0},{name:'missing Loader object',noLoader:true,writes:0},{name:'missing official version',noVersion:true,writes:0},{name:'different official version',version:'v3.2.10',writes:0},{name:'foreign document',foreign:true,writes:0},{name:'subframe',frame:true,writes:0}];
 for(const row of retainedRows){
  let forbidden=0;const window={bootstrapWrites:0,__YMCC_DECKY_FRONTEND__:{active:!row.inactive,buildId:row.module||'e'.repeat(64)},DeckyPluginLoader:{plugins:row.noPlugin?[]:[{name:'ymcc-sidebar'}],deckyState:{publicState:()=>({versionInfo:{current:row.noVersion?undefined:row.version||lock.version}})},importPlugin(){forbidden++;}}};
  window.top=row.frame?{}:window;if(row.noLoader)delete window.DeckyPluginLoader;
  const result=vm.runInNewContext(retainedExpression,{window,location:{origin:row.foreign?'https://example.invalid':'https://steamloopback.host'},Array,SteamClient:{Browser:{RestartJSContext(){forbidden++;}}}});
  assert.equal(window.bootstrapWrites,row.writes,row.name);assert.equal(forbidden,0,row.name+' has zero official Loader lifecycle calls');
  assert.equal(result,row.writes?'fixture-run':'ymcc-decky-retained-frontend-mismatch',row.name);
  cases.push('retained actual target: '+row.name);
 }
 assert(source.includes('retainedFrontendOnly?retainedGate:expression'),'watch must retain exact-target validation across context events');
 cases.push('retained watcher preserves exact module/version target gate');
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),runtime=fs.readFileSync(path.join(root,'native/decky_sidebar_runtime.h'),'utf8');
 const worker=fs.readFileSync(path.join(root,'native/decky_binding_worker.h'),'utf8');
 function actualTargetWiring(host,child){
  assert(/config\.bindingPlan\s*=\s*deckySidebarOwnedBindingPlan\s*;/.test(host),'production typed plan must be the installed caller');
  assert(/\{"requireFreshContext",\s*!retained\s*&&\s*g_deckySidebarRuntime\.snapshot\(\)\.pid\s*==\s*0\}/.test(host),'production launch binding must enable the actual-target gate');
  assert(child.includes('result.requireFreshContext=value.at("requireFreshContext").get<bool>()'),'child must decode the requested fresh-context gate');
  assert(child.includes('result.retainedFrontendOnly=value.at("retainedFrontendOnly").get<bool>()'),'child must decode the exact retained-target gate');
  assert(child.includes('request.requireFreshContext,request.retainedFrontendOnly,request.steam.pid'),'actual child bootstrap must consume both gates and the fixed Steam PID');
 }
 actualTargetWiring(main,worker);
 const missingHostGate=main.replace(/\{"requireFreshContext",\s*!retained\s*&&\s*g_deckySidebarRuntime\.snapshot\(\)\.pid\s*==\s*0\}/,'{"requireFreshContext",false}');
 assert.notEqual(missingHostGate,main,'review typed host gate mutation');
 assert.throws(()=>actualTargetWiring(missingHostGate,worker),/actual-target gate/,'removed typed host guard must fail');
 const missingChildGate=worker.replace('request.requireFreshContext,request.retainedFrontendOnly,request.steam.pid','false,request.retainedFrontendOnly,request.steam.pid');
 assert.notEqual(missingChildGate,worker,'review typed child gate mutation');
 assert.throws(()=>actualTargetWiring(main,missingChildGate),/actual child bootstrap/,'child bypass of target guard must fail');
 cases.push('typed host false-fresh mutation rejected','typed child fresh-target bypass rejected');
 assert(runtime.indexOf('LaunchAdmission finalAdmission;')<runtime.indexOf('job=CreateJobObjectW'), 'final admission must precede process creation');
 assert(source.includes('"decky-context-already-loaded"'),'loaded-context rejection needs its own stable non-retry reason');
 cases.push('production before-start binding enables the actual-CDP-target guard','final runtime admission precedes owned process creation');
 const report={ok:true,passed:cases.length,cases,actualProductionExpression:true,officialReinjectionNegativeDetected:true,historicalNullWindowCauseProven:false,realSteamActions:0};fs.writeFileSync(path.join(out,'no-repeat-context.json'),JSON.stringify(report,null,2));return report.passed;
}
if(process.argv[1]&&path.resolve(process.argv[1])===path.resolve(fileURLToPath(import.meta.url))){const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});console.log('PASS no-repeat contexts='+noRepeatContextTests(process.cwd(),out));}
