// Pure source transformation. Does not compile, spawn, load assemblies or authorize target execution.
import {createHash} from 'node:crypto';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
export const hashPrivateSource=x=>createHash('sha256').update(x).digest('hex');
export const PRIVATE_TERMINAL_TOOL_INPUTS=Object.freeze(['tools/input_coordinator_private_terminals.mjs','tools/fixtures/input_coordinator_lifecycle/private_terminal_observer.h']);
export const PRIVATE_TERMINALS=Object.freeze(['inputHostProbeHidSource','customSteamLibraryWindow','msiCrashOldOwnerDead']);
export const PRIVATE_DECISION_HOOKS=Object.freeze(['inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked']);
const need=(x,m)=>{if(!x)throw Error('PRIVATE_TERMINAL_'+m);};
export function applyPrivateTerminalClosure(inputs,{expected}={}){
 const keys=['inert','msiRuntime','fixture','pipe','observerHeader'];
 for(const k of keys)need(typeof inputs?.[k]==='string'&&/^[a-f0-9]{64}$/.test(expected?.[k]??'')&&hashPrivateSource(inputs[k])===expected[k],'EXACT_INPUT_REQUIRED:'+k);
 const original={...inputs},out={...inputs},pairs=[];
 function change(key,before,after,label){need(out[key].split(before).length===2,'UNIQUE_ANCHOR:'+label);out[key]=out[key].replace(before,after);pairs.push({key,label,before,after,beforeSHA:hashPrivateSource(before),afterSHA:hashPrivateSource(after)});}
 function body(key,symbol,code){const old=extract(original[key],symbol).code;change(key,old,old.slice(0,old.indexOf('{')+1)+'\n    '+code+'\n}',symbol);}
 body('inert','inputHostProbeHidSource','ymccPrivateTerminal::recordHidRequest(); // controlled memory request only; never scans/opens/reads a device');
 body('inert','customSteamLibraryWindow','return nullptr; // explicit private desktop model: no child window');
 body('msiRuntime','msiCrashOldOwnerDead','return ymccPrivateTerminal::proveOldOwnerDead(record.ownerPid,record.ownerCreationTime);');
 change('inert','#include "msi_crash_recovery_runtime.h"','#include "msi_crash_recovery_runtime.private_fixture.h"','MSI include only');
 change('fixture','#include "json.hpp"','#include "json.hpp"\n#include "source/native/private_terminal_observer.h"','observer declarations before full TU');
 change('fixture','if(argc!=3&&argc!=4)return 2;','if(argc!=3&&argc!=4)return 2;ymccPrivateTerminal::observeSelf(GetCurrentProcessId(),GetCurrentProcess());','observe actual self without replacing wmain decisions');
 change('pipe','fullPipeChildObservers.push_back(observation);','ymccPrivateTerminal::observeChildCreated(observation,pi.dwProcessId);\n fullPipeChildObservers.push_back(observation);','observe exact successful CreateProcess/DuplicateHandle');
 change('pipe','DWORD exit=999;if(wait==WAIT_OBJECT_0)GetExitCodeProcess(observation,&exit);','DWORD exit=999;if(wait==WAIT_OBJECT_0)GetExitCodeProcess(observation,&exit);\n ymccPrivateTerminal::observeChildWait(observation,wait);','observe original owned-child wait before original close');
 const hooks=PRIVATE_DECISION_HOOKS.map(symbol=>{const before=extract(original.inert,symbol).code,after=extract(out.inert,symbol).code;need(before===after,'DECISION_HOOK_CHANGED:'+symbol);return {symbol,sha256:hashPrivateSource(before),unchanged:true};});
 function restore(candidate){const value=Object.fromEntries(keys.map(k=>[k,candidate[k]]));for(const p of [...pairs].reverse()){need(typeof value[p.key]==='string'&&value[p.key].split(p.after).length===2,'INVERSE_ANCHOR:'+p.label);value[p.key]=value[p.key].replace(p.after,p.before);}for(const k of keys)need(hashPrivateSource(value[k])===expected[k],'INVERSE_BYTES:'+k);return value;}
 const restored=restore(out);for(const k of keys)need(restored[k]===original[k],'RESTORATION:'+k);
 const evidence={schemaVersion:1,status:'SOURCE_ONLY_NOT_COMPILED_NOT_EXECUTED',kind:'PRIVATE_THREE_TERMINAL_LEAF_CLOSURE',symbols:[...PRIVATE_TERMINALS],inputHashes:{...expected},outputHashes:Object.fromEntries(keys.map(k=>[k,hashPrivateSource(out[k])])),ports:pairs.filter(p=>PRIVATE_TERMINALS.includes(p.label)).map(p=>({symbol:p.label,key:p.key,beforeSHA:p.beforeSHA,afterSHA:p.afterSHA,beforeCode:p.before,afterCode:p.after})),observationAnchors:pairs.filter(p=>!PRIVATE_TERMINALS.includes(p.label)),decisionHooks:hooks,ownerPolicy:'Only exact owned-child creation + actual signaled wait + same PID/birth + successful exit-code read proves death. Self/unknown/foreign/mismatch/overflow/failure remain unproved.',hidPolicy:'Bounded controlled request recording only; no device IO',desktopPolicy:'Explicit no-child-window model',original21Unchanged:true,originalAssertionsAndMutationsUnchanged:true,sourceProjectionOnly:true,sourceAuthorityAdmissionRequired:true,targetExecutionAllowed:false};
 return {...out,evidence,restore};
}
// Pure JS reference for the finite observation policy. Tests use synthetic kernel
// witnesses; this reference never represents a native execution or authorizes one.
export function createPrivateOwnerReference({capacity=256}={}){
 need(Number.isSafeInteger(capacity)&&capacity>0&&capacity<=256,'REFERENCE_CAPACITY');const rows=[],tokens=new WeakMap();let rejected=0;
 const valid=(pid,birth)=>Number.isInteger(pid)&&pid>0&&pid<=0xffffffff&&typeof birth==='string'&&/^\d+$/.test(birth);
 function self(pid,birth){if(!valid(pid,birth)||rows.length===capacity||rows.some(r=>r.pid===pid&&r.birth===birth)){rejected++;return;}rows.push({pid,birth,self:true,exited:false,unknown:false});}
 function created({pid,birth,handle,pidRead,birthRead,createdSuccessfully,duplicatedSuccessfully}={}){if(!valid(pid,birth)||!handle||typeof handle!=='object'||pid!==pidRead||birth!==birthRead||!createdSuccessfully||!duplicatedSuccessfully||rows.some(r=>r.self&&r.pid===pid)||rows.length===capacity||rows.some(r=>r.pid===pid&&r.birth===birth)){rejected++;return null;}const token=Object.freeze({kind:'JS_SYNTHETIC_OWNED_CREATION'}),row={pid,birth,self:false,exited:false,unknown:false,handle};rows.push(row);tokens.set(token,row);return token;}
 function wait(token,{handle,originalSignaled,recheckSignaled,pidRead,birthRead,exitCodeReadOk}={}){const row=tokens.get(token);if(!row||row.handle!==handle){rejected++;return;}row.exited=originalSignaled===true&&recheckSignaled===true&&pidRead===row.pid&&birthRead===row.birth&&exitCodeReadOk===true;row.unknown=!row.exited;tokens.delete(token);row.handle=null;}
 function dead(pid,birth){if(!valid(pid,birth))return false;const r=rows.find(r=>r.pid===pid&&r.birth===birth);return Boolean(r&&!r.self&&!r.unknown&&r.exited);}
 return {self,created,wait,dead,snapshot:()=>({owners:rows.length,rejected})};
}
// Exact small anchor patch for the existing producer; old recipe/authority/runtime refusal remain intact.
export function patchPrivateTerminalProducer(source,{expectedSourceSHA}={}){
 need(hashPrivateSource(source)===expectedSourceSHA&&/^[a-f0-9]{64}$/.test(expectedSourceSHA??''),'PRODUCER_BEFORE_SHA');let code=source;const anchors=[];
 const edit=(before,after,label)=>{need(code.split(before).length===2,'PRODUCER_ANCHOR:'+label);code=code.replace(before,after);anchors.push({label,before,after});};
 edit("import {requireB49CompileBatch}","import {applyPrivateTerminalClosure,PRIVATE_TERMINAL_TOOL_INPUTS} from './input_coordinator_private_terminals.mjs';\nimport {requireB49CompileBatch}",'import new pure source tool');
 edit('argv=process.argv.slice(2),batchAuthority','argv=process.argv.slice(2),batchAuthority,privateTerminalClosure=false','explicit programmatic opt-in only');
 edit('if(suite&&(','if(typeof privateTerminalClosure!=="boolean")throw Error("Explicit boolean privateTerminalClosure required");\nif(suite&&(','strict closure opt-in type');
 edit('for(const rel of ["tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-compile-batch.mjs"','for(const rel of [...PRIVATE_TERMINAL_TOOL_INPUTS,"tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-compile-batch.mjs"','bind source tool and observer header as actual tool inputs');
 const insertion=`let privateTerminalEvidence=null;
 if(privateTerminalClosure){
  if(!compileOnly)throw Error('PRIVATE_TERMINALS_REQUIRE_EXPLICIT_COMPILE_ONLY');
  const msiFile=path.join(snapshot,'native/msi_crash_recovery_runtime.h'),fixtureFile=path.join(out,'fixture.cpp'),pipeFile=path.join(out,'pipe_cases.inc');
  const input={inert,msiRuntime:fs.readFileSync(msiFile,'utf8'),fixture:fs.readFileSync(fixtureFile,'utf8'),pipe:fs.readFileSync(pipeFile,'utf8'),observerHeader:fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/private_terminal_observer.h'),'utf8')};
  const closed=applyPrivateTerminalClosure(input,{expected:Object.fromEntries(Object.entries(input).map(([k,v])=>[k,sha(v)]))});
  inert=closed.inert;fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);fs.writeFileSync(fixtureFile,closed.fixture);fs.writeFileSync(pipeFile,closed.pipe);
  fs.writeFileSync(path.join(snapshot,'native/msi_crash_recovery_runtime.private_fixture.h'),closed.msiRuntime,{flag:'wx'});fs.writeFileSync(path.join(snapshot,'native/private_terminal_observer.h'),closed.observerHeader,{flag:'wx'});
  mutatedHeaderFiles.push('source/native/msi_crash_recovery_runtime.private_fixture.h','source/native/private_terminal_observer.h');mutatedHeaderInputs.push('native/msi_crash_recovery_runtime.h');privateTerminalEvidence=closed.evidence;
 }
 const compiledGenerated=`;
 edit('const compiledGenerated=',insertion,'after original mutations/wrappers, before original compiler ledger');
 edit('sourceClosureMeaning:closure.meaning','privateTerminalClosure:privateTerminalEvidence,sourceClosureMeaning:closure.meaning','bind source evidence to actual include graph');
 edit('managedProof,changed,fixtureExecuted:false','privateTerminalClosure:privateTerminalEvidence,managedProof,changed,fixtureExecuted:false','bind evidence to actual compile-only receipt');
 function restore(value){for(const a of [...anchors].reverse()){need(value.split(a.after).length===2,'PRODUCER_INVERSE');value=value.replace(a.after,a.before);}need(hashPrivateSource(value)===expectedSourceSHA,'PRODUCER_INVERSE_BYTES');return value;}
 need(restore(code)===source,'PRODUCER_PATCH_REVERSIBLE');return {code,anchors,beforeSHA:expectedSourceSHA,afterSHA:hashPrivateSource(code),restore,defaultOptIn:false,originalCompileCommandsUnchanged:true,runtimeStillRefused:true};
}
