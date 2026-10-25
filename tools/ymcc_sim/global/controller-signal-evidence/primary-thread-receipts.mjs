import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {compositionContract, prepareCompositionToolchain, sealCompositionNestedClosure, validateCompositionNestedClosure} from './owned-cleanup-composition-receipts.mjs';
const schema=()=>JSON.parse(fs.readFileSync(new URL('./consumer-schema.json',import.meta.url),'utf8'));
const need=(v,c)=>assert.ok(v,c);
const sha=b=>createHash('sha256').update(b).digest('hex');
const hash=p=>sha(fs.readFileSync(p));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const text=p=>fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'');
const norm=p=>path.win32.resolve(p).toLowerCase();
const same=isDeepStrictEqual;
const pin=p=>({path:p,sha256:hash(p)});
const roles={fixture:'fixture.cpp',object:'fixture.obj',command:'command.json',dependencies:'dependencies.json',executable:'fixture.exe',runtime:'RESULT.json',buildLog:'build.log',runLog:'run.log',imports:'imports.txt'};
function keys(value,names,code){need(value&&same(Object.keys(value).sort(),[...names].sort()),code);}
function inside(file,root,code){need(typeof file==='string'&&path.isAbsolute(file),code);const rel=path.relative(root,file);need(rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep),code);}
export function primaryFunction(text,name){
 const masked=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' '));
 const hits=[...masked.matchAll(new RegExp('^static\\s+(?:bool|void)\\s+'+name+'\\s*\\([^;]*?\\)\\s*(?:noexcept\\s*)?\\{','gm'))];need(hits.length===1,'PRIMARY_COMPLETE_FUNCTION:'+name);
 const start=hits[0].index;let at=masked.indexOf('{',start)+1,depth=1;while(depth&&at<masked.length){depth+=(masked[at]==='{')-(masked[at]==='}');at++;}need(depth===0,'PRIMARY_UNCLOSED_FUNCTION:'+name);
 const value=text.slice(start,at);return {text:value,sha256:sha(value),line:masked.slice(0,start).split('\n').length};
}
function adoption(t,current){const anchor=current?'    g_inputHostPrimaryThread = pi.hThread;':'    CloseHandle(pi.hThread);';const at=t.indexOf(anchor,t.indexOf('static bool inputHostStart(')),end=t.indexOf('    json helloReceipt;',at);need(at>=0&&end>at,'PRIMARY_ACTUAL_ADOPTION_BOUNDARY');return t.slice(at,end);}
export function primaryContract(repo){
 const c=schema().primaryThread;
 for(const name of ['historicalStart','historicalManifest','basisResult','basisSourceFreeze','producerBeforeBinding'])need(hash(c[name].path)===c[name].sha256,'PRIMARY_FIXED_PIN:'+name);
 const old=read(c.historicalManifest.path).inputs.find(r=>r.path==='native/virtual_controller_start_runtime.h');need(old?.sha256===c.historicalStart.sha256,'PRIMARY_HISTORICAL_START_MANIFEST');
 for(const row of c.deliveredSources)need(hash(path.join(repo,row.path))===row.sha256,'PRIMARY_DELIVERED_CONSUMER_CHANGED:'+row.path);
 const basis=read(c.basisResult.path);need(basis.pass===true&&basis.runs.length===14,'PRIMARY_BASIS_MATRIX');for(const v of c.variants)for(const profile of c.profiles)need(same(basis.runs.find(r=>r.profile===profile&&r.variant===v)?.runtime,c.expectedRuntime[v]),'PRIMARY_PRESERVED_BASELINE:'+v);
 need(c.sourceInputs.length===13&&c.variants.length===7&&Object.keys(c.negativeLabels).length===6&&c.profiles.length===2&&c.builds===14&&c.positiveChecksPerProfile===31,'PRIMARY_SCHEMA_MATRIX');
 return c;
}
export function primaryArtifactRows(repo){const c=primaryContract(repo);return ['basisResult','basisSourceFreeze','producerBeforeBinding'].map(name=>({...c[name],origin:'primary-thread.'+name}));}
export function primaryArgs(repo,owned,view,closure,identity){return ['-B',path.join(repo,schema().primaryThread.consumer),'--out',owned.buildOutput,'--toolchain',view.path,'--runtime_sourceClosure',closure,'--source-identity-manifest',identity.path,'--source-identity-sha256',identity.sha256];}
export async function executePrimaryThread({repo,sourceHashes,workspace,run,identity,python,toolchainAuthorityRepo=repo}){
 primaryContract(repo);need(hash(identity.path)===identity.sha256,'PRIMARY_APPROVAL_PIN');
 const approval=read(identity.path);need(!approval.sourceRepository||norm(approval.sourceRepository)===norm(repo),'PRIMARY_APPROVED_SOURCE_REPOSITORY');need(norm(approval.toolchainAuthorityRepository??repo)===norm(toolchainAuthorityRepo),'PRIMARY_APPROVED_TOOLCHAIN_REPOSITORY');need(approval.mainSha256===sourceHashes['native/main.cpp']&&approval.sourceBindings?.['native/main.cpp']===approval.mainSha256,'PRIMARY_MAIN_NOT_APPROVED');
 for(const [n,h] of Object.entries(approval.sourceBindings))need(sourceHashes[n]===h,'PRIMARY_APPROVED_SOURCE_BINDING:'+n);
 const owned=workspace.allocate('primary-thread'),toolchainView=prepareCompositionToolchain({repo:toolchainAuthorityRepo,owned}),closure=path.join(owned.directory,'runtime_sourceClosure.json');fs.writeFileSync(closure,JSON.stringify(sourceHashes,null,2)+'\n',{flag:'wx'});
 const nestedToolClosure={before:sealCompositionNestedClosure({repo:toolchainAuthorityRepo,owned,sourceHashes,phase:'before'})};let job;
 try{job=await run('primary-thread',python,primaryArgs(repo,owned,toolchainView,closure,identity),0,{},owned);}
 finally{nestedToolClosure.after=sealCompositionNestedClosure({repo:toolchainAuthorityRepo,owned,sourceHashes,phase:'after'});}
 for(const [n,h] of Object.entries(sourceHashes))need(hash(path.join(repo,n))===h,'PRIMARY_RUNTIME_SOURCE_CHANGED:'+n);
 need(hash(identity.path)===identity.sha256,'PRIMARY_APPROVAL_CHANGED');
 const file=path.join(owned.buildOutput,'RESULT.json'),receipt=read(file),freeze=path.join(owned.buildOutput,'SOURCE-FREEZE.json');
 const artifacts=(receipt.runs??[]).map(row=>({profile:row.profile,variant:row.variant,files:Object.fromEntries(Object.entries(roles).map(([role,name])=>[role,pin(path.join(owned.buildOutput,row.profile+'-'+row.variant,name))]))}));
 const context={schemaVersion:1,kind:'controller-signal-primary-thread',sourceRepository:repo,toolchainAuthorityRepository:toolchainAuthorityRepo,sourceHashes,ownedWorkspace:owned,approvedIdentityManifest:identity,runtimeSourceClosure:pin(closure),toolchainView,nestedToolClosure,sourceFreeze:pin(freeze),actualReceipt:pin(file),historicalSnapshot:pin(path.join(owned.buildOutput,'historical-start.h')),artifacts,
 entry:Object.fromEntries(['executable','args','exitCode','signal','error','timedOut','primaryToolsBefore','primaryToolsAfter','startedAt','finishedAt'].map(k=>[k,job[k]])),parentLifecycleAcceptance:'INCOMPLETE'};
 fs.writeFileSync(path.join(owned.buildOutput,'registration-context.json'),JSON.stringify(context,null,2)+'\n',{flag:'wx'});
 return {job,file,context};
}
export function primaryGeneratedUnits(repo,c){
 const lf=s=>s.replace(/\r\n/g,'\n'),rawMain=text(path.join(repo,'native/main.cpp')),main=lf(rawMain),rawStop=text(path.join(repo,'native/virtual_controller_stop_runtime.h')),start=text(path.join(repo,'native/virtual_controller_start_runtime.h'));
 const parts=Object.fromEntries(Object.entries(c.functions).map(([name,file])=>[name,primaryFunction(lf(text(path.join(repo,file))),name)]));
 const declaration=main.match(/^static HANDLE g_inputHostPrimaryThread\s*=\s*nullptr\s*;/gm),raw=main.match(/^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;/gm);need(declaration?.length===1&&raw?.length===1,'PRIMARY_ACTUAL_GLOBALS');
 let common=text(path.join(repo,'tools/fixtures/input_host_clear_retirement/suite.cpp.in'));common=common.slice(0,common.indexOf('int main(){try{'));
 common=common.replace('static HANDLE g_inputHostPrimaryThread=nullptr;',declaration[0]);
 const logger='static void appendNativeLifecycleLog(const char* event,const json& data){ports.logs.push_back({{"event",event},{"data",data}});}';need(common.split(logger).length===2,'PRIMARY_LOGGER_LEAF');common=common.replace(logger,logger.replace('ports.logs.push_back','if(primaryLoggerThrows)throw std::runtime_error("injected-log-failure");ports.logs.push_back'));
 let body=text(path.join(repo,c.template));body='static bool primaryLoggerThrows=false;\n'+body.replace('static bool primaryLoggerThrows=false;\n','');
 const leaf=text(path.join(repo,'tools/input_pipe_retirement_memory_selftest.cpp')),prefix=leaf.slice(0,leaf.indexOf('using json=nlohmann::json;'));
 const created=adoption(lf(start),true),historical=adoption(lf(text(c.historicalStart.path)),false);
 const result={};for(const variant of c.variants){let helper=parts.inputHostPrimaryThreadRetirementProvenLocked.text,clear=parts.inputHostClearLocked.text,life=parts.inputHostReconcileQuarantineOnLifecycleThread.text,owned=parts.inputHostOwnedCleanupProvenLocked.text,bus=parts.inputHostReconcileQuarantine.text,adoptCode=created;
  if(variant==='historical-start-loses-owner')adoptCode=historical;
  if(variant==='drop-owner-on-failed-Close')helper=helper.replace('if (receipt.closed) g_inputHostPrimaryThread = nullptr;','g_inputHostPrimaryThread = nullptr;');
  if(variant==='omit-owned-primary-duty')owned=owned.replace('!g_inputHostPrimaryThread &&','true &&');
  if(variant==='omit-Clear-primary-duty')clear=clear.replaceAll('|| !primaryThreadRetirementProven','|| false');
  if(variant==='omit-lifecycle-primary-duty')life=life.replace('    if (!inputHostPrimaryThreadRetirementProvenLocked("quarantine")) return;','    // specified negative: bypass primary close');
  if(variant==='omit-BUS-primary-continuation')bus=bus.replace('    if (g_inputHostPrimaryThread) {\n        inputHostQueueLifecycleWork("quarantine-endpoint-absent");\n        return;\n    }','    // specified negative: skip local handle debt queue');
  const replacements={'@@PIPE_LEAVES@@':prefix,'@@PIPE_CLOSE@@':parts.inputHostPipeCloseOwned.text,'@@PIPE_WRAPPER@@':raw[0]+'\n'+parts.inputHostPipeRetirementProvenLocked.text,'@@PRIMARY_HELPER@@':helper+'\n'+parts.nativeLifecycleEventIsBase.text,'@@CLEAR@@':clear,'@@OWNED@@':owned,'@@DISABLED@@':parts.inputHostDisabledCleanupProvenLocked.text,'@@PIPE_ONLY_HELPER@@':parts.inputHostPipeOnlyReturnAdmittedLocked.text,'@@BUS_QUARANTINE@@':bus,'@@LIFECYCLE_QUARANTINE@@':life};
  let generated=common;for(const [tag,value] of Object.entries(replacements)){need(generated.split(tag).length===2,'PRIMARY_GENERATION_MARKER:'+tag);generated=generated.replace(tag,()=>value);}
  result[variant]=body.replace('@@COMMON@@',()=>generated).replace('@@ADOPT_FUNCTION@@',()=>'static void publishCreatedHandles(PROCESS_INFORMATION& pi){\n'+adoptCode+'\n}');need(!result[variant].includes('@@'),'PRIMARY_UNEXPANDED_UNIT');
 }
 const rawFunctionHashes=Object.fromEntries(Object.entries(c.functions).map(([name,file])=>[name,primaryFunction(text(path.join(repo,file)),name).sha256]));rawFunctionHashes.inputHostStart=primaryFunction(start,'inputHostStart').sha256;
 return {units:result,parts,rawFunctionHashes,created,historical,declaration:declaration[0],historicalRawStartFunctionSha256:primaryFunction(text(c.historicalStart.path),'inputHostStart').sha256};
}
export function validatePrimaryFunctionIdentity(freeze,generated,c){
 need(freeze.normalizationPolicy===c.normalizationPolicy&&same(freeze.functions,generated.parts)&&same(freeze.rawFunctionHashes,generated.rawFunctionHashes),'PRIMARY_RAW_AND_LF_COMPLETE_FUNCTIONS');
 const names=Object.keys(c.functions).filter(n=>c.functions[n]==='native/main.cpp');need(same(freeze.rawActualMainFunctionHashes,Object.fromEntries(names.map(n=>[n,generated.rawFunctionHashes[n]]))),'PRIMARY_RAW_MAIN_FUNCTION_PINS');
}
// The producer's existing subprocess decoder may replace localized linker words.
// Capture only complete absolute .lib paths ending in ':', then require every
// path and byte hash to belong to the frozen manifest below.
export function primaryLinkLibraries(log){return [...log.matchAll(/^\s+[^\r\n]*?((?:[A-Za-z]:[\\/]|\\\\)[^\r\n]*?\.lib)\s*:\s*$/gmi)].map(m=>m[1].trim());}
export function validatePrimaryArtifactSet(context,output){
 need(context.artifacts?.length===14,'PRIMARY_FOURTEEN_ARTIFACT_SETS');
 const seen=new Set();for(const row of context.artifacts){const key=row.profile+'-'+row.variant;need(!seen.has(key),'PRIMARY_DUPLICATE_ARTIFACT_ROUTE');seen.add(key);keys(row.files,Object.keys(roles),'PRIMARY_ARTIFACT_ROLES');for(const [role,name] of Object.entries(roles))need(norm(row.files[role].path)===norm(path.join(output,key,name))&&hash(row.files[role].path)===row.files[role].sha256,'PRIMARY_ARTIFACT_HASH:'+role);}
}
export function validatePrimaryMatrix(r,c){
 need(Array.isArray(r.runs)&&r.runs.length===14,'PRIMARY_FOURTEEN_BUILD_MATRIX');
 for(const profile of c.profiles)for(const variant of c.variants){const rows=r.runs.filter(row=>row.profile===profile&&row.variant===variant);need(rows.length===1,'PRIMARY_PROFILE_VARIANT');const row=rows[0],rt=row.runtime,positive=variant==='current';
  need(row.accepted===true&&row.exitCode===(positive?0:2)&&row.expectedFailure===(positive?null:c.negativeLabels[variant]),'PRIMARY_SPECIFIED_NEGATIVE_OR_EXIT');
  need(rt?.privateThreadsJoinedBeforeExit===5&&rt.primaryOwnerRemaining===false&&rt.pipeOracleLiveBeforeExit===0&&rt.handlesBefore===50&&rt.handlesAfter===50&&rt.realProductSdkDriverOrDeviceExecuted===false,'PRIMARY_PRIVATE_RESOURCES_BEFORE_EXIT');
  need(positive?(rt.pass===true&&rt.checks===31&&rt.negativeOnlyPrimarySalvage===0&&rt.failures?.length===0):(rt.pass===false&&rt.failures?.includes(c.negativeLabels[variant])),'PRIMARY_CURRENT_OR_SPECIFIED_LABEL');
  need(same(rt,c.expectedRuntime[variant]),'PRIMARY_PRESERVED_CHECKS_CASES_AND_FAILURES:'+variant);
 }
}
export function validatePrimaryThread(r,{repo,sourceHashes,tools,identity=schema().sensorEnsure.currentMainManifest,toolchainAuthorityRepo=repo}){
 const c=primaryContract(repo);validatePrimaryMatrix(r,c);
 need(r.status==='PASS_CURRENT_CONSUMERS_WITH_PRIVATE_KERNEL_OWNERS'&&r.pass===true&&r.consumerInputsStable===true&&r.wholeMainUnchanged===true&&r.sourceChanges?.length===0&&r.runtimeClosureStable===true,'PRIMARY_CURRENT_STABLE_STATUS');
 need(r.wholeGoalComplete===false&&r.hardwareAccepted===false&&r.completeStartOrSdkOrProductExecuted===false&&r.processExitUsedAsCleanup===false&&r.arbitraryOsBlockingBound==='UNKNOWN','PRIMARY_SCOPE');
 const output=r.out;inside(output,path.resolve(repo,'../../../Testing/YMCC/Evidence/VC-CaptureReceipt23/t'),'PRIMARY_SHORT_OWNED_OUTPUT');
 const context=read(path.join(output,'registration-context.json')),owned=context.ownedWorkspace;
 need(norm(context.sourceRepository)===norm(repo)&&norm(context.toolchainAuthorityRepository)===norm(toolchainAuthorityRepo),'PRIMARY_EXECUTED_REPOSITORIES');
 need(context.kind==='controller-signal-primary-thread'&&context.parentLifecycleAcceptance==='INCOMPLETE'&&same(context.sourceHashes,sourceHashes),'PRIMARY_CONTEXT_SOURCE_CLOSURE');
 need(owned.id==='primary-thread'&&norm(owned.buildOutput)===norm(output)&&owned.tempPathLength<=110&&owned.outputPathLength<=110&&norm(owned.temp)===norm(path.join(owned.directory,'t'))&&hash(owned.jobIdentity.path)===owned.jobIdentity.sha256&&hash(owned.owner.path)===owned.owner.sha256,'PRIMARY_OWNED_JOB');
 const owner=read(owned.owner.path),job=read(owned.jobIdentity.path);need(job.id===owned.id&&norm(job.buildOutput)===norm(output)&&norm(owner.repository)===norm(repo)&&same(owner.sourceHashes,sourceHashes),'PRIMARY_OWNER_SOURCE_BINDING');
 need(same(context.approvedIdentityManifest,identity)&&hash(identity.path)===identity.sha256&&r.approvedIdentityManifest?.sha256===identity.sha256&&norm(r.approvedIdentityManifest.path)===norm(identity.path),'PRIMARY_APPROVED_IDENTITY');
 const approval=read(identity.path);need(!approval.sourceRepository||norm(approval.sourceRepository)===norm(repo),'PRIMARY_APPROVED_SOURCE_REPOSITORY');need(norm(approval.toolchainAuthorityRepository??repo)===norm(toolchainAuthorityRepo),'PRIMARY_APPROVED_TOOLCHAIN_REPOSITORY');need(approval.mainSha256===sourceHashes['native/main.cpp']&&approval.sourceBindings?.['native/main.cpp']===approval.mainSha256&&r.mainSha256AtStart===approval.mainSha256&&r.mainSha256AtEnd===approval.mainSha256,'PRIMARY_APPROVED_MAIN');
 for(const [n,h] of Object.entries(approval.sourceBindings))need(sourceHashes[n]===h,'PRIMARY_APPROVED_SOURCE_PIN');
 need(r.identityMode==='approved-manifest-and-runtime-source-closure'&&same(r.runtime_sourceClosure,sourceHashes)&&same(read(context.runtimeSourceClosure.path),sourceHashes)&&hash(context.runtimeSourceClosure.path)===context.runtimeSourceClosure.sha256&&norm(context.runtimeSourceClosure.path)===norm(path.join(owned.directory,'runtime_sourceClosure.json'))&&r.runtimeSourceClosureIdentity?.sha256===context.runtimeSourceClosure.sha256&&norm(r.runtimeSourceClosureIdentity.path)===norm(context.runtimeSourceClosure.path),'PRIMARY_RUNTIME_CLOSURE');
 for(const [n,h] of Object.entries(sourceHashes))need(hash(path.join(repo,n))===h,'PRIMARY_LIVE_SOURCE_CHANGED:'+n);
 need(norm(context.sourceFreeze.path)===norm(path.join(output,'SOURCE-FREEZE.json'))&&hash(context.sourceFreeze.path)===context.sourceFreeze.sha256,'PRIMARY_FREEZE_PIN');const freeze=read(context.sourceFreeze.path);
 keys(freeze.sourceHashes,c.sourceInputs,'PRIMARY_SOURCE_THIRTEEN');for(const n of c.sourceInputs)need(freeze.sourceHashes[n]===sourceHashes[n]&&hash(path.join(output,'source',n))===sourceHashes[n],'PRIMARY_ACTUAL_SNAPSHOT:'+n);
 const generated=primaryGeneratedUnits(repo,c);validatePrimaryFunctionIdentity(freeze,generated,c);need(same(r.actualFunctionHashes,Object.fromEntries(Object.entries(generated.parts).map(([n,p])=>[n,p.sha256]))),'PRIMARY_ACTUAL_FUNCTION_HASHES');
 const rawMainNames=Object.keys(c.functions).filter(n=>c.functions[n]==='native/main.cpp');need(same(freeze.rawActualMainFunctionHashes,Object.fromEntries(rawMainNames.map(n=>[n,generated.rawFunctionHashes[n]]))),'PRIMARY_RAW_MAIN_FUNCTION_PINS');
 need(freeze.actualAdoption===generated.created&&freeze.historicalAdoption===generated.historical&&freeze.ownerDeclaration===generated.declaration&&freeze.historicalRawStartFunctionSha256===generated.historicalRawStartFunctionSha256,'PRIMARY_ACTUAL_AND_HISTORICAL_START_FRAGMENTS');
 for(const key of ['historicalStart','historicalManifest'])need(freeze[key]?.sha256===c[key].sha256&&norm(freeze[key].path)===norm(c[key].path),'PRIMARY_HISTORICAL_RECEIPT_PIN');need(hash(context.historicalSnapshot.path)===c.historicalStart.sha256&&context.historicalSnapshot.sha256===c.historicalStart.sha256&&norm(context.historicalSnapshot.path)===norm(path.join(output,'historical-start.h')),'PRIMARY_HISTORICAL_SNAPSHOT');
 const {contract,manifest,capture}=compositionContract(toolchainAuthorityRepo),view=context.toolchainView,derived=structuredClone(capture);derived.environment.TEMP=owned.temp;derived.environment.TMP=owned.temp;
 need(norm(view.path)===norm(path.join(owned.directory,'tc'))&&same(view.original,contract.toolchain)&&view.manifestSha256===contract.toolchain.manifestSha256&&hash(path.join(view.path,'toolchain-manifest.json'))===view.manifestSha256&&hash(path.join(view.path,'build-environment.json'))===view.captureSha256&&same(read(path.join(view.path,'build-environment.json')),derived),'PRIMARY_TOOLCHAIN_ONLY_TEMP_REBOUND');
 const tool=(p,expected,h)=>{const approved=tools.files.find(row=>norm(row.path)===norm(expected));need(approved&&norm(p)===norm(expected)&&hash(p)===approved.sha256&&(!h||h===approved.sha256),'PRIMARY_REVIEWED_TOOL:'+expected);};
 const entry=context.entry;need(entry.exitCode===0&&entry.signal===null&&entry.error===null&&entry.timedOut===false&&same(entry.args,primaryArgs(repo,owned,view,context.runtimeSourceClosure.path,identity)),'PRIMARY_ACTUAL_BOUND_INVOCATION');tool(entry.executable,tools.primary.python);
 for(const phase of ['primaryToolsBefore','primaryToolsAfter']){keys(Object.fromEntries(entry[phase].map(r=>[r.role,r])),Object.keys(tools.primary),'PRIMARY_ALL_PRIMARY_TOOLS');for(const row of entry[phase])tool(row.path,tools.primary[row.role],row.sha256);}need(same(entry.primaryToolsBefore,entry.primaryToolsAfter),'PRIMARY_TOOL_DRIFT');
 const tc=r.toolchain;for(const [key,file,h] of [['manifest','toolchain-manifest.json',view.manifestSha256],['capture','build-environment.json',view.captureSha256]])need(norm(tc[key].path)===norm(path.join(view.path,file))&&tc[key].sha256===h,'PRIMARY_REPORTED_TOOLCHAIN');tool(tc.python.path,tools.primary.python,tc.python.sha256);
 need(capture.environment.WindowsSDKVersion.replace(/[\\/]$/,'')===tools.sdkVersion,'PRIMARY_CAPTURED_SDK');need(tc.tools.length===3,'PRIMARY_NATIVE_TOOLS');for(const [i,name,role] of [[0,'cl.exe','compiler'],[1,'link.exe','linker'],[2,'dumpbin.exe','dumpbin']]){const row=tc.tools[i],approved=manifest.tools.find(r=>r.name===name);need(approved&&approved.sha256===row.sha256&&norm(row.path)===norm(capture.tools[name].paths[0]),'PRIMARY_CAPTURED_TOOL_ABI');tool(row.path,tools.native[role],row.sha256);}
 const authority=new Map(manifest.files.map(row=>[norm(row.frozen),row])),observed=new Map();validatePrimaryArtifactSet(context,output);
 for(const row of r.runs){const matched=context.artifacts.filter(a=>a.profile===row.profile&&a.variant===row.variant);need(matched.length===1,'PRIMARY_ARTIFACT_MATRIX');const files=matched[0].files,target=path.join(output,row.profile+'-'+row.variant);keys(files,Object.keys(roles),'PRIMARY_ARTIFACT_ROLES');
  for(const [role,name] of Object.entries(roles))need(norm(files[role].path)===norm(path.join(target,name))&&hash(files[role].path)===files[role].sha256,'PRIMARY_ARTIFACT_HASH:'+role);
  need(same(read(files.runtime.path),row.runtime)&&same(JSON.parse(text(files.runLog.path)),row.runtime),'PRIMARY_ACTUAL_RUNTIME_LOG');
  for(const key of ['fixture','executable'])need(norm(row[key].path)===norm(files[key].path)&&row[key].sha256===files[key].sha256&&fs.statSync(files[key].path).size===row[key].bytes,'PRIMARY_GENERATED_AND_EXECUTED_PIN');
  need(sha(generated.units[row.variant])===files.fixture.sha256,'PRIMARY_WHOLE_GENERATED_ACTUAL_UNIT');
  const flags=row.profile==='Release'?['/O2','/DNDEBUG']:['/Od','/RTC1'],src=path.join(output,'source');
  const command=[tc.tools[0].path,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',...flags,'/I'+path.join(src,'native'),'/I'+path.join(src,'deps/json'),'/I'+path.join(src,'tools'),'/sourceDependencies',files.dependencies.path,files.fixture.path,'/Fo'+path.join(target,'fixture.obj'),'/Fe'+files.executable.path,'/link','/VERBOSE:LIB'];need(same(read(files.command.path),command),'PRIMARY_EXACT_COMPILE_PROFILE');
  const data=read(files.dependencies.path).Data;need(norm(data.Source)===norm(files.fixture.path)&&data.Includes?.length>250,'PRIMARY_ACTUAL_INCLUDE_GRAPH');
  for(const file of data.Includes){const rel=path.relative(src,file).replaceAll('\\','/');if(c.sourceInputs.includes(rel))need(hash(file)===sourceHashes[rel],'PRIMARY_SNAPSHOT_INCLUDE');else{const approved=authority.get(norm(file));need(approved&&hash(file)===approved.sha256,'PRIMARY_COMPILER_INPUT_ESCAPE');observed.set(norm(file),approved.sha256);}}
  const libraries=primaryLinkLibraries(text(files.buildLog.path));need(libraries.length>0,'PRIMARY_ACTUAL_LINK_LIBRARIES');for(const file of libraries){const approved=authority.get(norm(file));need(approved&&hash(file)===approved.sha256,'PRIMARY_LINK_INPUT_ESCAPE');}
  const imports=text(files.imports.path).toLowerCase();need(imports.includes('kernel32.dll')&&!['hid.dll','setupapi.dll','dinput8.dll','xinput','steam_api','winmm.dll','user32.dll'].some(s=>imports.includes(s)),'PRIMARY_NO_PRODUCT_DEVICE_IMPORTS');need(fs.readFileSync(files.executable.path).subarray(0,2).toString()==='MZ','PRIMARY_ACTUAL_PE');
 }
 const reported=Object.entries(r.toolDependencyClosure);need(reported.length===observed.size&&reported.every(([p,h])=>observed.get(norm(p))===h),'PRIMARY_COMPLETE_OBSERVED_DEPENDENCIES');
 const nested=validateCompositionNestedClosure(context,{repo,owned,sourceHashes,manifest,contract});need(Date.parse(read(context.nestedToolClosure.before.path).finishedAt)<=Date.parse(entry.startedAt)&&Date.parse(read(context.nestedToolClosure.after.path).startedAt)>=Date.parse(entry.finishedAt),'PRIMARY_NESTED_EXECUTION_WINDOW');
 need(norm(context.actualReceipt.path)===norm(path.join(output,'RESULT.json'))&&hash(context.actualReceipt.path)===context.actualReceipt.sha256&&same(read(context.actualReceipt.path),r),'PRIMARY_ORIGINAL_RECEIPT_BYTES');
 return {id:'actual-primary-thread-ownership',pass:true,profiles:2,currentBuilds:2,currentChecks:62,specifiedNegativeBuilds:12,builds:14,sourceInputs:13,privateThreadJoins:70,handlesBefore:50,handlesAfter:50,currentSalvage:0,resourcesReturnedBeforeExit:true,nestedToolClosure:nested,parentLifecycleAcceptance:'INCOMPLETE',wholeLifecycleAcceptance:false};
}
