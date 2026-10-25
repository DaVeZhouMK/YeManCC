// Bounded adapter for the existing native residency consumer. No duplicated model.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {toolchainSpec} from '../sleep-non-user-wake-consumer/adapter.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const sources=['native/main.cpp','native/sleep_residency_evidence.h','native/sleep_abnormal_resleep.h','native/sleep_abnormal_resleep_native.inc','native/sleep_retry_budget.h','native/sleep_request_fallback.h','deps/json/json.hpp','tools/sleep_residency_evidence_selftest.cpp','tools/sleep_residency_evidence_selftest.py','tools/fixtures/sleep_residency/abnormal_native_harness.inc','tools/fixtures/sleep_residency/cases.json','tools/fixtures/sleep_residency/README.md','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/sleep.mjs','tools/ymcc_sim/global/sleep-fixtures/residency-entry.mjs','tools/ymcc_sim/global/sleep-non-user-wake-consumer/adapter.mjs','tools/ymcc_sim/global/sleep-non-user-wake-consumer/toolchain-inputs.json','tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json'];
export function getResidencyAdapter(root,{identityOnly=false}={}){const spec=toolchainSpec(root);return {id:'sleep.residency-evidence-consumer',domain:'sleep',title:'Native DRIPS evidence, staged recovery and real ROG delayed-entry replay with compiled faults',kind:'node',path:'tools/ymcc_sim/global/sleep-fixtures/residency-entry.mjs',args:['--output','{outDir}/residency'],profiles:['sleep','desktop','full'],scope:'behavioral-mock',timeoutMs:90000,resultFiles:['residency/result.json'],evidenceInputs:sources,artifactInputs:spec.files.map(row=>row.path),boundary:'Actual production accounting capture and abnormal runtime, mocked power/event/timer APIs; 30 synthetic accounting inputs plus one archived AMD ROG record; no physical DRIPS or hardware acceptance. Existing compiler tool declaration reused; seven bounded executions, no parent duplication.'};}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const at=process.argv.indexOf('--output');if(at<0||!process.argv[at+1])throw Error('EXPLICIT_OUTPUT_REQUIRED');
 const out=path.resolve(process.argv[at+1]);if(out===repo||out.startsWith(repo+path.sep)||fs.existsSync(out))throw Error('FRESH_EXTERNAL_EVIDENCE_REQUIRED');
 fs.mkdirSync(out,{recursive:true});const begin=Date.now(),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),tool=toolchainSpec(repo);
 const inputs=sources.map(p=>({path:p,sha256:hash(path.join(repo,p))}));const rows=[];let failure=null;
 try{
  for(const row of tool.files)if(hash(row.path)!==row.sha256)throw Error('TOOL_IDENTITY_CHANGED:'+row.path);
  for(const mutation of ['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted']){
   const remaining=85000-(Date.now()-begin);if(remaining<1000)throw Error('CASE_BUDGET_EXHAUSTED');
   const dest=path.join(out,mutation),run=spawnSync(tool.pythonExecutable,['tools/sleep_residency_evidence_selftest.py','--output-dir',dest,'--mutation',mutation],{cwd:repo,env:{...process.env,COMSPEC:tool.cmdExecutable,PATH:path.dirname(tool.nodeExecutable)+';'+process.env.PATH},timeout:remaining,maxBuffer:2*1024*1024});
   fs.writeFileSync(path.join(out,mutation+'.stdout.log'),run.stdout??'');fs.writeFileSync(path.join(out,mutation+'.stderr.log'),run.stderr??'');
   if(run.error||run.signal||run.status!==0)throw Error('NATIVE_CONSUMER_FAILED:'+mutation+':'+(run.error?.message??run.status));
   const receiptPath=path.join(dest,'receipt.json'),receipt=JSON.parse(fs.readFileSync(receiptPath,'utf8'));
   if(receipt.status!=='passed'||receipt.realSleepTriggered!==false||(mutation!=='none'&&!receipt.expectedFaultCaught))throw Error('INVALID_CHILD_RECEIPT:'+mutation);
   rows.push({mutation,receipt:receiptPath,sha256:hash(receiptPath),expectedFaultCaught:receipt.expectedFaultCaught});
  }
  for(const row of inputs)if(hash(path.join(repo,row.path))!==row.sha256)throw Error('SOURCE_CHANGED_DURING_RUN:'+row.path);
  for(const row of tool.files)if(hash(row.path)!==row.sha256)throw Error('TOOL_CHANGED_DURING_RUN:'+row.path);
 }catch(error){failure=String(error.message??error);}
 const passed=!failure,result={schemaVersion:1,id:'sleep.residency-evidence-consumer',status:passed?'PASS':'FAIL',passed,executedThisRun:true,elapsedMs:Date.now()-begin,children:rows,sourceInputs:inputs,failure,realSleepTriggered:false,physicalAcceptance:false,scope:'Production native consumer with mocked Windows boundaries; live Diagnostic availability/timing remains unverified.'};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,children:rows.length,elapsedMs:result.elapsedMs,failure}));if(!passed)process.exitCode=1;
}
