#!/usr/bin/env node
// Orchestrate the existing source-bound suite; all lifecycle behavior stays there.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {REPO,MODULE,ID,SUITE,MUTATION,SENTINEL,MANAGED_DLL,MANAGED_MANIFEST,key,read,fact,sourceInputs,inputPlan,verifyPlan,assertPositiveExpectation} from './adapter.mjs';
const usage='Use --out ABS_NEW_DIRECTORY [--compile-out ABS_NEW_SHORT_DIRECTORY]. Serial current cold-msi-archive (14/545), then rollback-cold-observed-id (14/86; 14 expected failures). Preserves separate complete reports and logs; no normal product or device/service execution.';
const args=process.argv.slice(2);
if(args.length===1&&args[0]==='--help'){console.log(usage);process.exit(0);}
if(![2,4].includes(args.length)||args[0]!=='--out'||!path.isAbsolute(args[1])||(args.length===4&&(args[2]!=='--compile-out'||!path.isAbsolute(args[3]))))throw Error(usage);
const out=path.resolve(args[1]),compileOut=path.resolve(args[3]??path.join(out,'tu'));
if(fs.existsSync(out)||fs.existsSync(compileOut)||key(out)===key(compileOut))throw Error('Fresh distinct out and compile-out required');
if(path.join(compileOut,'rollback','source','native','virtual_target_return_pnp_absence.mutant.h').length>=240)throw Error('Compile path too long; supply a short --compile-out');
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(compileOut,{recursive:true});
const startedAt=new Date().toISOString(),checks=[],runs=[];let before=[],plan=null,error=null;
function check(assertion,passed,detail={}){checks.push({assertion,passed:passed===true,...detail});if(passed!==true)throw Error(assertion);}
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])])):value;
const equal=(a,b)=>JSON.stringify(canonical(a))===JSON.stringify(canonical(b));
const failureSet=rows=>rows.map(r=>JSON.stringify([r.case,r.assertion])).sort();
function drift(){const changed=[];for(const row of before){try{if(fact(row.path).sha256!==row.sha256)changed.push(row.path);}catch{changed.push(row.path);}}return changed;}
function stable(label){
 const changed=drift();check(label+' all source and artifact SHA unchanged',changed.length===0,{changed});
 const now=[...sourceInputs(REPO).map(p=>path.join(REPO,p)),...inputPlan(REPO).artifacts].map(key).sort();
 check(label+' source and artifact membership unchanged',equal([...new Set(now)],before.map(r=>key(r.path)).sort()));
}
function validateNative(dir,negative){
 const manifestPath=path.join(dir,'source-manifest.json'),m=read(manifestPath),bound=new Map(before.map(r=>[key(r.path),r.sha256]));
 check('complete original and generated native input receipt exists',Array.isArray(m.records)&&m.records.length>0&&Array.isArray(m.compiledGenerated)&&m.compiledGenerated.length>0);
 for(const r of [...m.records,...m.copiedButUncompiled])check('native original and snapshot bound: '+r.path,bound.get(key(r.original))===r.sha256&&fact(r.original).sha256===r.sha256&&fact(r.snapshot).sha256===r.sha256);
 for(const r of m.compiledGenerated)check('compiled generated SHA: '+path.basename(r.path),fact(r.path).sha256===r.sha256);
 for(const r of m.sourceExpansion.inputs)check('raw expansion SHA: '+path.basename(r.file),fact(r.file).sha256===r.sha256);
 check('expanded native body SHA',fact(m.sourceExpansion.expandedSnapshot).sha256===m.sourceExpansion.expandedSha256);
 for(const binding of m.compilerHeaderBindings){
  const raw=path.join(dir,'source',binding.source),control=binding.executedControl;
  check('raw native header SHA: '+binding.source,fact(raw).sha256===binding.sha256);
  if(negative&&binding.source==='native/virtual_target_return_pnp_absence.h'){
   check('mutant header actual include and generated SHA',control?.rawHeaderCompiled===false&&key(control.file)===key(path.join(dir,'source/native/virtual_target_return_pnp_absence.mutant.h'))&&m.observedIncludePaths.includes(key(control.file))&&m.compiledGenerated.some(r=>key(r.path)===key(control.file)&&r.sha256===control.sha256)&&fact(control.file).sha256===control.sha256);
  }else check('actual compiler observed header: '+binding.source,!control&&m.observedIncludePaths.includes(key(raw)));
 }
 return {manifest:fact(manifestPath),executable:fact(path.join(dir,'CoordinatorFixture.exe')),compiledSources:m.records.length,compiledGenerated:m.compiledGenerated.length};
}
function cleanupEvidence(dir,fixture,negative){
 const refs=fixture.cases.flatMap(c=>c.managedReceipts??[]),modeDir=path.join(dir,SUITE);
 const rawPaths=fs.readdirSync(modeDir).filter(n=>/^full-pipe-child-\d+\.json$/.test(n)).map(n=>path.join(modeDir,n));
 const expectedChildren=(negative?plan.spec.expectedNegative:plan.spec.expectedPositive).managedChildren;
 check('every raw managed child has a native wait/cleanup receipt',refs.length===expectedChildren&&rawPaths.length===expectedChildren&&new Set(refs.map(r=>key(r.path))).size===refs.length&&rawPaths.every(p=>refs.some(r=>key(r.path)===key(p))));
 const artifacts=new Map(before.map(r=>[key(r.path),r.sha256]));
 for(const ref of refs){
  const child=read(ref.path),state=child.stateAfterFinalCleanup;
  check('native exact child exited and released controller/context: '+ref.processId,ref.naturalExit===true&&ref.controllerReleased===true&&ref.contextReleased===true&&ref.productionHostSession===true&&ref.processId===child.processId&&Number.isInteger(ref.processId)&&ref.processId>0);
  check('raw managed receipt observes loop and successful final cleanup: '+ref.processId,child.status==='OBSERVED'&&child.exitObservationOwner==='native-parent-wait'&&child.failure===null&&child.pipeLoopReturned===true&&child.pipeLoopExitCode===0&&child.finalCleanupOk===true&&child.hasController===false&&child.hasContext===false&&state?.hasController===false&&state?.hasContext===false&&child.productMainExecuted===false&&child.hardwareIo===false&&child.realSdkLoaded===false&&child.actualProductionHostSessionConsumer===true&&child.actualProductionPipeLoop===true&&key(child.buildReceiptPath)===key(MANAGED_MANIFEST)&&child.buildReceiptSha256===plan.spec.managedManifestSha256&&equal(ref.tuple,state.tuple));
  for(const phase of ['startupBindings','finalBindings']){
   const b=child[phase];check('managed '+phase+' original/snapshot/generated counts',b?.sourceOriginals?.length===plan.manifest.inputBindings.sources.length&&b?.sourceSnapshots?.length===plan.manifest.inputBindings.sources.length&&b?.generated?.length===plan.manifest.inputBindings.generatedInputs.length);
   for(const section of ['sourceOriginals','sourceSnapshots','generated'])for(const row of b[section])check('managed '+phase+' '+section+' SHA: '+path.basename(row.path),row.matched===true&&artifacts.get(key(row.path))===row.sha256);
   for(const row of b.loadedAssemblies??[])check('loaded managed assembly declared: '+row.assembly,artifacts.get(key(row.path))===row.sha256);
  }
 }
 if(negative){
  // This exact rollback never creates a Host. Native checks observe no owned
  // child at all 14 case boundaries; the final budget case also proves no spawn.
  const starts=fixture.assertionTrace.filter(r=>r.assertion==='new independent lifecycle case starts with no residual owned child');
  check('zero-child negative has native no-residual/no-spawn evidence',refs.length===0&&starts.length===14&&starts.every(r=>r.passed===true)&&fixture.targetReturnObservations.length===0&&fixture.assertionTrace.some(r=>r.assertion==='MSI exhausted execution retries preserve both duties without writes or spawn'&&r.passed===true));
 }else{
  const waits=fixture.assertionTrace.filter(r=>r.assertion==='owned managed Host fixture exits naturally after real coordinator Stop');
  check('each positive child has native handle wait evidence',waits.length===refs.length&&waits.every(r=>r.passed===true));
 }
 return {managedProcessCleanupVerified:true,children:refs.length,proof:negative?'Native no-residual checks at all case boundaries, final no-spawn check, zero raw children and zero target-create observations':'Native exact process-handle waits plus matching raw OBSERVED disposal receipts',receipts:refs.map(r=>({native:r,raw:fact(r.path)}))};
}
function runVariant(name,negative){
 stable('before '+name);
 const dir=path.join(compileOut,name),logDir=path.join(out,name);fs.mkdirSync(logDir);
 const argv=[path.join(REPO,'tools/input_coordinator_lifecycle_selftest.mjs'),'--out',dir,'--managed-dll',MANAGED_DLL,'--cold-record',path.join(REPO,MODULE,'field-record.json'),'--suite',SUITE,...(negative?['--mutation',MUTATION]:[])];
 const command={executable:process.execPath,args:argv,cwd:REPO,timeoutMs:420000,startedAt:new Date().toISOString()};
 console.log(name+': starting existing '+SUITE+' suite');
 const child=spawnSync(process.execPath,argv,{cwd:REPO,windowsHide:true,encoding:'utf8',timeout:command.timeoutMs,maxBuffer:8*1024*1024,env:{...process.env,PATH:path.join(process.env.SystemRoot,'System32')+';'+process.env.PATH}});
 Object.assign(command,{finishedAt:new Date().toISOString(),exitCode:child.status,signal:child.signal,error:child.error?.message??null});save(path.join(logDir,'command.json'),command);fs.writeFileSync(path.join(logDir,'stdout.log'),child.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(logDir,'stderr.log'),child.stderr??'',{flag:'wx'});
 const run={name,negative,command,outputDirectory:dir};runs.push(run);
 check(name+' harness completed successfully; compile errors/timeouts cannot detect mutation',child.status===0&&!child.error&&!child.signal);
 const receiptPath=path.join(dir,'RESULT.json'),fixturePath=path.join(dir,SUITE,'fixture-result.json'),receipt=read(receiptPath),fixture=read(fixturePath),expected=negative?plan.spec.expectedNegative:plan.spec.expectedPositive;
 Object.assign(run,{receipt:fact(receiptPath),fixture:fact(fixturePath),status:receipt.status,cases:fixture.cases.length,checks:fixture.checks,failedAssertions:fixture.failures.length});
 check(name+' top-level RESULT PASS with no production input drift',receipt.status==='PASS'&&receipt.productionEdited===false&&receipt.changed.length===0&&receipt.expandedSourceChanged===false&&receipt.compiledGeneratedChanged.length===0&&receipt.copiedButUncompiledChanged.length===0);
 check(name+' exactly requested suite/mutation and one full TU',receipt.suite===SUITE&&receipt.mutation===(negative?MUTATION:null)&&receipt.oneCompiledCandidate===true&&receipt.sourceFullTranslationUnit===true&&receipt.normalProductMainExecuted===false&&receipt.realDeviceIo===false&&receipt.results.length===1&&receipt.results[0].mode===SUITE&&receipt.results[0].timedOut===false&&receipt.results[0].exitCode===(negative?1:0)&&equal(receipt.results[0].result,fixture));
 check(name+' fixed case/check count and inert product boundaries',fixture.cases.length===expected.cases&&fixture.checks===expected.checks&&fixture.checks>0&&fixture.assertionTrace.length===fixture.checks&&equal(fixture.cases.map(c=>c.name),plan.spec.caseNames)&&fixture.normalProductMainExecuted===false&&fixture.realDeviceIo===false&&fixture.forbiddenIo===0&&fixture.fullTranslationUnit===true);
 check(name+' byte-exact original archived record',receipt.coldRecordBinding?.sha256===plan.provenance.fieldRecord.sha256&&key(receipt.coldRecordBinding.path)===key(path.join(REPO,MODULE,'field-record.json'))&&fact(receipt.coldRecordBinding.snapshot).sha256===plan.provenance.fieldRecord.sha256);
 check(name+' same fixed managed prerequisite',receipt.managedProof?.dll?.sha256===fact(MANAGED_DLL).sha256&&receipt.managedProof.manifestSha256===plan.spec.managedManifestSha256&&receipt.managedProof.preflight?.status==='VERIFIED-BEFORE-NATIVE-COMPILE');
 const failedTrace=fixture.assertionTrace.filter(r=>r.passed===false);
 check(name+' failures have actual assertion trace evidence',equal(failureSet(failedTrace),failureSet(fixture.failures)));
 if(negative)check('directed semantic negative exact 14 failures, with at least three named sentinels',fixture.status==='FAIL'&&fixture.failures.length===expected.failedAssertions&&fixture.failures.filter(r=>r.assertion===SENTINEL).length>=expected.minimumSentinelFailures&&equal(failureSet(fixture.failures),failureSet(expected.failures)));
 else check('positive actual behavior passes all 545 checks',fixture.status==='PASS'&&fixture.failures.length===0&&fixture.assertionTrace.every(r=>r.passed===true));
 run.native=validateNative(dir,negative);run.cleanup=cleanupEvidence(dir,fixture,negative);stable('after '+name);
 console.log(name+': verified '+run.cases+' cases / '+run.checks+' checks');
}
try{
 plan=verifyPlan(inputPlan(REPO));assertPositiveExpectation(plan.spec);
 const paths=[...sourceInputs(REPO).map(p=>path.join(REPO,p)),...plan.artifacts],unique=new Map(paths.map(p=>[key(p),path.resolve(p)]));before=[...unique.values()].map(fact).sort((a,b)=>key(a.path).localeCompare(key(b.path)));
 save(path.join(out,'INPUTS-BEFORE.json'),{sources:sourceInputs(REPO),artifactInputs:plan.artifacts,files:before});
 runVariant('current',false);runVariant('rollback',true);
 check('exactly green then the one compiled negative ran serially',runs.length===2&&runs[0].negative===false&&runs[1].negative===true);
}catch(failure){error={message:failure.message,stack:failure.stack};checks.push({assertion:'complete bounded archive replay',passed:false,detail:failure.message});}
const changed=drift(),managedProcessCleanupVerified=runs.length===2&&runs.every(r=>r.cleanup?.managedProcessCleanupVerified===true);
const passed=!error&&changed.length===0&&checks.every(r=>r.passed===true)&&managedProcessCleanupVerified;
const result={schemaVersion:1,scenario:ID,status:passed?'PASS':'FAIL',startedAt,finishedAt:new Date().toISOString(),suite:SUITE,mutation:MUTATION,expectedMutationAssertion:SENTINEL,archiveSha256:plan?.provenance.archive.sha256??null,fieldRecordSha256:plan?.provenance.fieldRecord.sha256??null,sourceChanged:changed,productionInputDrift:changed.length,managedProcessCleanupVerified,productionEdited:false,normalProductMainExecuted:false,realDeviceIo:false,runs,checks,error};
save(path.join(out,'RESULT.json'),result);console.log(JSON.stringify({status:result.status,result:path.join(out,'RESULT.json'),managedProcessCleanupVerified,sourceChanged:changed,error:error?.message??null}));process.exitCode=passed?0:1;