import {parentPort} from 'node:worker_threads';
import path from 'node:path';
import {verifyMotionTraceConfiguration} from '../../../lib/hc_motion_compiler_trace.mjs';
if(!parentPort)throw Error('MOTION_TRACE_OWNED_WORKER_REQUIRED');
parentPort.on('message',({index,task})=>{
 try{
  if(!Number.isSafeInteger(index)||index<0||index>=40||!task||!Array.isArray(task.report?.configurations)||task.report.configurations.length!==2)throw Error('MOTION_TRACE_WORKER_TASK_INVALID');
  const results=task.report.configurations.map(config=>({id:task.label,reportFile:path.join(task.out,'source-bindings.json'),...verifyMotionTraceConfiguration({repo:task.repo,out:task.out,report:task.report,config,expectedAuthority:task.expectedAuthority})}));
  parentPort.postMessage({index,results});
 }catch(error){parentPort.postMessage({index,error:{message:error.message,code:error.code??null,evidence:error.evidence??null}});}
});
