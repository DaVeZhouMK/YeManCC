import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawn} from 'node:child_process';
import {runMatrixChild,runMatrixChecker} from '../../../hc_motion_matrix_binding_selftest.mjs';
import {finalizeMatrixSupplement} from './matrix-runner.mjs';
import {validateMatrixReport,verifyMatrixChildReceipt} from './matrix-evidence.mjs';
import {REPO_ROOT,motionConsumerInputs} from './inputs.mjs';
const base=fs.realpathSync(os.tmpdir()),prefix='ymcc-matrix-failure-contract-';
function fixture(t){const root=fs.mkdtempSync(path.join(base,prefix));t.after(()=>{const resolved=fs.realpathSync(root);assert(path.relative(fs.realpathSync(base),resolved).startsWith(prefix));fs.rmSync(resolved,{recursive:true,force:true});});return root;}
test('actual owned child timeout leaves its actual terminal PID/signal/streams and is never a positive report',async t=>{
 const root=fixture(t),result=await runMatrixChild({out:root,label:'Timeout',exe:process.execPath,argv:['-e','process.stdout.write("started");setInterval(()=>{},1000)'],timeoutMs:120});
 assert.equal(result.timedOut,true);assert.notEqual(result.status,0);assert(result.pid>0);const recorded=JSON.parse(fs.readFileSync(result.processFile));assert.equal(recorded.state,'TERMINAL');assert.equal(recorded.pid,result.pid);assert.equal(recorded.timedOut,true);assert.equal(recorded.timeoutMs,120);assert(fs.existsSync(root+'/Timeout.stdout.log'));assert.equal(validateMatrixReport({status:'RUNNING'},{}).status,'FAIL');
});
test('actual nonzero child preserves original code and partial output instead of becoming semantic detection',async t=>{
 const root=fixture(t),r=await runMatrixChild({out:root,label:'BadExit',exe:process.execPath,argv:['-e','process.stderr.write("original error");process.exit(7)']});assert.equal(r.exitCode,7);assert.equal(r.timedOut,false);assert.equal(fs.readFileSync(root+'/BadExit.stderr.log','utf8'),'original error');assert.equal(JSON.parse(fs.readFileSync(r.processFile)).exitCode,7);
});
test('running child receipt exists before natural completion and is rejected as a completed native process',async t=>{
 const root=fixture(t),promise=runMatrixChild({out:root,label:'Running',exe:process.execPath,argv:['-e','setTimeout(()=>{},500)']});
 const running=JSON.parse(fs.readFileSync(root+'/Running.process.json'));assert.equal(running.state,'RUNNING');assert(running.pid>0);assert.equal(running.exitCode,null);
 assert.throws(()=>verifyMatrixChildReceipt({out:root,generated:root,config:{profile:'Release',executionExit:0},role:'native',checkerPid:process.pid}),/MATRIX_ACTUAL_PROCESS_RECEIPT_REQUIRED/);await promise;
});
test('unchanged checker failure writes a real partial FAIL result instead of losing result.json',async t=>{
 const root=fixture(t),out=path.join(root,'checker');const result=await runMatrixChecker(['--repo',path.join(root,'unavailable-repo'),'--source',path.join(root,'no-main.cpp'),'--out',out]);assert.equal(result.status,'FAIL');const disk=JSON.parse(fs.readFileSync(out+'/result.json'));assert.equal(disk.status,'FAIL');assert(disk.failure);assert.deepEqual(disk.configurations,[]);
});
test('secondary source finalization failure preserves the original checker timeout and writes aggregate result',async t=>{
 const root=fixture(t),out=path.join(root,'q');fs.mkdirSync(out);const beforeFile=path.join(out,'main.before.cpp');fs.writeFileSync(beforeFile,'source');
 const timeout=await runMatrixChild({out:root,label:'OwnedTimeout',exe:process.execPath,argv:['-e','setInterval(()=>{},1000)'],timeoutMs:120});
 const primary={code:'MATRIX_CHECKER_TIMEOUT',evidence:{processFile:timeout.processFile,process:{pid:timeout.pid,timedOut:timeout.timedOut,signal:timeout.signal,exitCode:timeout.exitCode}}};
 const result=finalizeMatrixSupplement({aggregate:{schemaVersion:1,status:'FAIL',failure:primary},out,source:path.join(root,'missing-source.cpp'),sourceBytes:Buffer.from('source'),beforeFile,start:Date.now()});
 assert.deepEqual(result.failure,primary);assert(result.secondaryFailures.some(e=>e.code==='MATRIX_SOURCE_FINALIZATION_FAILED'));assert.deepEqual(JSON.parse(fs.readFileSync(out+'/failure.json')).failure,primary);assert.equal(JSON.parse(fs.readFileSync(out+'/result.json')).status,'FAIL');assert.equal(result.executionClosureVerified,false);
});
async function lockOwnedFile(root,file){
 const pwsh=process.env.YMCC_SIM_TEST_PWSH??'pwsh.exe',ready=path.join(root,'lock.ready');
 const script='$s=[System.IO.File]::Open($env:YMCC_MATRIX_LOCK_FILE,[System.IO.FileMode]::Open,[System.IO.FileAccess]::ReadWrite,[System.IO.FileShare]::None); try { [System.IO.File]::WriteAllText($env:YMCC_MATRIX_LOCK_READY,"ready"); [Console]::In.ReadLine() | Out-Null } finally { $s.Dispose() }';
 const child=spawn(pwsh,['-NoProfile','-NonInteractive','-Command',script],{windowsHide:true,stdio:['pipe','ignore','pipe'],env:{...process.env,YMCC_MATRIX_LOCK_FILE:file,YMCC_MATRIX_LOCK_READY:ready}});let stderr='';child.stderr.on('data',chunk=>{stderr+=chunk;});
 const closed=new Promise(resolve=>child.once('close',resolve)),deadline=Date.now()+10000;while(!fs.existsSync(ready)&&Date.now()<deadline){if(child.exitCode!==null)throw Error('owned file lock failed '+stderr);await new Promise(resolve=>setTimeout(resolve,20));}
 if(!fs.existsSync(ready)){child.stdin.end();child.kill();await closed;throw Error('owned file lock not ready');}
 return async()=>{child.stdin.end();await closed;};
}
for(const hadPrimary of [true,false])test('actual locked PDB cannot erase '+(hadPrimary?'original timeout':'failed closure')+' or remove q/result',async t=>{
 const root=fixture(t),out=path.join(root,'q');fs.mkdirSync(out);const source=path.join(root,'main.cpp'),bytes=Buffer.from('source');fs.writeFileSync(source,bytes);const beforeFile=path.join(out,'main.before.cpp');fs.writeFileSync(beforeFile,bytes);const pdb=path.join(out,'owned-probe.pdb');fs.writeFileSync(pdb,'owned PDB bytes');
 const timeout=hadPrimary?await runMatrixChild({out:root,label:'OwnedTimeout',exe:process.execPath,argv:['-e','setInterval(()=>{},1000)'],timeoutMs:120}):null;
 const release=await lockOwnedFile(root,pdb);try{
  const primary=timeout?{code:'MATRIX_CHECKER_TIMEOUT',evidence:{processFile:timeout.processFile,process:{pid:timeout.pid,timedOut:timeout.timedOut,signal:timeout.signal,exitCode:timeout.exitCode}}}:null;
  const result=finalizeMatrixSupplement({aggregate:{schemaVersion:1,status:hadPrimary?'FAIL':'PASS',...(hadPrimary?{failure:primary}:{})},out,source,sourceBytes:bytes,beforeFile,start:Date.now()});
  assert.equal(result.status,'FAIL');assert.equal(result.closureCollectionComplete,false);assert.equal(result.executionClosureVerified,false);assert(result.secondaryFailures.some(e=>e.path===pdb&&e.phase==='hash'));assert.equal(result.failure.code,hadPrimary?'MATRIX_CHECKER_TIMEOUT':'MATRIX_EVIDENCE_CLOSURE_INCOMPLETE');assert.equal(JSON.parse(fs.readFileSync(out+'/result.json')).failure.code,result.failure.code);assert.equal(JSON.parse(fs.readFileSync(out+'/failure.json')).failure.code,result.failure.code);
 }finally{await release();}
});
test('source closure already declares checker/probe and keeps original/owner gates and budgets',()=>{
 const inputs=motionConsumerInputs();for(const rel of ['tools/hc_motion_matrix_binding_selftest.mjs','tools/fixtures/hc_model_motion_consumer/matrix_binding_probe.cpp','tools/ymcc_sim/global/motion-consumer-signals/matrix-runner.mjs','tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.mjs'])assert(inputs.sources.includes(rel),rel);
 const source=fs.readFileSync(path.join(REPO_ROOT,'tools/ymcc_sim/global/motion-consumer-signals/run.mjs'),'utf8');assert(source.includes('verifyMotionOriginalCompilerTrace'));assert(source.includes('await runOwnerSupplement'));assert(source.includes("failure:matrix.failure"));
});
