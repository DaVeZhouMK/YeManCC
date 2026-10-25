import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs,verifyOriginalExecution} from './run.mjs';
import {createIoBoundary,executeOriginalBundle} from './vm-io.mjs';
import {getTdpConsumerAdapters,CASE_ID} from './adapter.mjs';
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
function fixture(t){
 const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'ymcc-tdp-contract-'));
 t.after(()=>{const resolved=fs.realpathSync(root);assert.equal(path.dirname(resolved),parent);assert(path.basename(resolved).startsWith('ymcc-tdp-contract-'));fs.rmSync(resolved,{recursive:true,force:true});});
 return root;
}
function execution(t){const root=fixture(t),file=path.join(root,'report.json');const raw={checks:spec.expectedNames.map(name=>({name,pass:true})),failures:0,scope:spec.originalScope,realHardwareExecuted:false};fs.writeFileSync(file,JSON.stringify(raw));return{exitCode:0,thrown:null,reportPath:file,report:raw,boundary:{denied:[],processAttempts:0,networkAttempts:0}};}
test('descriptor retains actual original source and exposes one reviewed software-only case',()=>{
 const [adapter]=getTdpConsumerAdapters();assert.equal(adapter.id,CASE_ID);assert.equal(adapter.scope,'behavioral-mock');
 assert(adapter.evidenceInputs.includes('tools/tdp_frontend_static_selftest.ts'));
 assert(adapter.evidenceInputs.includes('src/bridge/yeman.ts'));
 assert(adapter.evidenceInputs.includes('tools/mock-shell.ts'));
 assert(adapter.artifactInputs.length>=749&&adapter.artifactInputs.length<=3072);assert.deepEqual(adapter.resultFiles,['tdp-consumer/result.json','tdp-consumer/output-evidence.json']);
});
test('the entire original current report contract must pass',t=>{
 const original=execution(t);assert.equal(verifyOriginalExecution(original,spec).checks,20);
});
test('missing, duplicate and renamed original cases never become full acceptance',t=>{
 for(const mode of ['missing','duplicate','renamed']){
  const original=execution(t);
  if(mode==='missing')original.report.checks.pop();
  else if(mode==='duplicate')original.report.checks[1].name=original.report.checks[0].name;
  else original.report.checks[0].name='invented-success';
  assert.throws(()=>verifyOriginalExecution(original,spec),/TDP_ORIGINAL_(REPORT_SHAPE|CASE_MISSING_OR_DUPLICATE)/);
 }
});
test('contradictory counts, string booleans, physical claims and nonzero success exits are rejected',t=>{
 for(const mode of ['count','bool','physical','exit']){
  const original=execution(t);
  if(mode==='count')original.report.failures=1;
  else if(mode==='bool')original.report.checks[0].pass='true';
  else if(mode==='physical')original.report.realHardwareExecuted=true;
  else original.exitCode=1;
  assert.throws(()=>verifyOriginalExecution(original,spec));
 }
});
test('compile errors or timeouts cannot be counted as a detected original consumer fault',t=>{
 for(const thrown of ['build error','TDP_ORIGINAL_TIMEOUT']){
  const original=execution(t);original.thrown=thrown;original.exitCode=1;
  original.report.checks.find(row=>row.name===spec.fault.expectedFailedCase).pass=false;original.report.failures=1;
  assert.throws(()=>verifyOriginalExecution(original,spec,{negative:true}),/TDP_ORIGINAL_THREW_OR_TIMED_OUT/);
 }
});
test('fault acceptance requires the named unchanged original assertion and nonzero result',t=>{
 const original=execution(t);assert.throws(()=>verifyOriginalExecution(original,spec,{negative:true}),/TDP_FAULT_NOT_CAUGHT/);
 original.report.checks.find(row=>row.name===spec.fault.expectedFailedCase).pass=false;original.report.failures=1;original.exitCode=1;
 assert.deepEqual(verifyOriginalExecution(original,spec,{negative:true}).failedNames,[spec.fault.expectedFailedCase]);
 original.report.checks.find(row=>row.name===spec.fault.expectedFailedCase).pass=true;original.report.checks[0].pass=false;
 assert.throws(()=>verifyOriginalExecution(original,spec,{negative:true}),/TDP_FAULT_NOT_CAUGHT/);
});
test('hidden process, network and outside-file attempts invalidate an otherwise green report',t=>{
 for(const key of ['denied','processAttempts','networkAttempts']){
  const original=execution(t);if(key==='denied')original.boundary.denied.push('OUTSIDE_SCRATCH');else original.boundary[key]=1;
  assert.throws(()=>verifyOriginalExecution(original,spec),/TDP_IO_BOUNDARY_VIOLATION/);
 }
});
test('fixture filesystem performs only scratch round-trips',t=>{
 const root=fixture(t),scratch=path.join(root,'scratch');fs.mkdirSync(scratch);
 const {safeFs,observation}=createIoBoundary(scratch);const file=path.join(scratch,'data.txt');
 safeFs.writeFileSync(file,'memory-scenario');assert.equal(safeFs.readFileSync(file,'utf8'),'memory-scenario');
 assert.equal(observation.writes,1);assert.equal(observation.reads,1);
 const outside=path.join(root,'must-not-be-written.txt');assert.throws(()=>safeFs.writeFileSync(outside,'no'),/OUTSIDE_SCRATCH/);
 assert.equal(fs.existsSync(outside),false);assert.deepEqual(observation.denied,['OUTSIDE_SCRATCH']);
});
test('child-process and network modules cannot escape the reviewed terminal boundary',t=>{
 const root=fixture(t);const boundary=createIoBoundary(root);
 assert.throws(()=>boundary.requireReviewed('node:child_process').spawnSync('must-not-start.exe',[]),/PROCESS_FORBIDDEN/);
 assert.equal(boundary.observation.processAttempts,1);
 for(const name of ['node:net','node:tls','node:http','module'])assert.throws(()=>boundary.requireReviewed(name),/UNREVIEWED_REQUIRE/);
});
test('a generated script cannot silently finish without its original main promise',async t=>{
 const root=fixture(t),file=path.join(root,'no-main.cjs');fs.writeFileSync(file,'module.exports={pass:true};');
 const result=await executeOriginalBundle(file,path.join(root,'scratch'));
 assert.equal(result.thrown,'TDP_ORIGINAL_MAIN_NOT_AWAITABLE');assert.equal(result.report,null);
});
test('an async hang is bounded and cannot leave a success report',async t=>{
 const root=fixture(t),file=path.join(root,'hang.cjs');fs.writeFileSync(file,'new Promise(()=>{});');
 const result=await executeOriginalBundle(file,path.join(root,'scratch'),{timeoutMs:50});
 assert.equal(result.thrown,'TDP_ORIGINAL_TIMEOUT');assert.equal(result.report,null);
});
test('CLI refuses unknown and incomplete arguments, source writes and reused directories',t=>{
 const root=fixture(t);
 for(const args of [[],['--unknown',root],['--evidence-dir','relative'],['--evidence-dir',root],['--evidence-dir',root,'--extra','x']])assert.throws(()=>parseArgs(args));
 const fresh=path.join(root,'fresh');assert.equal(parseArgs(['--evidence-dir',fresh]).root,fresh);assert.equal(fs.existsSync(fresh),false);
});
