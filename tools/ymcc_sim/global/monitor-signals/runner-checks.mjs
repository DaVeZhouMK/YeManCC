import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {runBoundedNode,assessRun}from'./process.mjs';import{assertCurrent,sha256}from'./common.mjs';
// Tests the adapter's receipt protocol; these cases do not add product coverage.
export async function runRunnerChecks(out){
 fs.mkdirSync(out,{recursive:true});const checks=[];
 const pending=path.join(out,'timeout-fixture.mjs');fs.writeFileSync(pending,'setInterval(()=>{},1000);');
 const timeout=await runBoundedNode(pending,[],{cwd:out,timeoutMs:200});const rejected=assessRun({...timeout,receipt:{ok:true,status:'PASS'}});
 assert(timeout.timedOut);assert.equal(rejected.status,'FAIL');assert(rejected.reasons.includes('WORKER_TIMEOUT'));checks.push({id:'bounded-worker-timeout',status:'PASS',rejection:rejected,observed:timeout,expected:'FAIL',pass:true});
 const noReceipt=path.join(out,'missing-receipt-fixture.mjs');fs.writeFileSync(noReceipt,'console.log("completed without protocol receipt");');
 const finished=await runBoundedNode(noReceipt,[],{cwd:out,timeoutMs:5000});const absent=assessRun({...finished,receipt:null});
 assert.equal(finished.exitCode,0);assert.equal(absent.status,'FAIL');assert(absent.reasons.includes('MISSING_WORKER_RECEIPT'));checks.push({id:'zero-exit-missing-receipt',status:'PASS',rejection:absent,expected:'FAIL',pass:true});
 const failed=assessRun({exitCode:0,receipt:{ok:false,status:'FAIL'}});assert.equal(failed.status,'FAIL');checks.push({id:'nonpassing-structured-receipt',status:'PASS',rejection:failed,expected:'FAIL',pass:true});
 const fixture=path.join(out,'source-fixture.ts');fs.writeFileSync(fixture,'export const example=1;');const pin=sha256(fs.readFileSync(fixture));
 assert.doesNotThrow(()=>assertCurrent({'source-fixture.ts':pin},out));fs.appendFileSync(fixture,'\nexport const changed=2;');
 assert.throws(()=>assertCurrent({'source-fixture.ts':pin},out),/MONITOR_SOURCE_DRIFT/);
 const drift=assessRun({exitCode:0,receipt:{ok:true,status:'PASS'},expectedSourceHashes:{'source-fixture.ts':pin},currentSourceHashes:{'source-fixture.ts':sha256(fs.readFileSync(fixture))}});
 assert.equal(drift.status,'FAIL');checks.push({id:'source-drift-before-after',status:'PASS',rejection:drift,expected:'FAIL',pass:true});
 const overflow=assessRun({exitCode:0,outputTruncated:true,receipt:{ok:true,status:'PASS'}});assert.equal(overflow.status,'FAIL');checks.push({id:'truncated-output-cannot-pass',status:'PASS',rejection:overflow,expected:'FAIL',pass:true});
 const result={schema:'ymcc.monitor-runner-checks.v1',ok:true,status:'PASS',checks,scope:'runner negative controls only; no additional product claims',hardwareIo:false};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}
