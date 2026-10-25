import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {fileURLToPath} from 'node:url';
import {assessOldWriteResult,executeOldWriteStage} from './old-write-stage.mjs';import {deriveManagedAllowance} from './first-old-stage.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),pin=p=>({path:p,sha256:hash(p)});
export function runOldWriteStageTests({planPath,preloadPath,businessPath,launchPath,processPath,evidenceDir}){
  assert(path.isAbsolute(evidenceDir)&&!fs.existsSync(evidenceDir));const invocation=read(planPath),policy=read(invocation.policy.path),allowance=deriveManagedAllowance({policy,preload:read(preloadPath),invocation,frameworkMetadata:read(invocation.frameworkMetadata.path)});
  const d={business:read(businessPath),launch:read(launchPath),processReceipt:read(processPath),allowance,invocation},tests=[];
  const test=(id,fn)=>{fn();tests.push({id,passed:true});},refuse=(id,mutate)=>test(id,()=>{const next=structuredClone(d);mutate(next);assert.equal(assessOldWriteResult(next).passed,false);});
  test('actual-original6-35-9-receipt-accepted',()=>assert.equal(assessOldWriteResult(d).passed,true));
  refuse('one-original-check-missing',x=>x.business.checks.pop());refuse('one-original-check-failed',x=>x.business.checks[0].passed=false);
  refuse('original-case-order-changed',x=>x.business.cases.reverse());refuse('original-golden-conversion-changed',x=>x.business.goldenWrites[4].raw=47);
  refuse('actual-native-attempt-refused',x=>x.business.nativeAttempts=1);refuse('forced-exit-not-natural',x=>x.processReceipt.naturalExit=false);
  refuse('original45s-budget-not-extended',x=>x.processReceipt.durationMs=45001);refuse('missing-launch-receipt-not-success',x=>x.launch=null);
  refuse('unknown-loaded-managed-image',x=>x.launch.actualLoadedManaged.push({path:'D:/unapproved.dll',identity:'Unknown, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null',sha256:'a'.repeat(64),dynamic:false}));
  refuse('raw-HC-runtime-load-refused',x=>x.launch.actualLoadedManaged.push({...x.invocation.rawHc,identity:'HandheldCompanion, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null',dynamic:false}));
  refuse('raw-HC-code-executed-refused',x=>x.business.hc.codeExecuted=true);refuse('different-fixed-host-refused',x=>x.business.host.sha256='a'.repeat(64));
  test('mode-never-inferred',()=>assert.throws(()=>executeOldWriteStage({mode:undefined}),e=>e.code==='OLD_WRITE_EXPLICIT_STAGE_MODE_REQUIRED'));
  fs.mkdirSync(evidenceDir);const receipt={schemaVersion:1,status:'PASS_OLD_WRITE_STAGE_RECEIPT_REFUSALS',tests,passed:tests.length,failed:0,producerExecuted:false,inputs:[planPath,preloadPath,businessPath,launchPath,processPath].map(pin)};fs.writeFileSync(path.join(evidenceDir,'SUMMARY.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});return receipt;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){const a=process.argv.slice(2);assert.equal(a.length,12);const o=Object.fromEntries(Array.from({length:6},(_,i)=>a.slice(2*i,2*i+2)));const r=runOldWriteStageTests({planPath:o['--plan'],preloadPath:o['--preload'],businessPath:o['--business'],launchPath:o['--launch'],processPath:o['--process'],evidenceDir:o['--evidence-dir']});console.log(JSON.stringify({status:r.status,passed:r.passed,producerExecuted:false}));}
