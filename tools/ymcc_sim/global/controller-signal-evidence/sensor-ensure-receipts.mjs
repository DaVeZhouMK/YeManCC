import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const need=(value,code)=>assert.ok(value,code);
const digest=value=>createHash('sha256').update(value).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const normalize=value=>path.resolve(value).toLowerCase();
const schema=read(new URL('./consumer-schema.json',import.meta.url)).sensorEnsure;
const modulePath='tools/ymcc_sim/global/controller-signal-evidence';
const hex=/^[a-f0-9]{64}$/;
function sameKeys(actual,expected,code){need(actual&&Object.keys(actual).length===expected.length&&expected.every(key=>Object.hasOwn(actual,key)),code);}
function within(file,root,code){need(typeof file==='string'&&path.isAbsolute(file),code);const rel=path.relative(root,file);need(rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep),code);}
export function sensorEnsureFragments(text){
 const code=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' '));
 return Object.fromEntries(schema.functions.map(name=>{
  const matches=[...code.matchAll(new RegExp('^static[^\\n;{}]*\\b'+name+'\\s*\\([^;{}]*\\)\\s*(?:noexcept\\s*)?\\{','gm'))];
  need(matches.length===1,'SENSOR_ENSURE_COMPLETE_FUNCTION_REQUIRED:'+name);
  const start=matches[0].index;let end=start+matches[0][0].length,depth=1;
  while(depth&&end<code.length){depth+=(code[end]==='{')-(code[end]==='}');end++;}
  need(depth===0,'SENSOR_ENSURE_UNCLOSED_FUNCTION:'+name);
  const fragment=text.slice(start,end);return [name,{text:fragment,line:text.slice(0,start).split('\n').length,sha256:digest(fragment)}];
 }));
}
function mutated(text,variant){
 const pattern=variant==='restore-unchecked-close'?/if\s*\(!CloseHandle\(g_sensorThread\)\)\s*return\s+rejected\("thread-close",state,GetLastError\(\)\);/g:/state\s*==\s*WAIT_TIMEOUT/g;
 need([...text.matchAll(pattern)].length===1,'SENSOR_ENSURE_MUTATION_ANCHOR');
 return text.replace(pattern,variant==='restore-unchecked-close'?'CloseHandle(g_sensorThread);':'(state==WAIT_TIMEOUT || state==WAIT_FAILED)');
}
export function validateSensorEnsure(r,{repo,sourceHashes,source,tool,sdk,tools,current}){
 need(r?.status==='PASS'&&r.pass===true&&r.targetedAcceptance===true&&r.sourceCurrent===true&&Array.isArray(r.sourceDrift)&&r.sourceDrift.length===0&&
  r.productionAcceptance===false&&r.wholeLifecycleAcceptance===false&&r.processExitUsedAsCleanup===false&&r.parentLifecycleAcceptance==='INCOMPLETE', 'SENSOR_ENSURE_SCOPE');
 need(r.identityMode==='approved-manifest-and-runtime-source-closure','SENSOR_ENSURE_EXPLICIT_APPROVED_IDENTITY_REQUIRED');
 const manifest=schema.currentMainManifest;
 need(r.approvedIdentityManifest?.path&&normalize(r.approvedIdentityManifest.path)===normalize(manifest.path)&&r.approvedIdentityManifest.sha256===manifest.sha256&&hash(manifest.path)===manifest.sha256,'SENSOR_ENSURE_APPROVED_MANIFEST_PIN');
 const approval=read(manifest.path);
 need(hex.test(approval.mainSha256)&&approval.sourceBindings?.['native/main.cpp']===approval.mainSha256&&r.actualMainSha256===approval.mainSha256&&r.expectedMainSha256===approval.mainSha256&&sourceHashes['native/main.cpp']===approval.mainSha256&&hash(path.join(repo,'native/main.cpp'))===approval.mainSha256,'SENSOR_ENSURE_CURRENT_MAIN_NOT_APPROVED');
 sameKeys(r.runtime_sourceClosure,Object.keys(sourceHashes),'SENSOR_ENSURE_RUNTIME_CLOSURE_INCOMPLETE');
 for(const [name,pin] of Object.entries(sourceHashes))need(r.runtime_sourceClosure[name]===pin,'SENSOR_ENSURE_RUNTIME_CLOSURE_PIN:'+name);
 for(const [name,pin] of Object.entries(approval.sourceBindings))need(sourceHashes[name]===pin,'SENSOR_ENSURE_MANIFEST_SOURCE_BINDING:'+name);
 const runtime=r.runtimeSourceClosureIdentity;
 need(runtime?.path&&path.isAbsolute(runtime.path)&&hash(runtime.path)===runtime.sha256,'SENSOR_ENSURE_RUNTIME_CLOSURE_IDENTITY');
 need(JSON.stringify(read(runtime.path))===JSON.stringify(r.runtime_sourceClosure),'SENSOR_ENSURE_RUNTIME_CLOSURE_BYTES');
 const output=path.dirname(runtime.path),job=read(path.join(output,'../job.json'));
 const ownedBase=path.resolve(repo,'../../../Testing/YMCC/Evidence/VC-CaptureReceipt23/t');within(output,ownedBase,'SENSOR_ENSURE_SHORT_OWNED_WORKSPACE');
 need(job.id==='sensor-ensure'&&normalize(job.buildOutput)===normalize(output)&&job.outputPathLength<=110&&job.tempPathLength<=110&&normalize(job.temp)===normalize(path.join(output,'../t'))&&job.owner?.sha256===hash(job.owner.path),'SENSOR_ENSURE_SHORT_OWNED_WORKSPACE');
 const owner=read(job.owner.path);
 need(normalize(owner.repository)===normalize(repo)&&Object.keys(sourceHashes).length===Object.keys(owner.sourceHashes??{}).length&&Object.entries(sourceHashes).every(([name,pin])=>owner.sourceHashes[name]===pin),'SENSOR_ENSURE_WORKSPACE_SOURCE_IDENTITY');
 within(r.runPath,output,'SENSOR_ENSURE_EXECUTION_OUTSIDE_OWNER');need(normalize(r.snapshotPath)===normalize(path.join(r.runPath,'s')),'SENSOR_ENSURE_SNAPSHOT_PATH');
 need(normalize(r.mainInputPath)===normalize(path.join(repo,'native/main.cpp')),'SENSOR_ENSURE_OLD_SNAPSHOT_AS_CURRENT');
 const bound=source(r.sources,'sensor-ensure');need(bound.size===schema.requiredSources.length&&schema.requiredSources.every(name=>bound.has(name)),'SENSOR_ENSURE_SOURCE_CLOSURE_INCOMPLETE');
 for(const row of r.sources)need(hash(path.join(r.snapshotPath,row.path))===row.sha256,'SENSOR_ENSURE_FROZEN_SOURCE_CHANGED');
 const historical=schema.historicalMain;
 need(r.historicalMainSha256===historical.sha256&&normalize(r.historicalSourcePath)===normalize(historical.path)&&hash(historical.path)===historical.sha256&&hash(path.join(r.runPath,'historical-main.cpp'))===historical.sha256,'SENSOR_ENSURE_HISTORICAL_MAIN_PIN');
 const text=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/^\uFEFF/,''),actual=sensorEnsureFragments(text);
 const old=sensorEnsureFragments(fs.readFileSync(historical.path,'utf8').replace(/^\uFEFF/,''));
 sameKeys(r.extractedFunctions,['current','historical'],'SENSOR_ENSURE_FUNCTION_BUNDLES');
 for(const [kind,parts] of [['current',actual],['historical',old]]){
  sameKeys(r.extractedFunctions[kind],schema.functions,'SENSOR_ENSURE_FOUR_COMPLETE_FRAGMENTS');
  for(const name of schema.functions){const row=r.extractedFunctions[kind][name],expected=parts[name];
   need(row.sha256===expected.sha256&&row.line===expected.line&&hash(path.join(r.runPath,kind+'-'+name+'.cpp'))===expected.sha256,'SENSOR_ENSURE_FRAGMENT_HASH:'+kind+':'+name);
   if(kind==='historical')need(row.sha256===schema.historicalFunctions[name],'SENSOR_ENSURE_HISTORICAL_FUNCTION_PIN:'+name);
  }
 }
 current.hashes(r.extractedFunctions.current,'sensor-ensure');
 need(r.baseEventRegistration?.execution==='SOURCE_ONLY'&&r.baseEventRegistration.event==='input-capture-sensor-owner-rejected'&&r.baseEventRegistration.presentInMain===true&&r.baseEventRegistration.classifierExtracted===true&&r.baseEventRegistration.presentInClassifier===true&&current.fn('nativeLifecycleEventIsBase').includes('"input-capture-sensor-owner-rejected"'),'SENSOR_ENSURE_BASE_EVENT');
 tool(r.compiler?.path,tools.native.compiler,r.compiler?.sha256,'sensor-ensure.compiler');tool(r.compiler?.vcvarsPath,tools.vcvars,r.compiler?.vcvarsSha256,'sensor-ensure.vcvars');sdk(r.compiler?.sdkVersion,'sensor-ensure');
 need(JSON.stringify(r.toolchain?.include)===JSON.stringify(tools.native.include)&&JSON.stringify(r.toolchain?.lib)===JSON.stringify(tools.native.lib),'SENSOR_ENSURE_INCLUDE_LIB_IDENTITY');
 const supplementFile=path.join(repo,schema.supplementalAuthority.path);
 need(hash(supplementFile)===schema.supplementalAuthority.sha256,'SENSOR_ENSURE_SUPPLEMENTAL_AUTHORITY_PIN');
 const expectedTools=new Map();for(const row of [...tools.files,...read(supplementFile).files]){
  const key=normalize(row.path);need(!expectedTools.has(key)||expectedTools.get(key).sha256===row.sha256,'SENSOR_ENSURE_TOOL_IDENTITY_CONFLICT');expectedTools.set(key,row);
 }
 need(r.authorityFilesAfterMatched===true&&Array.isArray(r.authorityFilesBefore)&&r.authorityFilesBefore.length===expectedTools.size&&new Set(r.authorityFilesBefore.map(row=>normalize(row.path))).size===expectedTools.size,'SENSOR_ENSURE_TOOL_CLOSURE');
 const declaredTool=row=>{const expected=expectedTools.get(normalize(row.path));need(expected&&row.sha256===expected.sha256&&hash(row.path)===row.sha256,'SENSOR_ENSURE_OBSERVED_TOOL_PIN');};
 r.authorityFilesBefore.forEach(declaredTool);
 for(const key of ['primaryToolsBefore','primaryToolsAfter']){
  const rows=r[key];need(Array.isArray(rows)&&rows.length===Object.keys(tools.primary).length&&new Set(rows.map(row=>row.role)).size===rows.length,'SENSOR_ENSURE_PRIMARY_TOOLS');
  for(const [role,expected] of Object.entries(tools.primary)){const row=rows.find(v=>v.role===role);need(row,'SENSOR_ENSURE_PRIMARY_ROLE');tool(row.path,expected,row.sha256,'sensor-ensure.'+role);}
 }
 need(JSON.stringify(r.primaryToolsBefore)===JSON.stringify(r.primaryToolsAfter),'SENSOR_ENSURE_PRIMARY_TOOLS_CHANGED');
 need(Array.isArray(r.pythonModules)&&r.pythonModules.length>=60&&new Set(r.pythonModules.map(row=>row.module)).size===r.pythonModules.length,'SENSOR_ENSURE_PYTHON_IMPORT_CLOSURE');r.pythonModules.forEach(declaredTool);
 need(Array.isArray(r.observedNativeDependencies)&&r.observedNativeDependencies.length>=200&&new Set(r.observedNativeDependencies.map(row=>normalize(row.path))).size===r.observedNativeDependencies.length,'SENSOR_ENSURE_NATIVE_INCLUDE_CLOSURE');r.observedNativeDependencies.forEach(declaredTool);
 const observedIncludes=new Map();
 need(Array.isArray(r.rows)&&r.rows.length===16,'SENSOR_ENSURE_4_POSITIVE_12_NEGATIVE_BUILDS');
 for(const profile of schema.profiles)for(const mode of schema.modes)for(const variant of schema.variants){
  const selected=r.rows.filter(row=>row.profile===profile&&row.mode===mode&&row.variant===variant);need(selected.length===1,'SENSOR_ENSURE_BUILD_MATRIX');
  const row=selected[0],positive=variant==='current',counts=schema.expectedCounts[mode][variant],summary=row.summary;
  need(row.buildExit===0&&row.exit===(positive?0:1)&&row.expectedNegative===!positive&&row.accepted===true&&summary?.checks===counts[0]&&summary.failures===counts[1],'SENSOR_ENSURE_BUILD_OR_RUNTIME_OUTCOME');
  const dir=path.join(r.runPath,profile[0]+mode[0]+schema.variants.indexOf(variant));
  const files={cpp:'test.cpp',executable:'test.exe',buildLog:'build.log',runLog:'run.log',dependencies:'deps.json'};
  sameKeys(row.executionPaths,Object.keys(files),'SENSOR_ENSURE_EXECUTION_ARTIFACTS');
  for(const [key,name] of Object.entries(files))need(normalize(row.executionPaths[key])===normalize(path.join(dir,name)),'SENSOR_ENSURE_EXECUTION_PATH');
  const flags=profile==='Release'?['/O2','/MT','/DNDEBUG']:['/Od','/RTC1','/MTd','/D_DEBUG'];
  const compile=[r.compiler.path,'/nologo','/std:c++17','/utf-8','/EHsc','/W4',...flags,
   '/DYMCC_ENSURE_KERNEL='+Number(mode==='kernel'),'/I'+path.join(r.snapshotPath,'native'),'/I'+path.join(r.snapshotPath,'deps/json'),
   '/sourceDependencies',row.executionPaths.dependencies,row.executionPaths.cpp,'/Fo'+path.join(dir,'test.obj'),'/Fe'+row.executionPaths.executable];
  need(JSON.stringify(row.compileCommand)===JSON.stringify(compile)&&JSON.stringify(row.runtimeCommand)===JSON.stringify([row.executionPaths.executable]),'SENSOR_ENSURE_ACTUAL_TOOL_INVOCATION');
  need(hash(row.executionPaths.cpp)===row.inputSha256&&hash(row.executionPaths.executable)===row.executableSha256&&hash(row.executionPaths.buildLog)===row.buildLogSha256&&hash(row.executionPaths.runLog)===row.runLogSha256&&hash(row.executionPaths.dependencies)===row.dependenciesSha256,'SENSOR_ENSURE_EXECUTION_ARTIFACT_HASH');
  const parts=variant==='historical'?old:actual,compiled={};
  const functions=[schema.functions[1],schema.functions[0],schema.functions[2],schema.functions[3]].map(name=>{
   const part=parts[name],fragment=name===schema.functions[0]&&!['current','historical'].includes(variant)?mutated(part.text,variant):part.text;
   compiled[name]=digest(fragment);return '#line '+part.line+' "'+(variant==='historical'?'historical-main.cpp':'native/main.cpp')+'"\n'+fragment;
  });
  sameKeys(row.compiledFunctions,schema.functions,'SENSOR_ENSURE_COMPILED_FUNCTIONS');for(const name of schema.functions)need(row.compiledFunctions[name]===compiled[name],'SENSOR_ENSURE_ACTUAL_COMPILED_FRAGMENT');
  const memory=fs.readFileSync(path.join(repo,'tools/fixtures/input_capture_timer_retirement/sensor_ensure_memory.cpp.in'),'utf8').replace(/^\uFEFF/,''),kernel=fs.readFileSync(path.join(repo,'tools/fixtures/input_capture_timer_retirement/sensor_ensure_kernel.cpp.in'),'utf8').replace(/^\uFEFF/,'');
  const generated=memory.replace('@KERNEL_PORT@',kernel).replace('@ACTUAL@',functions.join('\n\n'));
  need(digest(generated)===row.inputSha256,'SENSOR_ENSURE_WHOLE_FUNCTION_TRANSLATION_UNIT');
  const log=fs.readFileSync(row.executionPaths.runLog,'utf8').replace(/\r+\n/g,'\n');const summaries=[...log.matchAll(/^SUMMARY (\{.*\})\r?$/gm)];
  need(summaries.length===1&&JSON.stringify(JSON.parse(summaries[0][1]))===JSON.stringify(summary),'SENSOR_ENSURE_PREEXIT_SUMMARY_LOG');
  const failed=log.split(/\r?\n/).filter(line=>line.startsWith('FAIL '));need(JSON.stringify(failed)===JSON.stringify(row.failedAssertions)&&failed.length===summary.failures,'SENSOR_ENSURE_FAILED_ASSERTIONS');
  for(const name of schema.scenarioNames)need(log.includes('PASS '+name+' starts with zero owned resources'),'SENSOR_ENSURE_SCENARIO_MISSING:'+name);
  need(summary.threadsCreated===16&&summary.threadsJoined===16&&summary.threadsClosed===16&&summary.eventsCreated===14&&summary.eventsClosed===14&&summary.auxCreated===(mode==='kernel'?16:0)&&summary.auxClosed===summary.auxCreated&&summary.ownedBeforeExit===0&&summary.workerTimeouts===0&&summary.handlesBefore>0&&summary.handlesBefore===summary.handlesAfter,'SENSOR_ENSURE_REAL_JOIN_CLOSE_PRIVATE_ZERO');
  need(summary.orphanOwners===(['historical','restore-unchecked-close'].includes(variant)?1:0),'SENSOR_ENSURE_ORPHAN_SALVAGE_WITNESS');
  if(positive)need(failed.length===0,'SENSOR_ENSURE_POSITIVE_FAILURE');else need(row.requiredNegativeLabel===schema.negativeLabels[variant]&&failed.some(line=>line.includes(schema.negativeLabels[variant])),'SENSOR_ENSURE_SPECIFIED_FAILURE_NOT_CAUGHT');
  const deps=read(row.executionPaths.dependencies).Data.Includes;
  need(Array.isArray(deps)&&deps.length>200,'SENSOR_ENSURE_COMPILER_DEPENDENCIES');
  for(const file of deps){const relative=path.relative(r.snapshotPath,file);
   if(relative&&!path.isAbsolute(relative)&&!relative.startsWith('..')){const name=relative.replaceAll('\\','/');need(bound.has(name)&&hash(file)===sourceHashes[name],'SENSOR_ENSURE_LOCAL_INCLUDE_ESCAPE');}
   else{const expected=expectedTools.get(normalize(file));need(expected&&hash(file)===expected.sha256,'SENSOR_ENSURE_NATIVE_INCLUDE_ESCAPE');observedIncludes.set(normalize(file),expected.sha256);}
  }
 }
 need(observedIncludes.size===r.observedNativeDependencies.length&&r.observedNativeDependencies.every(row=>observedIncludes.get(normalize(row.path))===row.sha256),'SENSOR_ENSURE_NATIVE_INCLUDE_MANIFEST_MISMATCH');
 need(r.remaining?.some(v=>v.includes('UNKNOWN'))&&r.remaining.some(v=>v.includes('NOT_RUN')),'SENSOR_ENSURE_REMAINING_BOUNDARIES');
 return {id:schema.id,pass:true,profiles:2,currentBuilds:4,specifiedNegativeBuilds:12,privateKernelBuilds:2,explicitlyJoinedKernelThreads:32,wholeLifecycleAcceptance:false,parentLifecycleAcceptance:'INCOMPLETE'};
}
