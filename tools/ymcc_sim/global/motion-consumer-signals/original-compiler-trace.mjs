import fs from 'node:fs';
import path from 'node:path';
import {Worker} from 'node:worker_threads';
import {ADAPTER_TIMEOUT_MS} from './contracts.mjs';
import {EXPECTED_MUTATIONS} from './contracts.mjs';
import {readJson,ownedFile} from './evidence.mjs';
import {TRACE_SCHEMA,expectedMotionTraceAuthority,verifyMotionTraceConfiguration} from '../../../lib/hc_motion_compiler_trace.mjs';
const need=(ok,code)=>{if(!ok)throw Error(code);};
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
/** Mandatory addition after the unchanged complete report/executable guards.
 * Old M26 lacks this evidence and is never upgraded by a historical PASS. */
function motionTracePlan({repo,rawRoot,report,sourceInputs,externalInputs,tools}){
 need(report.compilerTrace?.schema===TRACE_SCHEMA&&report.compilerTrace.requiredForEveryConfiguration===true&&report.compilerTrace.osModuleLoadingProven===false,'MOTION_ORIGINAL_TRACE_MISSING');
 need(Array.isArray(tools?.files)&&tools.files.length>=574,'MOTION_ORIGINAL_TRACE_COMPLETE_TOOL_ALLOWLIST');
 const plan=[];
 function verify(out,bound,label){
  need(bound.compilerTrace?.schema===TRACE_SCHEMA&&bound.compilerTrace.requiredForEveryConfiguration===true&&bound.compilerTrace.osModuleLoadingProven===false&&Array.isArray(bound.configurations)&&bound.configurations.length===2&&new Set(bound.configurations.map(c=>c.name)).size===2&&['Release','Checked'].every(name=>bound.configurations.some(c=>c.name===name)),'MOTION_ORIGINAL_TRACE_CONFIGURATIONS');
  const expectedAuthority=expectedMotionTraceAuthority({repo,out,report:bound,sourceInputs,externalInputs,toolInputs:tools.files});
  plan.push({repo,out,report:bound,expectedAuthority,label});
 }
 verify(rawRoot,report,'positive');
 need(Array.isArray(report.mutations)&&report.mutations.length===39&&new Set(report.mutations.map(m=>m.name)).size===39,'MOTION_ORIGINAL_TRACE_39_FAULTS');
 for(const name of EXPECTED_MUTATIONS){
  const mutation=report.mutations.find(m=>m.name===name);need(mutation,'MOTION_ORIGINAL_TRACE_FAULT_MISSING');
  const relative=path.join('mutants',name,'evidence'),out=path.join(rawRoot,relative),bound=readJson(ownedFile(rawRoot,path.join(relative,'source-bindings.json')));
  need(path.resolve(mutation.evidence)===path.resolve(out),'MOTION_ORIGINAL_TRACE_FAULT_ROOT');
  for(const config of bound.configurations??[]){const summary=mutation.configurations.find(row=>row.name===config.name);need(summary&&same(summary.compilerTrace,config.compilerTrace)&&same(summary.nativeProcess,config.nativeProcess),'MOTION_ORIGINAL_TRACE_FAULT_SUMMARY');}
  verify(out,bound,name);
 }
 need(plan.length===40&&plan.every(task=>task.report.configurations.length===2),'MOTION_ORIGINAL_TRACE_ALL80_REQUIRED');
 return plan;
}
function completeTrace(results,tools){
 need(results.length===80,'MOTION_ORIGINAL_TRACE_ALL80_REQUIRED');
 return {status:'PASS',schema:TRACE_SCHEMA,configurations:results,configurationCount:80,positiveConfigurations:2,originalFaultConfigurations:78,declaredToolMembers:tools.files.length,onlyActualCompilerRowsObserved:true,osModuleLoadingProven:false,scope:'MSVC sourceDependencies plus linker library search/reference output, original compiler/native processes and exact input hashes; no OS load graph.'};
}
// Kept as the serial oracle for exact comparison and callers requiring synchronous verification.
export function verifyMotionOriginalCompilerTrace(options){
 const results=[];for(const task of motionTracePlan(options))for(const config of task.report.configurations)results.push({id:task.label,reportFile:path.join(task.out,'source-bindings.json'),...verifyMotionTraceConfiguration({repo:task.repo,out:task.out,report:task.report,config,expectedAuthority:task.expectedAuthority})});
 return completeTrace(results,options.tools);
}
/** Run the same80 exact verifications, with no hash cache or omitted file checks. */
export async function verifyMotionOriginalCompilerTraceParallel(options,{timeoutMs}={}){
 need(Number.isSafeInteger(timeoutMs)&&timeoutMs>=1&&timeoutMs<=ADAPTER_TIMEOUT_MS,'MOTION_TRACE_PARALLEL_BUDGET_REQUIRED');
 const deadline=Date.now()+timeoutMs,plan=motionTracePlan(options),workers=[],completed=new Array(plan.length);let next=0,done=0,settled=false,timer;
 try{
  await new Promise((resolve,reject)=>{
   const fail=error=>{if(!settled){settled=true;reject(error);}};
   timer=setTimeout(()=>fail(Error('MOTION_TRACE_PARALLEL_BUDGET_EXHAUSTED')),Math.max(1,deadline-Date.now()));
   function dispatch(worker){if(next<plan.length){const index=next++;worker.expected=index;worker.postMessage({index,task:plan[index]});}}
   for(let i=0;i<4;i++){
    const worker=new Worker(new URL('./original-compiler-trace-worker.mjs',import.meta.url));workers.push(worker);
    worker.on('error',fail);worker.on('exit',code=>{if(!settled)fail(Error('MOTION_TRACE_WORKER_EXITED_BEFORE_COMPLETE:'+code));});
    worker.on('message',message=>{
     if(settled)return;
     try{
      need(message&&message.index===worker.expected&&!completed[message.index],'MOTION_TRACE_WORKER_INDEX_MISMATCH');
      if(message.error)throw Object.assign(Error(message.error.message),{code:message.error.code,evidence:message.error.evidence});
      const task=plan[message.index];need(Array.isArray(message.results)&&message.results.length===2,'MOTION_TRACE_WORKER_INCOMPLETE');
      for(let c=0;c<2;c++)need(message.results[c].id===task.label&&message.results[c].reportFile===path.join(task.out,'source-bindings.json')&&message.results[c].configuration===task.report.configurations[c].name&&same(message.results[c].trace,task.report.configurations[c].compilerTrace),'MOTION_TRACE_WORKER_RESULT_BINDING');
      completed[message.index]=message.results;done++;
      if(done===plan.length){settled=true;resolve();}else dispatch(worker);
     }catch(error){fail(error);}
    });
    dispatch(worker);
   }
  });
  return completeTrace(completed.flat(),options.tools);
 }finally{clearTimeout(timer);await Promise.allSettled(workers.map(worker=>worker.terminate()));}
}
