import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {readVirtualControllerSource,replaceUniqueSourceText} from '../../virtual_controller_runtime_source.mjs';
import {extract as originalExtract} from '../legion_motion_consumer/extract_main.mjs';
import {isolateCurrentFixtureSettingsMutex} from '../input_coordinator_lifecycle/settings_mutex_isolation.mjs';

const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const here=path.dirname(fileURLToPath(import.meta.url));
export const coldSelections=Object.freeze(['original-cold-environment','typed-preio','lock-route','baseline-repro','failed-duty-successor','pending-zero-absence','supplement','durable-write-marker','durable-read-marker','durable-write-unhide','durable-read-unhide','durable-write-helper','durable-read-helper']);
export function parseColdArguments(args){
 assert(Array.isArray(args),'named cold fixture arguments required');const o={};
 for(let i=0;i<args.length;i++){
  const k=args[i];assert(['--out','--toolchain','--prepare-only','--case','--configuration','--shared-record'].includes(k)&&!Object.hasOwn(o,k),'unknown or duplicate cold fixture option');
  if(k==='--prepare-only')o[k]=true;else{assert(typeof args[i+1]==='string'&&args[i+1]&&!args[i+1].startsWith('--'),'cold fixture option value required');o[k]=args[++i];}
 }
 assert(o['--out']&&path.isAbsolute(o['--out'])&&!/[\0\r\n]/.test(o['--out']),'absolute fresh cold output required');
 if(o['--toolchain']!==undefined)assert(path.isAbsolute(o['--toolchain'])&&!/[\0\r\n]/.test(o['--toolchain']),'absolute captured toolchain required');
 assert(o['--prepare-only']||o['--toolchain'],'actual compile/run requires captured toolchain');
 const selection=o['--case']??'original-cold-environment',configuration=o['--configuration']??'Both';
 assert(coldSelections.includes(selection),'unknown exact cold scenario');assert(['Both','Release','Checked'].includes(configuration),'unknown cold configuration');
 if(o['--shared-record']!==undefined)assert(path.isAbsolute(o['--shared-record'])&&!/[\0\r\n]/.test(o['--shared-record'])&&selection.startsWith('durable-'),'explicit owned durable record required');
 if(selection.startsWith('durable-')&&!o['--prepare-only'])assert(o['--shared-record']&&configuration!=='Both','durable execution needs one configuration and explicit shared record');
 return {out:path.resolve(o['--out']),toolchain:o['--toolchain']===undefined?undefined:path.resolve(o['--toolchain']),prepareOnly:o['--prepare-only']===true,selection,explicitSelection:o['--case']!==undefined,configuration,sharedRecord:o['--shared-record']===undefined?undefined:path.resolve(o['--shared-record'])};
}
function plain(file){
 assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'absolute plain cold path required');
 for(let p=path.resolve(file);;p=path.dirname(p)){if(fs.existsSync(p)){assert(!fs.lstatSync(p).isSymbolicLink(),'cold reparse path rejected');assert.equal(fs.realpathSync(p).toLowerCase(),p.toLowerCase(),'cold path redirection rejected');}if(path.dirname(p)===p)break;}
}
function lexicalVariables(body,parameters,repo){
 const require=createRequire(path.resolve(repo,'../../../Testing/YMCC/Workspace/NodeTools/package.json'));
 const parser=require('@babel/parser'),t=require('@babel/types');
 const ast=parser.parse('function preparation('+parameters.join(',')+'){\n'+body+'\n}',{sourceType:'script'});
 const scopes=new WeakMap(),parents=new WeakMap(),root={bindings:new Set(),parent:null,function:true};
 const binds=(pattern,s)=>{for(const name of Object.keys(t.getBindingIdentifiers(pattern)))s.bindings.add(name);};
 function visit(n,p,s){if(!n||typeof n.type!=='string')return;parents.set(n,p);const before=s;
  if(t.isFunction(n)){if(t.isFunctionDeclaration(n)&&n.id)binds(n.id,s);s={bindings:new Set(),parent:s,function:true};if(n.id)binds(n.id,s);for(const x of n.params)binds(x,s);}
  else if(t.isBlockStatement(n)||t.isCatchClause(n)||t.isForStatement(n)||t.isForInStatement(n)||t.isForOfStatement(n)||t.isSwitchStatement(n))s={bindings:new Set(),parent:s,function:false};
  scopes.set(n,s);if(t.isVariableDeclaration(n)){let target=s;if(n.kind==='var')while(target.parent&&!target.function)target=target.parent;for(const d of n.declarations)binds(d.id,target);}
  if(t.isClassDeclaration(n)&&n.id)binds(n.id,before);if(t.isCatchClause(n)&&n.param)binds(n.param,s);
  for(const k of t.VISITOR_KEYS[n.type]??[]){const child=n[k];if(Array.isArray(child))for(const c of child)visit(c,n,s);else visit(child,n,s);}
 }
 visit(ast,null,root);const found=new Map();
 function refs(n){if(!n||typeof n.type!=='string')return;if(t.isIdentifier(n)&&t.isReferenced(n,parents.get(n))){let s=scopes.get(n),bound=false;while(s){if(s.bindings.has(n.name)){bound=true;break;}s=s.parent;}if(!bound){const rows=found.get(n.name)??[];rows.push({line:n.loc.start.line-1,column:n.loc.start.column});found.set(n.name,rows);}}
  for(const k of t.VISITOR_KEYS[n.type]??[]){const child=n[k];if(Array.isArray(child))for(const c of child)refs(c);else refs(child,n);}
 }
 refs(ast);return{free:[...found].sort(([a],[b])=>a.localeCompare(b)).map(([name,references])=>({name,references})),parserInputs:Object.keys(require.cache).filter(p=>p.includes(path.sep+'@babel'+path.sep)).map(p=>{const b=fs.readFileSync(p);return{path:p,bytes:b.length,sha256:sha(b)}})};
}
const once=(text,before,after)=>{assert.equal(text.split(before).length,2,'unique cold preparation span required: '+before.slice(0,80));return text.replace(before,()=>after);};

