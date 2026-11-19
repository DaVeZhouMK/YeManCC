import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs,verifyOriginalExecution,frozenCompilerOptions} from './run.mjs';
import {createIoBoundary,executeOriginalBundle} from './vm-io.mjs';
import {getTdpConsumerAdapters,CASE_ID} from './adapter.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
function fixture(t){
 const workspace=simulationCorePath('Workspace'),parent=fs.realpathSync.native(workspace);
 assert(samePath(parent,workspace)&&!fs.lstatSync(workspace).isSymbolicLink());
 const root=fs.mkdtempSync(path.join(parent,'ymcc-tdp-contract-')),identity=fs.lstatSync(root,{bigint:true});
 t.after(()=>{
  const actual=fs.realpathSync.native(root),current=fs.lstatSync(root,{bigint:true});
  assert(samePath(actual,root)&&samePath(path.dirname(actual),parent));
  assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===identity.dev&&current.ino===identity.ino);
  fs.rmSync(actual,{recursive:true,force:true});
 });
 return root;
}
function execution(t){const root=fixture(t),file=path.join(root,'report.json');const raw={checks:spec.expectedNames.map(name=>({name,pass:true})),failures:0,scope:spec.originalScope,realHardwareExecuted:false};fs.writeFileSync(file,JSON.stringify(raw));return{exitCode:0,thrown:null,reportPath:file,report:raw,boundary:{denied:[],processAttempts:0,networkAttempts:0}};}
test('descriptor retains actual original source and exposes one reviewed software-only case',()=>{
 const [adapter]=getTdpConsumerAdapters();assert.equal(adapter.id,CASE_ID);assert.equal(adapter.scope,'behavioral-mock');
 assert(adapter.evidenceInputs.includes('tools/tdp_frontend_static_selftest.ts'));
 assert(adapter.evidenceInputs.includes('src/bridge/yeman.ts'));
 assert(adapter.evidenceInputs.includes('tools/mock-shell.ts'));
 assert.equal(adapter.evidenceInputs.filter(file=>file==='tsconfig.json').length,1);
 assert(!spec.buildInputs.includes('tsconfig.json'),'compiler configuration is not a bundled module');
 for(const file of ['src/bridge/steamInstallationFacts.ts','src/bridge/steamUi.ts']){
  assert.equal(adapter.evidenceInputs.filter(input=>input===file).length,1);
  assert.equal(spec.buildInputs.filter(input=>input===file).length,1);
 }
 assert.equal(adapter.artifactInputs.length,751);assert(adapter.artifactInputs.length<=3072);assert.equal(new Set(adapter.artifactInputs.map(file=>file.toLowerCase())).size,751);assert.deepEqual(adapter.resultFiles,['tdp-consumer/result.json','tdp-consumer/output-evidence.json']);
});
test('frontend compiler options use only the reviewed frozen root config',()=>{
 const bytes=Buffer.from('{"compilerOptions":{"baseUrl":".","paths":{"@/*":["src/*"]}},"references":[{"path":"./tsconfig.node.json"}]}');
 assert.deepEqual(frozenCompilerOptions({path:'tsconfig.json',bytes}),{tsconfigRaw:bytes.toString('utf8')});
 assert.throws(()=>frozenCompilerOptions(),/TDP_COMPILER_CONFIG_NOT_FROZEN/);
 assert.throws(()=>frozenCompilerOptions({path:'tsconfig.node.json',bytes}),/TDP_COMPILER_CONFIG_NOT_FROZEN/);
 assert.throws(()=>frozenCompilerOptions({path:'tsconfig.json',bytes:Buffer.from('{')}),/TDP_COMPILER_CONFIG_JSON/);
 assert.throws(()=>frozenCompilerOptions({path:'tsconfig.json',bytes:Buffer.from('{"compilerOptions":[],"references":[]}')}),/TDP_COMPILER_CONFIG_SHAPE/);
 assert.throws(()=>frozenCompilerOptions({path:'tsconfig.json',bytes:Buffer.from('{"compilerOptions":{},"extends":"other.json"}')}),/TDP_COMPILER_CONFIG_EXTENDS_UNBOUND/);
});
test('the entire original current report contract must pass',t=>{
 const original=execution(t);assert.equal(verifyOriginalExecution(original,spec).checks,107);
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
test('fault acceptance requires only the named unchanged original assertion and exit 1',t=>{
 const original=execution(t),negative={negative:true},rejection=/TDP_FAULT_NOT_CAUGHT/;
 const target=original.report.checks.find(row=>row.name===spec.fault.expectedFailedCase);
 const unrelated=original.report.checks.find(row=>row.name!==spec.fault.expectedFailedCase);
 const validate=(exitCode,targetPass,unrelatedPass)=>{
  original.exitCode=exitCode;target.pass=targetPass;unrelated.pass=unrelatedPass;
  original.report.failures=Number(!targetPass)+Number(!unrelatedPass);
  fs.writeFileSync(original.reportPath,JSON.stringify(original.report));
  return verifyOriginalExecution(original,spec,negative);
 };
 assert.deepEqual(validate(1,false,true).failedNames,[spec.fault.expectedFailedCase]);
 for(const params of [[0,false,true],[7,false,true],[-1,false,true],[1,false,false],[1,true,false],[1,true,true]]){
  assert.throws(()=>validate(...params),rejection,JSON.stringify(params));
 }
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
