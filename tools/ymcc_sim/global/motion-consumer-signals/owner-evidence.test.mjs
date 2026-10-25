import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {runProcess} from '../runner.mjs';
import {REPO_ROOT,motionConsumerInputs} from './inputs.mjs';
import {getMotionConsumerAdapters} from './adapter.mjs';
import {ADAPTER_TIMEOUT_MS,PRODUCER_TIMEOUT_MS,EXPECTED_MUTATIONS} from './contracts.mjs';
import {MATRIX_FAULTS,MATRIX_BUDGET_MS,sha} from './matrix-evidence.mjs';
import {OWNER_CHECKER,OWNER_CONTRACT,OWNER_PROBE,OWNER_TRACE,OWNER_FAULTS,OWNER_BUDGET_MS,OWNER_CHILD_TIMEOUT_MS,ownerCheckerArguments,createOwnerFaults,assertOwnerProcess,validateOwnerReport,validateOwnerStage,verifyOwnerEvidence} from './owner-evidence.mjs';
import {runOwnerSupplement,captureOwnerClosure,finalizeOwnerEvidence} from './owner-runner.mjs';
// These are deliberately fabricated report schemas. They grant no native/global
// acceptance; actual offline checker executions are separate scratch evidence.
const oracle={identities:Array.from({length:63},()=>({})),legacy:{cases:Array.from({length:215},()=>({}))}};
function schema(fault){
 const costs={reviewedFindCalls:1,reviewedFindComparisons:2,defaultFindCalls:1,defaultFindComparisons:2,legacyContainsCalls:1,legacyContainsComparisons:2,totalCatalogPrimaryReads:0};
 const result=fault?{status:'FAIL',error:fault.expected+(fault.id==='rollback-class-search'?':'+JSON.stringify(costs):''),scenario:fault.id==='wrong-primary-priority'?'':'schema-not-execution',checks:1,windows:0}:{schemaVersion:1,status:'PASS',checks:2404129,boundPairs:9975,windows:159600,admitted:3642,rejected:155958,oldProcessCalls:3642,newProcessCalls:3642,supersededLegacyAttempts:56,oldCost:costs,newCost:{...Object.fromEntries(Object.keys(costs).map(n=>[n,0])),totalCatalogPrimaryReads:1},fullBusExecuted:false,realDevicesAccessed:false,wholeProcessCpuReduction:null};
 return {schemaVersion:1,status:'PASS',expectedFailure:fault?.expected??null,inputs:[{file:'schema-only',sha256:'a'.repeat(64)}],configurations:['Release','Checked'].map(mode=>({mode,compileExit:0,exitCode:fault?1:0,signal:null,accepted:true,report:structuredClone(result),dependencies:[{file:'schema-only',sha256:'b'.repeat(64)}]})),observations:['hc_model_motion_catalog.snapshot.h','hc_default_motion_catalog.h','hc_legacy_motion_catalog.snapshot.h'].map(file=>({file,algorithmPreserved:true,originalSha256:'a'.repeat(64),instrumentedSha256:'b'.repeat(64)})),drift:[],originalLoopAlgorithmsPreserved:true,productionBodiesRewritten:false,baselineHelperSymbolRenamedOnlyToAvoidADL:true,fullBusExecuted:false,hardwareIo:false,wholeProcessCpuReduction:null};
}
const validate=(r,fault)=>validateOwnerReport(r,{fault,oracle});
test('owner schema samples preserve both configurations and original oracle grid without claiming execution',()=>{
 for(const fault of [undefined,...OWNER_FAULTS]){const v=validate(schema(fault),fault);assert.equal(v.status,'PASS',v.issues.join(','));assert.equal(v.structuralValidationOnly,true);}
});
for(const [name,edit]of [
 ['missing report',()=>null],['missing Checked',r=>{r.configurations.pop();}],['duplicate Release',r=>{r.configurations[1].mode='Release';}],['compile failure',r=>{r.configurations[0].compileExit=2;}],['native bad exit',r=>{r.configurations[0].exitCode=2;}],['native signal',r=>{r.configurations[0].signal='SIGTERM';}],['accepted flag alone',r=>{r.configurations[0].report.status='FAIL';}],['assertion grid reduced',r=>{r.configurations[0].report.checks--;}],['state grid reduced',r=>{r.configurations[0].report.boundPairs--;}],['zero effects',r=>{r.configurations[0].report.oldProcessCalls=0;}],['mismatched outputs',r=>{r.configurations[0].report.newProcessCalls++;}],['repeated class lookup',r=>{r.configurations[0].report.newCost.reviewedFindCalls=1;}],['cost measurements removed',r=>{delete r.configurations[0].report.oldCost;}],['superseded legacy check removed',r=>{r.configurations[0].report.supersededLegacyAttempts=0;}],['native includes omitted',r=>{r.configurations[0].dependencies=[];}],['drift concealed',r=>{r.drift=[{file:'changed'}];}],['physical scope promoted',r=>{r.hardwareIo=true;}],['CPU reduction invented',r=>{r.wholeProcessCpuReduction=0.2;}],['lookup algorithm changed',r=>{r.observations[0].algorithmPreserved=false;}],['instrumentation unchanged',r=>{r.observations[0].instrumentedSha256=r.observations[0].originalSha256;}],
])test('owner positive schema rejects '+name,()=>{let r=schema();const changed=edit(r);if(changed!==undefined)r=changed;assert.equal(validate(r).status,'FAIL');});
for(const fault of OWNER_FAULTS)test('both actual semantic exits and exact assertion remain mandatory for '+fault.id,()=>{
 for(const edit of [r=>{r.configurations.pop();},r=>{r.configurations[0].exitCode=0;},r=>{r.configurations[1].compileExit=1;},r=>{r.configurations[0].report.status='PASS';},r=>{r.configurations[0].report.error='other failure';},r=>{r.configurations[0].report.error=fault.expected+'-unrelated';}]){const r=schema(fault);edit(r);assert.equal(validate(r,fault).status,'FAIL');}
});
test('malformed owner schemas cannot become acceptance or throw past the structural boundary',()=>{for(const r of [null,{},'PASS',{configurations:[null],observations:[null]}])assert.doesNotThrow(()=>assert.equal(validate(r).status,'FAIL'));});
const stage=()=>({status:'PASS',positive:{status:'PASS',configurations:schema().configurations},faults:OWNER_FAULTS.map(f=>({...f,status:'PASS',configurations:schema(f).configurations})),semanticSourceFaultsDetected:4,logicalLookupRegressionDetected:1});
test('aggregate keeps all five original owner faults and ten native negative configurations',()=>{assert.deepEqual(validateOwnerStage(stage()),{status:'PASS',positiveConfigurations:2,faultGroups:5,faultConfigurations:10});for(const edit of [s=>s.faults.pop(),s=>{s.faults[4]=s.faults[0];},s=>s.faults[0].configurations.pop(),s=>{s.faults[0].configurations[0].exitCode=0;},s=>{s.positive=null;},s=>{s.status='FAIL';},s=>{s.logicalLookupRegressionDetected=0;}]){const s=stage();edit(s);assert.throws(()=>validateOwnerStage(s));}});
test('fixed owner invocation really starts unchanged checker with preload and dual profiles',()=>{
 for(const fault of [undefined,...OWNER_FAULTS]){const args=ownerCheckerArguments({repo:REPO_ROOT,source:'D:/owned/main.cpp',generated:'D:/owned/g',out:'D:/owned/e',fault});assert.equal(args[0],'--import');assert.equal(fileURLToPath(args[1]),path.join(REPO_ROOT,OWNER_TRACE));assert.equal(args[2],path.join(REPO_ROOT,OWNER_CHECKER));assert.equal(args[args.indexOf('--profiles')+1],'Release,Checked');assert.equal(args.includes('--expect-failure'),!!fault);if(fault)assert.equal(args.at(-1),fault.expected);}
 assert.throws(()=>ownerCheckerArguments({repo:REPO_ROOT,source:'x',out:'y'}));assert.throws(()=>ownerCheckerArguments({repo:REPO_ROOT,source:'x',out:'y',generated:'g',fault:{id:'new experiment'}}));
});
test('all fault controls change one part, preserve other bytes and restore their exact preimage',()=>{
 const contract=JSON.parse(fs.readFileSync(path.join(REPO_ROOT,OWNER_CONTRACT))),main=fs.readFileSync(path.join(REPO_ROOT,'native/main.cpp'),'utf8');
 const headerBytes=fs.readFileSync(path.join(REPO_ROOT,'capabilities/HcMotionCapabilityCatalog.h')),catalogBytes=fs.readFileSync(path.join(REPO_ROOT,'capabilities/HcMotionCapabilityCatalog.generated.h'));
 for(const text of [main.replaceAll('\r\n','\n'),main.replaceAll('\r\n','\n').replaceAll('\n','\r\n')]){
  const original={main:Buffer.from('// preserve UTF8 归属\n'+text),header:headerBytes,catalog:catalogBytes},controls=createOwnerFaults({sourceBytes:original.main,headerBytes,catalogBytes,contract});
  assert.deepEqual(controls.map(c=>c.id),OWNER_FAULTS.map(c=>c.id));for(const c of controls){assert.equal(c.proof.reverseSha256,sha(original[c.part]));assert.notEqual(c.proof.partAfterSha256,c.proof.partBeforeSha256);for(const part of Object.keys(original))assert.equal(c.bytes[part].equals(original[part]),part!==c.part);}
 }
 assert.throws(()=>createOwnerFaults({sourceBytes:Buffer.from(main),headerBytes:Buffer.from('missing required primary guard'),catalogBytes,contract}),/OWNER_FAULT_HUNK_NOT_UNIQUE/);
});
test('real checker children still reject exit, timeout, signal, launch error, truncated output and missing PID',async()=>{
 const call=await runProcess(process.execPath,['-e','process.stdout.write("owned child")'],{cwd:REPO_ROOT,timeoutMs:1000,outputLimit:1024});assertOwnerProcess(call);
 for(const edit of [r=>{r.exitCode=1;},r=>{r.timedOut=true;},r=>{r.signal='SIGTERM';},r=>{r.launchError='ENOENT';},r=>{r.outputTruncated=true;},r=>{r.pid=null;}]){const r={...call};edit(r);assert.throws(()=>assertOwnerProcess(r),/OWNER_CHECKER_PROCESS_FAILED/);}
 const bad=await runProcess(process.execPath,['-e','process.exit(3)'],{cwd:REPO_ROOT,timeoutMs:1000,outputLimit:1024});assert.throws(()=>assertOwnerProcess(bad));
});
test('missing owner report cannot pass filesystem evidence validation',t=>{const root=fs.mkdtempSync(path.join(os.tmpdir(),'owner-missing-report-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));assert.throws(()=>verifyOwnerEvidence({repo:REPO_ROOT,out:root,generated:root,source:'x',sourceInputs:[],externalInputs:[],toolInputs:[],checkerPid:1}),/ENOENT/);});
test('whole public leaf declares real owner files and mandatory original/matrix/owner result reports with unchanged budgets',()=>{
 const d=getMotionConsumerAdapters()[0],inputs=motionConsumerInputs();assert.equal(d.id,'gyro.machine-motion-consumers');assert.equal(d.timeoutMs,900000);assert.equal(ADAPTER_TIMEOUT_MS,900000);assert.equal(PRODUCER_TIMEOUT_MS,840000);assert.equal(OWNER_BUDGET_MS,240000);assert.equal(OWNER_CHILD_TIMEOUT_MS,60000);assert.equal(MATRIX_BUDGET_MS,240000);assert.equal(EXPECTED_MUTATIONS.length,39);assert.equal(MATRIX_FAULTS.length,4);
 assert.deepEqual(d.resultFiles,['m/result.json','m/q/result.json','m/q/p/e/result.json',...Array.from({length:4},(_,i)=>'m/q/f'+i+'/e/result.json'),'m/o/result.json','m/o/p/e/result.json',...Array.from({length:5},(_,i)=>'m/o/f'+i+'/e/result.json')]);
 for(const file of [OWNER_CHECKER,OWNER_CONTRACT,OWNER_PROBE,OWNER_TRACE,'tools/ymcc_sim/global/motion-consumer-signals/owner-runner.mjs','tools/ymcc_sim/global/motion-consumer-signals/owner-evidence.mjs']){assert(inputs.sources.includes(file),file);assert(d.evidenceInputs.includes(file),file);}
 // This checks only entry ordering; actual checker runs have separate evidence.
 const source=fs.readFileSync(path.join(REPO_ROOT,'tools/ymcc_sim/global/motion-consumer-signals/run.mjs'),'utf8');assert(source.indexOf('await runOwnerSupplement')>source.indexOf("if(matrix.status!=='PASS')"));assert(source.indexOf('validateOwnerStage(owner)')>source.indexOf('await runOwnerSupplement'));assert(source.indexOf('validateOwnerStage(owner)')<source.indexOf("result.status='PASS'"));
});
test('owner formal stage cannot treat generated-only as original suite success',async t=>{
 const base=fs.mkdtempSync('D:/owner-formal-negative-');t.after(()=>fs.rmSync(base,{recursive:true,force:true}));const generated=path.join(base,'r');fs.mkdirSync(generated);const source=path.join(base,'main.cpp'),bytes=Buffer.from('// schema only');fs.writeFileSync(source,bytes);fs.writeFileSync(path.join(generated,'source-bindings.json'),JSON.stringify({status:'generated-only',mode:'candidate',consumerOnlyControl:false}));
 const result=await runOwnerSupplement({repo:REPO_ROOT,out:path.join(base,'o'),generated,source,sourceBytes:bytes,sourceInputs:[],externalInputs:[],tools:{files:[]},generatedClosure:[],env:{},adapterStartedAt:Date.now()});assert.equal(result.status,'FAIL');assert.equal(result.failure.code,'OWNER_FULL_ORIGINAL_PRODUCER_REQUIRED');assert.equal(result.positive,null);assert.deepEqual(result.faults,[]);
});

for(const priorFailure of [undefined,{code:'OWNER_CHECKER_PROCESS_FAILED',detail:'Earlier checker timed out.'}])test('owner artifact read failure '+(priorFailure?'retains the first process failure':'cannot pass an otherwise successful stage'),t=>{
 const base=fs.mkdtempSync('D:/owner-closure-negative-'),file=path.join(base,'locked.pdb');
 t.after(()=>{assert(path.resolve(base).startsWith(path.resolve('D:/owner-closure-negative-')));assert(!fs.lstatSync(base).isSymbolicLink());fs.rmSync(base,{recursive:true,force:true});});
 fs.writeFileSync(file,'owned compiler artifact');const read=fs.readFileSync;
 t.mock.method(fs,'readFileSync',function(input,...args){if(input===file){const error=Error('injected sharing violation');error.code='EBUSY';throw error;}return read.call(this,input,...args);});
 const result={status:priorFailure?'FAIL':'PASS',...(priorFailure?{failure:priorFailure}:{})};
 const closure=captureOwnerClosure(base,result);assert.equal(result.status,'FAIL');assert.equal(result.failure.code,priorFailure?.code??'OWNER_ARTIFACT_IDENTITY_UNAVAILABLE');if(priorFailure)assert.equal(result.failure,priorFailure);
 assert.deepEqual(result.sourceInventory,[file]);assert.deepEqual(closure,[]);assert.equal(result.closureFailures.length,1);assert.equal(result.closureFailures[0].path,file);assert.equal(result.closureFailures[0].code,'EBUSY');
});

for(const mode of ['source-read','source-copy','source-drift','report-write','report-identity'])test('owner finalization preserves earlier failure when '+mode,t=>{
 const base=fs.mkdtempSync('D:/owner-finish-negative-'),out=path.join(base,'o'),source=path.join(base,'main.cpp'),beforeFile=path.join(out,'main.before.cpp'),bytes=Buffer.from('// owned source');
 t.after(()=>{assert(path.resolve(base).startsWith(path.resolve('D:/owner-finish-negative-')));assert(!fs.lstatSync(base).isSymbolicLink());fs.rmSync(base,{recursive:true,force:true});});
 fs.mkdirSync(out);fs.writeFileSync(source,bytes);fs.writeFileSync(beforeFile,bytes);
 const failure={code:'OWNER_CHECKER_PROCESS_FAILED',detail:'Original timeout remains the first failure.'},result={status:'FAIL',failure};
 const read=fs.readFileSync,copy=fs.copyFileSync,write=fs.writeFileSync;
 if(mode==='source-read'||mode==='report-identity')t.mock.method(fs,'readFileSync',function(file,...args){if(file===path.join(out,mode==='source-read'?'main.after.cpp':'result.json')){const error=Error('injected finish read failure');error.code='EBUSY';throw error;}return read.call(this,file,...args);});
 if(mode==='source-copy'||mode==='source-drift')t.mock.method(fs,'copyFileSync',function(from,to,...args){if(mode==='source-copy'){const error=Error('injected copy access failure');error.code='EACCES';throw error;}copy.call(this,from,to,...args);fs.writeFileSync(source,'// changed owned source');});
 if(mode==='report-write')t.mock.method(fs,'writeFileSync',function(file,...args){if(file===path.join(out,'result.json')){const error=Error('injected report access failure');error.code='EACCES';throw error;}return write.call(this,file,...args);});
 const finish=()=>finalizeOwnerEvidence({out,source,sourceBytes:bytes,beforeFile,result,start:Date.now()});
 if(mode.startsWith('report-'))assert.throws(finish,error=>{assert.equal(error.evidence.failure,failure);assert.equal(error.evidence.reportError.code,mode==='report-write'?'EACCES':'EBUSY');return true;});
 else{finish();const report=JSON.parse(read.call(fs,path.join(out,'result.json'),'utf8'));assert.equal(report.status,'FAIL');assert.deepEqual(report.failure,failure);assert(report.finalizationFailures.length>0);assert.equal(report.productionCandidateUnchanged,mode==='source-drift'?false:null);}
 assert.equal(result.status,'FAIL');assert.equal(result.failure,failure);
});

for(const copyDenied of [false,true])test('owner finalization with no prior failure '+(copyDenied?'rejects incomplete source evidence':'keeps complete evidence'),t=>{
 const base=fs.mkdtempSync('D:/owner-finish-complete-'),out=path.join(base,'o'),source=path.join(base,'main.cpp'),beforeFile=path.join(out,'main.before.cpp'),bytes=Buffer.from('// owned complete source');
 t.after(()=>{assert(path.resolve(base).startsWith(path.resolve('D:/owner-finish-complete-')));assert(!fs.lstatSync(base).isSymbolicLink());fs.rmSync(base,{recursive:true,force:true});});
 fs.mkdirSync(out);fs.writeFileSync(source,bytes);fs.writeFileSync(beforeFile,bytes);
 if(copyDenied)t.mock.method(fs,'copyFileSync',()=>{const error=Error('injected denied source copy');error.code='EACCES';throw error;});
 const result={status:'PASS'},closure=finalizeOwnerEvidence({out,source,sourceBytes:bytes,beforeFile,result,start:Date.now()});
 const saved=JSON.parse(fs.readFileSync(path.join(out,'result.json'),'utf8'));
 assert.equal(saved.status,copyDenied?'FAIL':'PASS');assert.equal(saved.productionCandidateUnchanged,copyDenied?null:true);
 assert.equal(saved.finalizationFailures.length>0,copyDenied);assert(closure.some(row=>row.path===path.join(out,'result.json')));
 if(copyDenied)assert.equal(saved.failure.code,'OWNER_SOURCE_FINISH_IDENTITY_UNAVAILABLE');else{assert.equal(saved.failure,undefined);assert.equal(saved.closureFailures.length,0);assert.equal(closure.length,3);}
});
