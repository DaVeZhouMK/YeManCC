import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import {bindPsExitArmEvidence} from './ps_exit_receipt_projection.mjs';

const workspace=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Workspace');
const makePower=()=>({bound:true,terminal:true,currentSlots:0,retiredSlots:0,currentBorrowers:0,retiredBorrowers:0,pendingLocalReturns:0,
 importInFlight:false,commandBorrower:false,closeRefreshBorrower:false,originalHasBorrowers:false,originalLocalReturnPending:false});

function prepare(t,{negative=false,editNative=()=>{},editManaged=()=>{},writeManaged=true}={}){
 const base=fs.realpathSync(workspace),out=fs.mkdtempSync(path.join(base,'ps-exit-receipt-'));
 const rootIdentity=fs.lstatSync(out,{bigint:true});
 const mode=negative?'full-pipe-core-negative':'full-pipe-core',dir=path.join(out,mode);
 fs.mkdirSync(dir);
 t.after(()=>{
  assert.equal(fs.realpathSync(out),out);assert.equal(path.dirname(out),base);
  const now=fs.lstatSync(out,{bigint:true});
  assert(now.isDirectory()&&now.dev===rootIdentity.dev&&now.ino===rootIdentity.ino&&now.birthtimeNs===rootIdentity.birthtimeNs);
  assert.equal(fs.realpathSync(dir),dir);
  for(const name of ['fixture-result.json','full-pipe-child-1.json']){
   const file=path.join(dir,name);
   if(fs.existsSync(file))fs.unlinkSync(file);
  }
  fs.rmdirSync(dir);fs.rmdirSync(out);
 });
 const host={status:'OBSERVED',failure:null,processId:112,finalCleanupOk:true,hasController:false,hasContext:false,powerOwnershipAfterFinalCleanup:makePower()};
 const assertion='late ownership reply must not rebuild';
 const result={status:negative?'FAIL':'PASS',checks:1,assertionTrace:[{assertion,passed:!negative}],failures:negative?[{assertion}]:[],
  cases:[{managedReceipt:{...structuredClone(host),nativeObservedProcessExit:true,nativeObservedExitCode:0,nativeObserverCloseSucceeded:true,nativeObserverCloseError:0}}]};
 editNative(result);editManaged(host);
 const write=(name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value),{flag:'wx'});
 write('fixture-result.json',result);
 if(writeManaged)write('full-pipe-child-1.json',host);
 return {out,arm:{mode,result}};
}

for(const negative of [false,true])test(`PS exit binds ${negative?'negative FAIL':'positive PASS'} without rewriting it`,t=>{
 const {out,arm}=prepare(t,{negative}),evidence=bindPsExitArmEvidence(out,arm);
 assert.equal(evidence.rawStatus,negative?'FAIL':'PASS');assert.equal(evidence.actualAssertions,1);
 for(const file of [evidence.native,evidence.managed]){assert.match(file.sha256,/^[0-9a-f]{64}$/);assert.equal(fs.statSync(file.path).size,file.bytes);}
});

for(const [title,options,error] of [
 ['missing managed receipt',{writeManaged:false},/ENOENT/],
 ['other Host receipt',{editManaged:host=>host.processId++},/MANAGED_RAW_NOT_BOUND/],
 ['missing observer close',{editNative:r=>delete r.cases[0].managedReceipt.nativeObserverCloseSucceeded},/NATIVE_CHILD_NOT_RETURNED/],
 ['unreturned managed resource',{editNative:r=>r.cases[0].managedReceipt.hasContext=true,editManaged:host=>host.hasContext=true},/MANAGED_CHILD_NOT_RETURNED/],
 ['outstanding power borrower',{editNative:r=>r.cases[0].managedReceipt.powerOwnershipAfterFinalCleanup.currentBorrowers=1,editManaged:host=>host.powerOwnershipAfterFinalCleanup.currentBorrowers=1},/POWER_STILL_OWNED/],
 ['incomplete assertion trace',{editNative:r=>r.assertionTrace.length=0},/ASSERTION_TRACE_INCOMPLETE/],
 ['wrong negative trace',{negative:true,editNative:r=>r.assertionTrace[0].assertion='another assertion'},/ASSERTION_TRACE_FAILURE_MISMATCH/],
])test(`PS exit rejects ${title}`,t=>{const {out,arm}=prepare(t,options);assert.throws(()=>bindPsExitArmEvidence(out,arm),error);});

test('PS exit rejects a Native aggregate that differs from its original',t=>{
 const {out,arm}=prepare(t);arm.result.status='FAIL';
 assert.throws(()=>bindPsExitArmEvidence(out,arm),/NATIVE_RAW_CHANGED/);
});