// One source preparation implementation is shared by prepare-only and real build.
// No compiler, child process, device or installed-app call is made here.
export function prepareColdSources({repo,out,selection='original-cold-environment',configurations=['Release','Checked']}){
 assert(coldSelections.includes(selection),'unknown exact cold scenario');repo=path.resolve(repo);out=path.resolve(out);plain(repo);plain(out);
 assert(Array.isArray(configurations)&&configurations.length>0&&new Set(configurations).size===configurations.length&&configurations.every(x=>['Release','Checked'].includes(x)),'exact cold configurations required');
 const relative=path.relative(repo,out);assert(relative&&(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))&&!fs.existsSync(out),'fresh output outside repository required');
 fs.mkdirSync(out,{recursive:true});const snapshot=path.join(out,'source'),records=[];
 const save=(name,v)=>fs.writeFileSync(path.join(out,name),JSON.stringify(v,null,2)+'\n',{flag:'wx'});
 function copy(file,relative){plain(file);const st=fs.lstatSync(file);assert(!st.isSymbolicLink(),'cold source reparse rejected');if(st.isDirectory()){for(const item of fs.readdirSync(file))copy(path.join(file,item),path.join(relative,item));return;}assert(st.isFile(),'regular cold source required');const dest=path.join(snapshot,relative),bytes=fs.readFileSync(file);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});records.push({relative:relative.replaceAll('\\','/'),original:file,snapshot:dest,sha256:sha(bytes),bytes:bytes.length});}
 const copied=['native','capabilities','deps/adlx-sdk','deps/json','deps/webview2/build/native/include','deps/webview2/build/native/x64/WebView2LoaderStatic.lib','tools/fixtures/input_coordinator_lifecycle','tools/fixtures/input_coordinator_cold_return','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/virtual_controller_runtime_source.mjs','tools/virtual_controller_runtime_source.test.mjs','tools/input_coordinator_lifecycle_selftest.mjs','tools/input_coordinator_cold_return_selftest.mjs','tools/msi_late_hide_pending_regression.h','tools/msi_late_hide_publication_regression.h','tools/msi_late_hide_inflight_regression.h'];
 for(const relative of copied)copy(path.join(repo,relative),relative);
 const expanded=readVirtualControllerSource(path.join(snapshot,'native/main.cpp')),source=expanded.source,key=p=>path.resolve(p).toLowerCase();
 const primary=expanded.inputs.map(i=>{const row=records.find(r=>key(r.snapshot)===key(i.file));assert(row&&row.sha256===i.sha256);return row;});save('EXPANSION.json',expanded.manifest);
 const driver=fs.readFileSync(path.join(snapshot,'tools/input_coordinator_lifecycle_selftest.mjs'),'utf8'),begin=driver.indexOf('let inert=source;const substitutions=[];'),end=driver.indexOf("fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert)");assert(begin>=0&&end>begin,'original cold preparation slice required');
 const extractBegin=driver.indexOf('function extract(text,symbol'),extractEnd=driver.indexOf('\nimport {readVirtualControllerSource');assert(extractBegin>=0&&extractEnd>extractBegin,'actual parse-view extractor required');
 const extract=new Function('originalExtract','crypto',driver.slice(extractBegin,extractEnd)+';return extract;')(originalExtract,crypto);
 const slice=driver.slice(begin,end),parameters=['source','mutation','out','fs','path','sha','extract','replaceUniqueSourceText','snapshot'];
 const bindings={applicationMatrixRows:null,Error,expanded,gpdLiveSdTransferSelected:false,gpdPersonaMutexSelected:false,gpdR4CandidateBinding:null,JSON,Object,oemAfterOffSelected:false,records:records.map(r=>({...r,path:r.relative})),sdOffSourceLostSelected:false,selectedCase:null,Set,sourceReconfirmSelected:false,sourceWakeSelected:false,String,suite:null,wakeKeptSourceSelected:false};
 const lexical=lexicalVariables(slice,parameters,repo);assert.deepEqual(lexical.free.map(x=>x.name),Object.keys(bindings).sort((a,b)=>a.localeCompare(b)),'actual preparation lexical contract drift');
 save('FREE-VARIABLES.json',{driverSha256:sha(driver),sliceSha256:sha(slice),free:lexical.free,parserInputs:lexical.parserInputs,bindings:Object.fromEntries(Object.entries(bindings).map(([n,v])=>[n,n==='records'?'actual snapshot records':n==='expanded'?'actual include expansion':typeof v==='function'?'unchanged intrinsic':v]))});
 const exclusiveFs={...fs,writeFileSync:(p,b,o)=>fs.writeFileSync(p,b,typeof o==='string'?{encoding:o,flag:'wx'}:{...(o??{}),flag:'wx'})};
 const prepare=new Function(...Object.keys(bindings),...parameters,slice+';return {inert,core,structCore,compilerHeaderBindings,substitutions,fragments};');
 const prepared=prepare(...Object.values(bindings),source,null,out,exclusiveFs,path,sha,extract,replaceUniqueSourceText,snapshot);let inert=prepared.inert;const localLeaves=[];
 const steam=extract(inert,'steamOriginalRecoveryEngineHandle').code,steamStub=steam.slice(0,steam.indexOf('{')+1)+'\n throw std::logic_error("Steam recovery outside cold-only scope");\n}';inert=once(inert,steam,steamStub);localLeaves.push({symbol:'steamOriginalRecoveryEngineHandle',beforeSha256:sha(steam),afterSha256:sha(steamStub),scope:'Existing unsupported Steam endpoint remains throw-only outside cold scope'});
 for(const [symbol,body]of [['inputHostTargetReturnMachineBinding','return coldBinding();'],['hidHideCliWriterRecordPath','return coldWriterPath();'],['hidHideCliWriterMachineBinding','return coldBinding();'],['inputHostTargetReturnColdAbsence','return coldAbsence(knownIds);'],['inputHostTargetReturnColdHidden','return coldHidden(hidden);'],['inputHostTargetReturnProbePhysicalWriter','return coldProbe(owner);'],['inputHostTargetReturnVerifyPhysicalPath','return coldProof(root,target);'],['hidHideRun','return coldCli(arguments,output,exitCodeOut);']]){const old=extract(inert,symbol).code,next=old.slice(0,old.indexOf('{')+1)+'\n'+body+'\n}';inert=once(inert,old,next);localLeaves.push({symbol,beforeSha256:sha(old),afterSha256:sha(next),scope:'Existing cold OS/helper input endpoint only'});}
 const ownerFile=path.join(snapshot,'native/msi_native_foreground_isolation_runtime.h'),ownerBefore=fs.readFileSync(ownerFile,'utf8');let owner=ownerBefore;const observations=[];
 // Preserve the actual product owner and its contention return values.
 for(const [name,tag,anchor]of [['msiNativeForegroundHandleWork','work','if(msiPostHideMaintainOwner("post-hide-maintenance"))return;'],['msiNativeForegroundMaintenance','maintenance','if(msiPostHideMaintainOwner("post-hide-period-ended"))return;']]){const old=extract(owner,name).code,next=once(old,anchor,anchor+'\n coldRepairObserveGeneric("'+tag+'");');owner=once(owner,old,next);observations.push({symbol:name,beforeSha256:sha(old),afterSha256:sha(next),scope:'Controlled generic-entry observation only'});}
 const poll=extract(owner,'msiNativeForegroundPollCycleLocked').code,mapped=once(poll,'s.helperExitCode=result==0?0:6;','s.helperExitCode=result==0?0:coldRepairHelperExit();s.helperExitQueryAttempted=true;s.helperExitQuerySucceeded=true;s.helperExitQueryError=0;');owner=once(owner,poll,mapped);
 const ownerFixture=path.join(snapshot,'native/msi_native_foreground_isolation_runtime.fixture.h');fs.writeFileSync(ownerFixture,owner,{flag:'wx'});inert=once(inert,'#include "msi_native_foreground_isolation_runtime.h"','#include "msi_native_foreground_isolation_runtime.fixture.h"');
 const coldHeader=path.join(snapshot,'native/virtual_target_return_cold_runtime.h'),coldBytes=fs.readFileSync(coldHeader,'utf8'),coldStruct=extract(coldBytes,'ColdEnvironment','struct').code;
 if(source.includes('struct ColdEnvironment'))assert.equal(extract(inert,'ColdEnvironment','struct').code,coldStruct);
 for(const name of ['inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup'])assert.equal(extract(inert,name).code,extract(source,name).code,'actual cold algorithm body changed');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert,{flag:'wx'});
 const baseText=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),runtimeRoots=configurations.map(name=>path.join(out,name,'private-files')),runtimeRoot=runtimeRoots[0];
 const isolation=isolateCurrentFixtureSettingsMutex(baseText,{runtimeRoot,moduleMaintenance:true});let base=isolation.text;
 if(runtimeRoots.length>1){const original='root!=normalizeWindowsPathForCompare(L'+JSON.stringify(runtimeRoot)+')',expanded=runtimeRoots.map(root=>'root!=normalizeWindowsPathForCompare(L'+JSON.stringify(root)+')').join(' && ');base=once(base,original,expanded);}
 fs.writeFileSync(path.join(out,'base.fixture.cpp'),base,{flag:'wx'});save('SETTINGS-BOUNDARY.json',{...isolation,runtimeRoots,generatedSha256:sha(base),exactConfigurationRootsOnly:true,compilerRun:false,businessRun:false});
 for(const name of fs.readdirSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle')).filter(n=>n.endsWith('.inc')))fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle',name),path.join(out,name),fs.constants.COPYFILE_EXCL);
 for(const name of ['msi_late_hide_pending_regression.h','msi_late_hide_publication_regression.h','msi_late_hide_inflight_regression.h'])fs.copyFileSync(path.join(snapshot,'tools',name),path.join(out,name),fs.constants.COPYFILE_EXCL);
 fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_cold_return/fixture.cpp'),path.join(out,'fixture.cpp'),fs.constants.COPYFILE_EXCL);
 fs.writeFileSync(path.join(out,'cold-repair-config.h'),'#pragma once\n#define COLD_REPAIR_EXPECT_EXCLUSIVE 1\n#define COLD_REPAIR_DEFAULT_CASE L"'+selection+'"\n',{flag:'wx'});
 const id=f=>{const b=fs.readFileSync(f);return{path:f,bytes:b.length,sha256:sha(b)}};
 const generated=[path.join(snapshot,'native/main.inert.cpp'),ownerFixture,...fs.readdirSync(out).filter(n=>/\.(inc|cpp|h)$/.test(n)).map(n=>path.join(out,n))].map(id);
 const toolInputs=[fileURLToPath(import.meta.url),path.join(repo,'tools/input_coordinator_cold_return_selftest.mjs'),path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'),path.join(repo,'tools/virtual_controller_runtime_source.mjs'),path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'),path.join(repo,'tools/lib/hc_controller_motion_fixture.mjs'),path.join(repo,'tools/fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs'),path.join(repo,'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-runtime-layout.mjs')].map(id);
 const plan={schemaVersion:1,status:'PREPARED_NOT_COMPILED',sourceToolPolicy:'CURRENT_CANONICAL_SOURCE_BOUND_PREPARATION_V2',selection,configurations,runtimeRoots,defaultOriginalGroupUnchanged:true,compilerRun:false,businessRun:false,fullTUPrepared:true,sourceRepo:repo,output:out,sourceInputs:primary,toolInputs,parserInputs:lexical.parserInputs,generated,records,localLeaves,ownerObservations:observations,ownerAdapter:{original:ownerFile,originalSha256:sha(ownerBefore),generated:ownerFixture,generatedSha256:sha(owner),lockPolicyChanged:false,terminalPort:'Existing test-only helper exit/query observations; no real helper Job or SDK'},coldEnvironment:{path:coldHeader,sha256:sha(coldBytes),bodySha256:sha(coldStruct),bodyUnchanged:true},compilerHeaderBindings:prepared.compilerHeaderBindings,fixtureArguments:[runtimeRoot,'ABS_FRESH_RESULT_JSON',...(selection==='original-cold-environment'?[]:[selection])],scope:'Original cold algorithms with controlled OS/helper input endpoints only; no Start/Host/helper Job/device/Steam claim',wholeProductMainExecuted:false,fullToolClosure:false,currentPrimaryAccepted:false};
 save('PREPARE.json',plan);save('NEXT-COMPILE-INPUTS.json',{entry:path.join(out,'fixture.cpp'),supportSource:path.join(snapshot,'native/dsu_server.cpp'),includeDirectories:[out,path.join(snapshot,'native'),path.join(snapshot,'deps/json'),path.join(snapshot,'deps/webview2/build/native/include')],staticLibrary:path.join(snapshot,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib'),fixtureArguments:plan.fixtureArguments,compilerRun:false,businessRun:false});
 return{snapshot,records,primary,prepared,localLeaves,generated,plan,runtimeRoot,toolInputs:[...toolInputs,...lexical.parserInputs],coldStruct};
}
