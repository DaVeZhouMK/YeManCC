import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {verifyParentBaselineReference,verifyPackageMaintenanceReference,verifyCurrentRunParentBaselineReference} from './release-baseline-reference.mjs';
import {baselineEnvironment,packageMaintenanceEnvironment} from '../global/decky-persistent-lifecycle/baseline-environment.mjs';
import {PARENT_ID} from '../global/decky-persistent-lifecycle/current-parent-global.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
const repo=path.resolve(import.meta.dirname,'../../..'),controlCore=path.resolve(simulationCorePath()),root=process.env.YMCC_BASELINE_REFERENCE_CONTROL_ROOT?path.resolve(process.env.YMCC_BASELINE_REFERENCE_CONTROL_ROOT):path.join(controlCore,'Evidence/FixtureIntake-20261030/Continuation-20261031/BaselineReuse-20261031/Controls');assert(root.toLowerCase().startsWith(controlCore.toLowerCase()+path.sep),'REFERENCE_CONTROL_ROOT_MUST_STAY_IN_CORE');fs.mkdirSync(root,{recursive:true});
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),pin=file=>({path:file,sha256:hash(file),bytes:fs.statSync(file).size}),write=(f,v)=>{fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,JSON.stringify(v,null,2)+'\n');};
// Inert JSON/source fixtures validate bindings only; none is an original product acceptance.
function fixture(){
 const own=fs.mkdtempSync(path.join(root,'owned-')),project=path.join(own,'repo'),core=path.join(own,'core'),run=path.join(core,'Evidence/run'),parentFile=path.join(run,'cases',PARENT_ID,'decky-persistent-lifecycle/result.json'),out=path.dirname(parentFile),globalFile=path.join(run,'result.json');fs.mkdirSync(out,{recursive:true});
 const alias='tools/decky_update_recovery_selftest.mjs',entry='tools/ymcc_sim/global/decky-update-recovery/run.mjs',required='native/original-negative-fixture.h';
 for(const name of [alias,entry,required]){const f=path.join(project,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,name===alias?fs.readFileSync(path.join(repo,alias)):'Inert binding fixture, never compiled or executed.\n');}
 const sources=[alias,entry,required].sort().map(name=>({path:name,sha256:hash(path.join(project,name))})),raw=path.join(core,'Evidence/baseline/result.json'),stdout=path.join(out,'processes/original.plugins.decky-update-recovery.stdout.bin'),stderr=path.join(out,'processes/original.plugins.decky-update-recovery.stderr.bin');fs.mkdirSync(path.dirname(stdout),{recursive:true});fs.writeFileSync(stdout,'inert original output');fs.writeFileSync(stderr,'');
 const original={passed:true,fieldRegression:'decky-update-disconnect-20261020',nativeChecks:1,sourceInputs:sources,inputsUnchanged:true,realSteamActions:0,userSettingsWrites:0,hardwareWrites:0};write(raw,original);
 const start=Date.now()-1000,tools=[pin(process.execPath)],processProof={id:'original.plugins.decky-update-recovery',startedAt:new Date(start+100).toISOString(),finishedAt:new Date(start+200).toISOString(),program:tools[0],arguments:[path.join(project,entry),'--evidence-dir',path.dirname(raw)],cwd:project,originalBudgetMs:240000,timeoutMs:240000,elapsedMs:100,exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false,stdout:pin(stdout),stderr:pin(stderr),boundSourceInputs:sources,toolArtifacts:tools,baselineEnvironment:baselineEnvironment(),originalResult:pin(raw)};
 const processFile=path.join(out,'processes/original.plugins.decky-update-recovery.json'),parent={id:PARENT_ID,status:'PASS',passed:true,fullPersistentAcceptance:true,sourceUnchanged:true,toolsUnchanged:true,metadataIntegrity:true,startedAt:new Date(start).toISOString(),finishedAt:new Date(start+500).toISOString(),sourceInputs:sources,toolArtifacts:tools,processes:[processProof],rawEvidence:[pin(raw)],originalUpdateRecovery:{raw:pin(raw),process:processProof,sourceVerified:true,reused:false},legacyTypedMigration:{status:'INERT_METADATA_TEST_ONLY'}};
 const global={startedAt:new Date(start-100).toISOString(),verdict:{status:'PASS_SELECTED_SCOPE'},results:[{id:PARENT_ID,status:'PASS',exitCode:0}]};
 const refresh=()=>{
  write(processFile,processProof);write(path.join(out,'SOURCE-PINS.json'),parent.sourceInputs);write(path.join(out,'TOOL-PINS.json'),parent.toolArtifacts);write(path.join(out,'REGRESSION-COVERAGE.json'),{oldFailsNotPromoted:true,historical47:47});write(path.join(out,'LEGACY-TYPED-MIGRATION.json'),parent.legacyTypedMigration);write(path.join(out,'RAW-EVIDENCE.json'),{files:parent.rawEvidence,processes:[pin(processFile)],rawVerdictsPreserved:true});write(path.join(out,'HISTORY.json'),{status:'HISTORICAL_FAIL_PRESERVED',oldSummary:{failed:1},files:[]});write(parentFile,parent);write(globalFile,global);
 };
 refresh();const options=()=>({repoRoot:project,coreRoot:core,globalResult:globalFile,globalSha256:hash(globalFile),parentResult:parentFile,parentSha256:hash(parentFile),declaration:{evidenceInputs:sources.map(r=>r.path),artifactInputs:[process.execPath],baselineRequiredSources:[entry,required]}});
 const rebindRaw=()=>{const p=pin(raw);processProof.originalResult=p;parent.originalUpdateRecovery.raw=p;parent.rawEvidence=[p];};
 return {own,project,core,run,out,globalFile,parentFile,global,parent,processProof,processFile,original,raw,sources,alias,required,refresh,options,rebindRaw,verify:()=>verifyParentBaselineReference(options())};
}
test('a fresh complete original in the same parent is shared without launching a second producer',()=>{const f=fixture(),r=f.verify();assert.equal(r.status,'SHARED_REFERENCE');assert.equal(r.originalProducerExecutionsWithinParent,1);assert.equal(r.originalProducerExecutionsByThisReference,0);assert.equal(r.cached,false);assert.equal(r.fullProductAcceptance,false);});
test('another global run or stale supplied digests cannot supply the parent reference',()=>{const f=fixture(),a=f.options();assert.throws(()=>verifyParentBaselineReference({...a,parentSha256:'0'.repeat(64)}),/PARENT_CHANGED/);const elsewhere=path.join(f.core,'Evidence/other/result.json');write(elsewhere,f.global);assert.throws(()=>verifyParentBaselineReference({...a,globalResult:elsewhere,globalSha256:hash(elsewhere)}),/FOREIGN_RUN/);});
test('parent failure or imported baseline cannot be relabeled shared current success',()=>{const f=fixture();f.parent.status='FAIL';f.refresh();assert.throws(f.verify);f.parent.status='PASS';f.parent.originalUpdateRecovery.reused=true;f.refresh();assert.throws(f.verify,/IMPORTED_PREVIOUS_BASELINE/);});
test('a hidden original failure remains failure even if a parent claims success',()=>{const f=fixture();f.original.passed=false;write(f.raw,f.original);f.rebindRaw();f.refresh();assert.throws(f.verify,/BASELINE_ORIGINAL_FAIL/);});
test('a narrowed original command or altered original deadline is not equivalent',()=>{const f=fixture();f.processProof.arguments.push('--positive-only');f.refresh();assert.throws(f.verify,/ORIGINAL_ARGUMENTS/);f.processProof.arguments.pop();f.processProof.timeoutMs=239999;f.refresh();assert.throws(f.verify,/REUSE_REQUIRES_FULL_ORIGINAL_BUDGET/);});
test('foreign process time or extra duplicate producer does not pass same-run admission',()=>{const f=fixture();f.processProof.startedAt='2000-01-01T00:00:00.000Z';f.refresh();assert.throws(f.verify,/PROCESS_NOT_CURRENT/);f.processProof.startedAt=f.parent.startedAt;f.parent.processes.push({...f.processProof});f.refresh();assert.throws(f.verify,/EXECUTION_NOT_UNIQUE/);});
test('raw bytes, log bytes and independent process file are all bound',()=>{const f=fixture();fs.appendFileSync(f.processProof.stdout.path,'changed');assert.throws(f.verify,/PIN_CHANGED/);const g=fixture();write(g.processFile,{...g.processProof,exitCode:7});assert.throws(g.verify,/PROCESS_FILE_MISMATCH/);const h=fixture();fs.appendFileSync(h.raw,' ');assert.throws(h.verify,/PIN_CHANGED/);});
test('changed environment cannot reuse a differently configured baseline',()=>{const f=fixture();f.processProof.baselineEnvironment.YMCC_SIM_POWERSHELL='different-host.exe';f.refresh();assert.throws(f.verify,/ENVIRONMENT_CHANGED/);});
test('changed source or missing original negative source proof rejects sharing',()=>{const f=fixture();fs.appendFileSync(path.join(f.project,f.required),'changed');assert.throws(f.verify,/SOURCE_CHANGED/);const g=fixture();g.processProof.boundSourceInputs=g.sources.filter(r=>r.path!==g.required);g.refresh();assert.throws(g.verify,/SOURCE_PROOF_MISSING/);});
test('unreviewed release alias additions cannot silently lose their new assertions',()=>{const f=fixture();const file=path.join(f.project,f.alias);fs.appendFileSync(file,'\n// new assertion scope\n');f.sources.find(r=>r.path===f.alias).sha256=hash(file);f.refresh();assert.throws(f.verify,/ALIAS_UNREVIEWED/);});
test('different tool scope and corrupt mandatory parent metadata both reject',()=>{const f=fixture();f.processProof.toolArtifacts=[];f.refresh();assert.throws(f.verify,/TOOL_SCOPE_CHANGED/);const g=fixture();write(path.join(g.out,'HISTORY.json'),{status:'PASS',oldSummary:{failed:0},files:[]});assert.throws(g.verify,/HISTORICAL|HISTORY|FAILURE/);});
test('duplicate or absent global parent cannot turn an old producer into a current required step',()=>{const f=fixture();f.global.results.push({...f.global.results[0]});f.refresh();assert.throws(f.verify,/PARENT_NOT_UNIQUE/);const g=fixture();g.global.results=[];g.refresh();assert.throws(g.verify,/PARENT_NOT_UNIQUE/);});

test('the actual release loop defers the alias, runs the parent once and preserves unmet failure',{timeout:15000},()=>{
 const own=fs.mkdtempSync(path.join(root,'flow-')),harness=path.join(own,'harness.ps1'),observed=path.join(own,'observed.json');
 fs.writeFileSync(harness,`param([switch]$ParentFails)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile('${path.join(repo,'tools/decky_update_release_gate.ps1').replaceAll("'","''")}',[ref]$tokens,[ref]$errors)
$node=$ast.Find({param($n) $n -is [System.Management.Automation.Language.TryStatementAst] -and $n.Body.Statements.Count -gt 0 -and $n.Body.Statements[0].Extent.Text.StartsWith('$env:YMCC_DECKY_ARTIFACT_ROOT=')},$true)
if(-not $node){throw 'ORIGINAL_RELEASE_LOOP_NOT_FOUND'}
$script:events=[Collections.Generic.List[object]]::new();$script:baselineStarts=0;$script:parentStarts=0;$steps=[Collections.Generic.List[object]]::new();$script:recoveryRequirement=$null
function Invoke-Required([string]$Name,[string]$Program,[string[]]$Arguments){if($Name -eq 'decky_update_recovery_selftest'){throw 'DUPLICATE_ALIAS_PRODUCER'};$script:events.Add(@{name=$Name;args=$Arguments})|Out-Null}
function Invoke-DeckyLifecycleRequired {$script:parentStarts++;$script:baselineStarts++;if($ParentFails){throw 'ORIGINAL_PARENT_FAIL'};$steps.Add(@{lifecycleProof=@{current='inert-proof'}})|Out-Null;$script:events.Add(@{name='parent'})|Out-Null}
function Complete-DeckyRecoverySharedRequirement($Step){if($script:baselineStarts -ne 1 -or $script:recoveryRequirement.status -ne 'NOT_RUN_DEFERRED_TO_PARENT'){throw 'REFERENCE_BEFORE_PRODUCER'};$script:recoveryRequirement.status='SHARED_REFERENCE';$script:events.Add(@{name='shared-reference'})|Out-Null}
$gateRoot=$PSScriptRoot;$priorArtifactRoot='caller-original-root';$env:YMCC_DECKY_ARTIFACT_ROOT=$priorArtifactRoot;$caught=$false
$actual=Join-Path $PSScriptRoot 'actual-loop.ps1';[IO.File]::WriteAllText($actual,$node.Extent.Text)
try{. $actual}catch{if($_.Exception.Message -ne 'ORIGINAL_PARENT_FAIL'){throw};$caught=$true}
@{events=@($script:events);baselineStarts=$script:baselineStarts;parentStarts=$script:parentStarts;requirement=$script:recoveryRequirement;caught=$caught;restored=$env:YMCC_DECKY_ARTIFACT_ROOT}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath '${observed.replaceAll("'","''")}' -Encoding UTF8
`);
 for(const fails of [false,true]){
  const r=spawnSync('pwsh.exe',['-NoProfile','-File',harness,...(fails?['-ParentFails']:[])],{encoding:'utf8',timeout:12000,windowsHide:true});assert.equal(r.status,0,r.stderr+r.stdout);const v=JSON.parse(fs.readFileSync(observed,'utf8').replace(/^\uFEFF/,''));
  assert.equal(v.baselineStarts,1);assert.equal(v.parentStarts,1);assert.equal(v.restored,'caller-original-root');assert.equal(v.caught,fails);assert.equal(v.requirement.status,fails?'NOT_RUN_DEFERRED_TO_PARENT':'SHARED_REFERENCE');assert.equal(v.events.some(x=>x.name==='decky_update_recovery_selftest'),false);assert.equal(v.events.filter(x=>x.name==='shared-reference').length,fails?0:1);
 }
});

// INERT metadata admission controls only. Owned text files named .exe are never executed.
// Extend the existing reference tests; no new registered business ID or release parent.
const inertPackageMutations=[
 {name:'skip-incoming-integrity',expectedCase:'package hash mismatch is rejected before any installed mutation',expectedFailure:'Expected rejection PACKAGE_HASH_MISMATCH, got '},
 {name:'no-rollback',expectedCase:'mid-transaction copy failure restores all prior committed files',expectedFailure:'Failed install did not restore exact prior state'},
 {name:'stale-rollback-snapshot',expectedCase:'rollback revalidates every file after its initial snapshot before restoring it',expectedFailure:'Expected rejection ROLLBACK_CONCURRENT_CHANGE, got '}
];
function packageMaintenanceFixture(){
 const f=fixture(),base=path.dirname(f.raw),packageRoot=path.join(base,'package-maintenance'),systemRoot=path.join(f.own,'INERT-Windows'),shell=path.join(systemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
 const put=(file,text)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,text);return pin(file);};
 const sourceNames=['tools/decky_package_maintenance_selftest.ps1','native/decky_package_maintenance.ps1','native/decky_package_maintenance_script.h'];
 for(const name of sourceNames){put(path.join(f.project,name),'INERT private source identity; never executed or compiled.\n');f.sources.push({path:name,sha256:hash(path.join(f.project,name))});}
 const executable=put(shell,'INERT private shell identity; not an executable.\n'),compiler=path.join(f.own,'INERT-tools/compiler.exe'),compilerPin=put(compiler,'INERT private compiler identity; not an executable.\n'),manifest=path.join(f.own,'INERT-tools/authority.json');
 write(manifest,{fixtureKind:'INERT_METADATA_ONLY',compiler});
 const authority={compiler,manifestSha256:hash(manifest),files:[compilerPin,pin(manifest)]},environment=packageMaintenanceEnvironment({...process.env,SystemRoot:systemRoot});
 f.parent.toolArtifacts.push(executable,...authority.files);
 const ordinaryNames=[...inertPackageMutations.map(row=>row.expectedCase),...Array.from({length:29},(_,i)=>'INERT ordinary assertion '+(i+4))];
 const result={fixtureKind:'INERT_METADATA_ONLY',pass:true,status:'PASS',complete:true,totalBudgetMs:30000,runBudgetMs:25000,cleanupReserveMs:5000,executionScope:'full-original-ordinary-suite',selectedMutation:'',selectedCase:null,realSteamActions:0,hardwareWrites:0,productProcessesLaunched:0,productionGlobalMutexAccessed:false,fixtureMutexNamespaceRemapped:true,declaredOrdinaryCaseCount:32,executedOrdinaryCaseCount:32,omittedOrdinaryCases:[],count:33,passed:33,cases:[...ordinaryNames.map(name=>({name,pass:true,executed:true,fixtureKind:'INERT_METADATA_ONLY'})),{name:'INERT mutation summary',pass:true,executed:true,fixtureKind:'INERT_METADATA_ONLY'}],mutations:Object.fromEntries(inertPackageMutations.map(row=>[row.name,'EXPECTED_NONZERO'])),mutationRetirement:[],sourceSha256:hash(path.join(f.project,sourceNames[1])),embeddedSha256:hash(path.join(f.project,sourceNames[2]))};
 const negativeResults=inertPackageMutations.map(def=>{
  const independentRoot=path.join(packageRoot,'mutation-'+def.name),file=path.join(independentRoot,'evidence/result.json');
  const raw={fixtureKind:'INERT_METADATA_ONLY',complete:true,status:'FAIL',pass:false,executionScope:'selected-original-mutation-assertion',selectedMutation:def.name,selectedCase:def.expectedCase,count:1,passed:0,declaredOrdinaryCaseCount:32,executedOrdinaryCaseCount:1,omittedOrdinaryCases:ordinaryNames.filter(name=>name!==def.expectedCase),cases:[{name:def.expectedCase,pass:false,executed:true,error:def.expectedFailure,fixtureKind:'INERT_METADATA_ONLY'}]};
  write(file,raw);
  result.mutationRetirement.push({...def,independentRoot,executionScope:'selected-original-mutation-assertion',plannedCaseCount:1,jobAssigned:true,jobCloseSucceeded:true,exitObserved:true,sharedDeadline:true,timedOut:false,error:null,exitCode:1});
  return {name:def.name,result:pin(file)};
 });
 const stdout=put(path.join(base,'package-maintenance.stdout.raw'),'INERT captured package stdout\n'),stderr=put(path.join(base,'package-maintenance.stderr.raw'),'');
 const packageProcess={fixtureKind:'INERT_METADATA_ONLY',executable,source:pin(path.join(f.project,sourceNames[0])),sourceSha256After:hash(path.join(f.project,sourceNames[0])),toolSha256After:executable.sha256,sourceReadErrorAfter:null,toolReadErrorAfter:null,cwd:fs.realpathSync(f.project),environment,args:['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(f.project,sourceNames[0]),'-EvidenceDir',packageRoot,'-TotalBudgetMs','30000','-RunBudgetMs','25000','-CleanupReserveMs','5000'],status:0,signal:null,error:null,timedOut:false,stageBudgetMs:30000,innerTotalBudgetMs:30000,runBudgetMs:25000,cleanupReserveMs:5000,registeredAdapterBudgetMs:240000,startedAt:new Date(Date.parse(f.processProof.startedAt)+10).toISOString(),finishedAt:new Date(Date.parse(f.processProof.startedAt)+20).toISOString(),elapsedMs:10,stdoutBytes:stdout.bytes,stderrBytes:stderr.bytes,fixtureCompilerAuthority:{...authority,inputs:authority.files.length}};
 const packageProcessFile=path.join(base,'package-maintenance-process.json'),resultFile=path.join(packageRoot,'result.json');
 const refresh=()=>{
  write(packageProcessFile,packageProcess);write(resultFile,result);
  f.original.packageMaintenance=result;f.original.packageMaintenanceExecution={schemaVersion:1,process:pin(packageProcessFile),result:pin(resultFile),stdout:pin(stdout.path),stderr:pin(stderr.path),negativeResults};
  write(f.raw,f.original);f.rebindRaw();f.refresh();
 };
 const options=()=>({...f.options(),requirePackageMaintenance:true,packageEnvironment:environment,packageFixtureAuthority:authority,declaration:{...f.options().declaration,artifactInputs:f.parent.toolArtifacts.map(row=>row.path)}});
 const verify=()=>verifyPackageMaintenanceReference({repoRoot:f.project,baselineResult:f.raw,baselineValue:f.original,originalProcess:f.processProof,sourceInputs:f.sources,toolArtifacts:f.parent.toolArtifacts,expectedEnvironment:environment,authority});
 refresh();
 return {...f,result,packageProcess,packageProcessFile,resultFile,negativeResults,environment,authority,executable,refresh,options,verify,verifyParent:()=>verifyParentBaselineReference(options())};
}
test('INERT complete package receipt shares 32 ordinary assertions and three retired negatives with zero producers',()=>{
 const f=packageMaintenanceFixture(),r=f.verify(),parent=f.verifyParent();
 assert.equal(f.result.count,33);assert.equal(f.result.cases.length,33);assert.equal(r.status,'SHARED_REFERENCE');assert.equal(r.ordinaryCases,32);assert.equal(r.semanticNegativeMutations,3);assert.equal(r.originalProducerExecutionsWithinParent,1);assert.equal(r.originalProducerExecutionsByThisReference,0);assert.equal(r.originalBudgetMs,30000);assert.deepEqual(r.originalArguments,f.packageProcess.args);assert.equal(r.cached,false);assert.equal(r.fullProductAcceptance,false);assert.deepEqual(parent.packageMaintenance,r);
 assert.equal(f.result.selectedMutation,'');f.result.selectedMutation=null;f.refresh();assert.equal(f.verify().status,'SHARED_REFERENCE');
 for(const row of f.negativeResults){const raw=JSON.parse(fs.readFileSync(row.result.path,'utf8'));assert.equal(raw.fixtureKind,'INERT_METADATA_ONLY');assert.equal(raw.status,'FAIL');assert.equal(raw.pass,false);}
});
test('INERT package raw, logs and process bytes cannot change behind their pins',()=>{
 for(const field of ['result','stdout','stderr','process']){const f=packageMaintenanceFixture();fs.appendFileSync(f.original.packageMaintenanceExecution[field].path,' ');assert.throws(f.verify,/PIN_CHANGED/,field);}
});
test('INERT package bindings reject a different result, log size or foreign process path even after repinning',()=>{
 const f=packageMaintenanceFixture();write(f.resultFile,{...f.result,complete:false});f.original.packageMaintenanceExecution.result=pin(f.resultFile);assert.throws(f.verify,/RESULT_NOT_IN_BASELINE/);
 const g=packageMaintenanceFixture();g.packageProcess.stdoutBytes++;g.refresh();assert.throws(g.verify,/STDOUT_SIZE/);
 const h=packageMaintenanceFixture(),other=path.join(h.own,'INERT-foreign-process.json');write(other,h.packageProcess);h.original.packageMaintenanceExecution.process=pin(other);assert.throws(h.verify,/PATH_CHANGED/);
});
test('INERT negative originals remain FAIL and cannot be omitted, altered, swapped or promoted',()=>{
 const missing=packageMaintenanceFixture();missing.original.packageMaintenanceExecution.negativeResults.pop();assert.throws(missing.verify,/PACKAGE_REFERENCE_/);
 for(const mode of ['bytes','pass','assertion','selection','swap']){
  const f=packageMaintenanceFixture(),row=f.negativeResults[0],raw=JSON.parse(fs.readFileSync(row.result.path,'utf8'));
  if(mode==='bytes')fs.appendFileSync(row.result.path,' ');
  else if(mode==='swap')row.result=f.negativeResults[1].result;
  else{if(mode==='pass'){raw.status='PASS';raw.pass=true;raw.passed=1;raw.cases[0].pass=true;}if(mode==='assertion')raw.cases[0].error='INERT unrelated failure';if(mode==='selection')raw.selectedMutation='no-rollback';write(row.result.path,raw);row.result=pin(row.result.path);}
  assert.throws(f.verify,/PIN_CHANGED|PACKAGE_REFERENCE_/,mode);
 }
});
test('INERT negatives require all original mutations, nonzero semantic failures and complete retirement',()=>{
 const edits=[r=>{delete r.mutations['no-rollback'];},r=>{r.mutations['no-rollback']='PASS';},r=>{r.mutationRetirement.pop();},r=>{r.mutationRetirement[1]={...r.mutationRetirement[0]};}];
 for(const field of ['jobAssigned','jobCloseSucceeded','exitObserved','sharedDeadline'])edits.push(r=>{r.mutationRetirement[0][field]=false;});
 edits.push(r=>{r.mutationRetirement[0].timedOut=true;},r=>{r.mutationRetirement[0].exitCode=0;},r=>{r.mutationRetirement[0].error='INERT retirement failure';});
 for(const edit of edits){const f=packageMaintenanceFixture();edit(f.result);f.refresh();assert.throws(f.verify);}
});
test('INERT incomplete ordinary coverage and unexecuted or failed assertions cannot share',()=>{
 const edits=[r=>{r.executedOrdinaryCaseCount=31;},r=>{r.omittedOrdinaryCases=['INERT omitted'];},r=>{r.declaredOrdinaryCaseCount=31;},r=>{r.cases[0].executed=false;},r=>{r.cases[0].pass=false;},r=>{r.cases.pop();},r=>{r.passed=32;},r=>{r.complete=false;},r=>{delete r.selectedMutation;},r=>{r.selectedMutation='no-rollback';},r=>{r.selectedCase='INERT selected';},r=>{r.executionScope='selected-original-mutation-assertion';}];
 for(const edit of edits){const f=packageMaintenanceFixture();edit(f.result);f.refresh();assert.throws(f.verify,/PACKAGE_REFERENCE_/);}
});
test('INERT package timeout, failed process and exhausted deadline cannot share',()=>{
 for(const [field,value] of Object.entries({timedOut:true,status:1,signal:'SIGTERM',error:'INERT timeout',elapsedMs:30001})){const f=packageMaintenanceFixture();f.packageProcess[field]=value;f.refresh();assert.throws(f.verify,/PACKAGE_REFERENCE_/,field);}
});
test('INERT package filtered arguments and altered original budget partitions are rejected',()=>{
 for(const extra of [['-SkipMutationProof'],['-MutationCase','no-rollback'],['-Case','INERT selected']]){const f=packageMaintenanceFixture();f.packageProcess.args.push(...extra);f.refresh();assert.throws(f.verify,/ORIGINAL_ARGUMENTS/);}
 for(const field of ['stageBudgetMs','innerTotalBudgetMs','runBudgetMs','cleanupReserveMs','registeredAdapterBudgetMs']){const f=packageMaintenanceFixture();f.packageProcess[field]--;f.refresh();assert.throws(f.verify,/PACKAGE_REFERENCE_PROCESS_/,field);}
 for(const field of ['totalBudgetMs','runBudgetMs','cleanupReserveMs']){const f=packageMaintenanceFixture();f.result[field]--;f.refresh();assert.throws(f.verify,/PACKAGE_REFERENCE_RESULT_/,field);}
 const f=packageMaintenanceFixture();f.packageProcess.args[f.packageProcess.args.indexOf('30000')]='29999';f.refresh();assert.throws(f.verify,/ORIGINAL_ARGUMENTS/);
});
test('INERT package execution must remain within the current original parent process interval',()=>{
 for(const edit of [f=>{f.packageProcess.startedAt=new Date(Date.parse(f.processProof.startedAt)-1).toISOString();},f=>{f.packageProcess.finishedAt=new Date(Date.parse(f.processProof.finishedAt)+1).toISOString();},f=>{f.packageProcess.finishedAt=f.processProof.startedAt;},f=>{f.packageProcess.startedAt='INERT invalid timestamp';}]){const f=packageMaintenanceFixture();edit(f);f.refresh();assert.throws(f.verify,/NOT_CURRENT/);}
});
test('INERT package environment, source and working directory drift reject sharing',()=>{
 for(const field of ['PATH','PSModulePath','TEMP','TMP','YMCC_SIM_POWERSHELL']){const f=packageMaintenanceFixture();f.packageProcess.environment={...f.environment,[field]:'INERT changed'};f.refresh();assert.throws(f.verify,/ENVIRONMENT_CHANGED/,field);}
 const f=packageMaintenanceFixture();fs.appendFileSync(f.packageProcess.source.path,'INERT changed');assert.throws(f.verify,/PIN_CHANGED/);
 const g=packageMaintenanceFixture();g.packageProcess.sourceSha256After='0'.repeat(64);g.refresh();assert.throws(g.verify,/SOURCE_CHANGED/);
 for(const field of ['sourceSha256','embeddedSha256']){const h=packageMaintenanceFixture();h.result[field]='0'.repeat(64);h.refresh();assert.throws(h.verify,/SOURCE_CHANGED/,field);}
 const h=packageMaintenanceFixture();h.packageProcess.cwd=h.own;h.refresh();assert.throws(h.verify,/CWD_CHANGED/);
});
test('INERT package shell and compiler must retain their exact prebound tool identities',()=>{
 const f=packageMaintenanceFixture();fs.appendFileSync(f.executable.path,'INERT changed');assert.throws(f.verify,/PIN_CHANGED/);
 const g=packageMaintenanceFixture();g.parent.toolArtifacts.splice(g.parent.toolArtifacts.findIndex(row=>row.path===g.executable.path),1);assert.throws(g.verify,/SHELL_NOT_PREBOUND/);
 const h=packageMaintenanceFixture();h.parent.toolArtifacts.splice(h.parent.toolArtifacts.findIndex(row=>row.path===h.authority.compiler),1);assert.throws(h.verify,/TOOL_NOT_PREBOUND/);
 const j=packageMaintenanceFixture();fs.appendFileSync(j.authority.compiler,'INERT changed');assert.throws(j.verify,/PIN_CHANGED/);
 const k=packageMaintenanceFixture();k.packageProcess.fixtureCompilerAuthority={...k.packageProcess.fixtureCompilerAuthority,manifestSha256:'0'.repeat(64)};k.refresh();assert.throws(k.verify,/COMPILER_AUTHORITY/);
});
test('INERT requirePackageMaintenance cannot accept an otherwise valid legacy baseline without package proof',()=>{
 const f=fixture();assert.equal(f.verify().status,'SHARED_REFERENCE');
 assert.throws(()=>verifyParentBaselineReference({...f.options(),requirePackageMaintenance:true,packageFixtureAuthority:{fixtureKind:'INERT_METADATA_ONLY'}}),/EXECUTION_BINDING_REQUIRED/);
});
test('INERT failed baseline or parent remains failure with a complete package proof',()=>{
 const f=packageMaintenanceFixture();f.original.passed=false;f.refresh();assert.throws(f.verify,/BASELINE_NOT_PASS/);assert.throws(f.verifyParent,/BASELINE_ORIGINAL_FAIL/);assert.equal(JSON.parse(fs.readFileSync(f.raw,'utf8')).passed,false);
 const g=packageMaintenanceFixture();g.parent.status='FAIL';g.parent.passed=false;g.refresh();assert.throws(g.verifyParent);assert.equal(JSON.parse(fs.readFileSync(g.parentFile,'utf8')).status,'FAIL');
});
test('INERT actual PowerShell sharing function uses one verifier for all original steps and preserves deferred parent failure',{timeout:15000},()=>{
 const f=packageMaintenanceFixture(),reference=f.verifyParent(),own=fs.mkdtempSync(path.join(root,'package-flow-')),harness=path.join(own,'harness.ps1'),template=path.join(own,'INERT-reference.json'),observed=path.join(own,'observed.json');reference.resources=Object.fromEntries([['cache','decky-local-resource-cache'],['worker','decky-owned-resource-worker']].map(([family,step])=>[family,{fixtureKind:'INERT_METADATA_ONLY',status:'SHARED_REFERENCE',requiredReleaseStep:step,originalProducerExecutionsWithinParent:1,originalProducerExecutionsByThisReference:0,cached:false,originalProcess:reference.originalProcess,originalResult:reference.originalResult,originalElapsedMs:1}]));write(template,reference);
 fs.writeFileSync(harness,`param([string]$Mode)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile('${path.join(repo,'tools/decky_update_release_gate.ps1').replaceAll("'","''")}',[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'ACTUAL_GATE_PARSE_FAILED'}
$function=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Complete-DeckyRecoverySharedRequirement'},$true)
$loop=$ast.Find({param($n) $n -is [System.Management.Automation.Language.TryStatementAst] -and $n.Body.Statements.Count -gt 0 -and $n.Body.Statements[0].Extent.Text.StartsWith('$env:YMCC_DECKY_ARTIFACT_ROOT=')},$true)
if(-not $function -or -not $loop){throw 'ACTUAL_SHARING_AST_MISSING'}
$actualFunction=Join-Path $PSScriptRoot 'actual-shared-function.ps1';[IO.File]::WriteAllText($actualFunction,$function.Extent.Text);. $actualFunction
$script:reference=Get-Content -LiteralPath '${template.replaceAll("'","''")}' -Raw|ConvertFrom-Json
if($Mode -eq 'missing-package'){$script:reference.PSObject.Properties.Remove('packageMaintenance')}
if($Mode -eq 'package-producer'){$script:reference.packageMaintenance.originalProducerExecutionsByThisReference=1}
if($Mode -eq 'missing-resource'){$script:reference.resources.PSObject.Properties.Remove('cache')}
if($Mode -eq 'resource-producer'){$script:reference.resources.worker.originalProducerExecutionsByThisReference=1}
$script:helperCalls=0;$script:parentStarts=0;$steps=[Collections.Generic.List[object]]::new();$script:recoveryRequirement=$null
$script:packageMaintenanceRequirement=@{name='decky-package-maintenance';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}
$script:resourceRequirements=@{cache=@{name='decky-local-resource-cache';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0};worker=@{name='decky-owned-resource-worker';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}}
function Invoke-Required([string]$Name,[string]$Program,[string[]]$Arguments){
 if($Name -ne 'decky_update_recovery_selftest'){return}
 if($Program -cne 'node' -or [IO.Path]::GetFileName($Arguments[0]) -cne 'release-baseline-reference.mjs' -or $Arguments.Count -ne 11){throw 'UNEXPECTED_SHARED_HELPER'}
 $expected=@('--global-result',$script:reference.globalResult.path,'--global-sha256',$script:reference.globalResult.sha256,'--parent-result',$script:reference.parentResult.path,'--parent-sha256',$script:reference.parentResult.sha256,'--out',(Join-Path (Split-Path -Parent $script:reference.globalResult.path) 'release-baseline-shared-reference.json'))
 for($i=0;$i -lt $expected.Count;$i++){if($Arguments[$i+1] -cne $expected[$i]){throw 'SHARED_HELPER_BINDING_CHANGED'}}
 $script:helperCalls++;$script:reference|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $Arguments[10] -Encoding UTF8
 $steps.Add(@{name=$Name;passed=$true;exitCode=0})|Out-Null
}
function Invoke-DeckyLifecycleRequired {
 $script:parentStarts++
 if($Mode -eq 'parent-fail'){$steps.Add(@{name='decky-persistent-lifecycle';passed=$false;status='FAIL'})|Out-Null;throw 'INERT_PARENT_FAIL'}
 $steps.Add(@{name='decky-persistent-lifecycle';passed=$true;lifecycleProof=@{globalResult=$script:reference.globalResult;producerResult=$script:reference.parentResult}})|Out-Null
}
$gateRoot=$PSScriptRoot;$priorArtifactRoot='INERT-caller-root';$env:YMCC_DECKY_ARTIFACT_ROOT=$priorArtifactRoot;$caught=$null
$actualLoop=Join-Path $PSScriptRoot 'actual-shared-loop.ps1';[IO.File]::WriteAllText($actualLoop,$loop.Extent.Text)
try{. $actualLoop}catch{$caught=$_.Exception.Message}
@{fixtureKind='INERT_METADATA_ONLY';helperCalls=$script:helperCalls;parentStarts=$script:parentStarts;steps=$steps.ToArray();recovery=$script:recoveryRequirement;package=$script:packageMaintenanceRequirement;resources=$script:resourceRequirements;caught=$caught;restored=$env:YMCC_DECKY_ARTIFACT_ROOT}|ConvertTo-Json -Depth 30|Set-Content -LiteralPath '${observed.replaceAll("'","''")}' -Encoding UTF8
`);
 for(const mode of ['valid','missing-package','package-producer','missing-resource','resource-producer','parent-fail']){
  const child=spawnSync('pwsh.exe',['-NoProfile','-File',harness,'-Mode',mode],{encoding:'utf8',timeout:3000,windowsHide:true});assert.equal(child.status,0,child.stderr+child.stdout);const v=JSON.parse(fs.readFileSync(observed,'utf8').replace(/^\uFEFF/,''));
  assert.equal(v.fixtureKind,'INERT_METADATA_ONLY');assert.equal(v.parentStarts,1);assert.equal(v.restored,'INERT-caller-root');assert.equal(v.helperCalls,mode==='parent-fail'?0:1);
  const shared=v.steps.filter(row=>row.execution==='SHARED_REFERENCE');
  if(mode==='valid'){
   assert.equal(v.caught,null);assert.equal(shared.length,4);assert.deepEqual(shared.map(row=>row.name),['decky_update_recovery_selftest','decky-package-maintenance','decky-local-resource-cache','decky-owned-resource-worker']);
   for(const row of shared)assert.equal(row.originalProducerExecutions,0);
   assert.equal(shared[0].verificationExecuted,true);assert.equal(shared[1].verificationExecuted,false);assert.equal(shared[1].verificationSharedWith,'decky_update_recovery_selftest');assert.equal(shared[1].sharedReference.path,shared[0].sharedReference.path);assert.equal(shared[1].sharedReference.sha256,shared[0].sharedReference.sha256);
   for(const requirement of [v.recovery,v.package,v.resources.cache,v.resources.worker]){assert.equal(requirement.status,'SHARED_REFERENCE');assert.equal(requirement.originalProducerExecutionsWithinParent,1);assert.equal(requirement.originalProducerExecutionsByThisReference,0);}
  }else{
   assert.match(v.caught,mode==='parent-fail'?/INERT_PARENT_FAIL/:mode.includes('resource')?/DECKY_RESOURCE_SHARED_SCOPE_INVALID/:/DECKY_PACKAGE_MAINTENANCE_SHARED_SCOPE_INVALID/);assert.equal(shared.length,0);assert.equal(v.recovery.status,'NOT_RUN_DEFERRED_TO_PARENT');assert.equal(v.package.status,'NOT_RUN_DEFERRED_TO_PARENT');assert.equal(v.resources.cache.status,'NOT_RUN_DEFERRED_TO_PARENT');assert.equal(v.resources.worker.status,'NOT_RUN_DEFERRED_TO_PARENT');
   if(mode==='parent-fail')assert.equal(v.steps.find(row=>row.name==='decky-persistent-lifecycle').status,'FAIL');
  }
 }
});

// These extend the existing inert metadata fixture; no real parent/baseline/compiler is started.
function currentRunParentFixture(){
 const f=fixture(),start=Date.parse(f.parent.startedAt);
 const parentExecution={id:PARENT_ID,status:'PASS',exitCode:0,pid:4242,
  source:'tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs',
  evidenceDir:path.join(f.run,'cases',PARENT_ID),startedAt:new Date(start-10).toISOString(),finishedAt:new Date(start+510).toISOString(),
  sourceChanged:false,candidateArtifactChanged:false,timedOut:false,outputTruncated:false,
  requiredEvidenceMissing:false,requiredEvidenceFailed:false,signal:null,launchError:null,
  structuredEvidence:[{...pin(f.parentFile),failed:false,reportedSourceCount:f.sources.length,sourceMismatches:[]}],
  fixtureKind:'INERT_CURRENT_PARENT_OBSERVATION_NOT_RUNTIME_ACCEPTANCE'};
 const options=()=>({repoRoot:f.project,coreRoot:f.core,runDir:f.run,runStartedAt:f.global.startedAt,parentExecution,declaration:f.options().declaration});
 const refresh=()=>{f.refresh();parentExecution.structuredEvidence=[{...pin(f.parentFile),failed:false,reportedSourceCount:f.sources.length,sourceMismatches:[]}];};
 return {...f,parentExecution,currentOptions:options,refreshCurrent:refresh,scopeFile:path.join(f.run,'parent-baseline-reference-scope.json'),verifyCurrent:()=>verifyCurrentRunParentBaselineReference(options())};
}
test('current parent subset shares the one actual parent producer without claiming whole global PASS',()=>{
 const f=currentRunParentFixture();f.global.verdict.status='STILL_RUNNING';write(f.globalFile,f.global);const untouched=hash(f.globalFile);
 const r=f.verifyCurrent(),subset=JSON.parse(fs.readFileSync(f.scopeFile,'utf8'));
 assert.equal(r.status,'SHARED_REFERENCE');assert.equal(r.canonicalTestId,'plugins.decky-update-recovery');assert.equal(r.parentId,PARENT_ID);
 assert.equal(r.originalProducerExecutionsWithinParent,1);assert.equal(r.originalProducerExecutionsByThisReference,0);assert.equal(r.originalProducerExecutionsByThisCall,0);
 assert.equal(r.verificationExecuted,true);assert.equal(r.cached,false);assert.equal(r.fullProductAcceptance,false);assert.equal(r.globalFullPassed,false);
 assert.deepEqual(r.originalResult,pin(f.raw));assert.deepEqual(r.parentResult,pin(f.parentFile));assert.deepEqual(r.scopeResult,pin(f.scopeFile));
 assert.equal(r.verificationWallMs>=r.parentVerificationWallMs,true);assert.deepEqual(r.globalResult,r.scopeResult);
 assert.equal(subset.scope,'current-run-completed-parent-subset');assert.equal(subset.plannedTests,1);assert.deepEqual(subset.results,[f.parentExecution]);
 assert.deepEqual(subset.verdict,{status:'PASS_SELECTED_SCOPE',exitCode:0});assert.equal(subset.globalFullPassed,false);assert.equal(subset.fullProductAcceptance,false);
 assert.equal(subset.runStartedAt,f.global.startedAt);assert.equal(hash(f.globalFile),untouched);
});
test('current parent subset rejects failed incomplete or unobserved adapter facts before writing scope',()=>{
 const f=currentRunParentFixture(),unchanged=JSON.stringify(f.parentExecution);
 const edits=[
  e=>e.status='FAIL',e=>e.status='BLOCKED',e=>e.exitCode=2,e=>e.id='plugins.other-parent',e=>e.pid=null,
  e=>e.sourceChanged=true,e=>delete e.sourceChanged,e=>e.candidateArtifactChanged=true,e=>delete e.candidateArtifactChanged,
  e=>e.timedOut=true,e=>e.outputTruncated=true,e=>e.requiredEvidenceMissing=true,e=>e.requiredEvidenceFailed=true,
  e=>e.signal='SIGTERM',e=>e.launchError='ENOENT',e=>e.buildError=true,
  e=>e.source='tools/ymcc_sim/global/decky-persistent-lifecycle/owned-run.mjs',
  e=>delete e.startedAt,e=>delete e.finishedAt,e=>e.startedAt='not-a-time',
 ];
 for(const edit of edits){const e=JSON.parse(unchanged);edit(e);assert.throws(()=>verifyCurrentRunParentBaselineReference({...f.currentOptions(),parentExecution:e}),/CURRENT_RUN_BASELINE_REFERENCE/);assert.equal(fs.existsSync(f.scopeFile),false);}
});
test('current parent subset rejects historical reversed future or missing actual run windows',()=>{
 const f=currentRunParentFixture(),unchanged=JSON.stringify(f.parentExecution);
 for(const edit of [
  e=>e.startedAt='2000-01-01T00:00:00.000Z',
  e=>e.finishedAt=f.global.startedAt,
  e=>e.finishedAt=new Date(Date.now()+60000).toISOString(),
  e=>e.startedAt=new Date(Date.parse(f.parent.startedAt)+1).toISOString(),
  e=>e.finishedAt=new Date(Date.parse(f.parent.finishedAt)-1).toISOString(),
 ]){const e=JSON.parse(unchanged);edit(e);assert.throws(()=>verifyCurrentRunParentBaselineReference({...f.currentOptions(),parentExecution:e}),/CURRENT_RUN_BASELINE_REFERENCE_(ADAPTER_NOT_CURRENT|RAW_OUTSIDE_ADAPTER_WINDOW)/);assert(!fs.existsSync(f.scopeFile));}
 for(const value of [undefined,null,'not-a-time',NaN]){assert.throws(()=>verifyCurrentRunParentBaselineReference({...f.currentOptions(),runStartedAt:value}),/TIME_REQUIRED|TIME_INVALID/);assert(!fs.existsSync(f.scopeFile));}
});
test('current parent subset requires unique current raw pin and the exact runner evidence directory',()=>{
 const f=currentRunParentFixture(),unchanged=JSON.stringify(f.parentExecution);
 for(const edit of [
  e=>delete e.structuredEvidence,
  e=>e.structuredEvidence=[],
  e=>e.structuredEvidence.push({...e.structuredEvidence[0]}),
  e=>e.structuredEvidence[0].failed=true,
  e=>e.structuredEvidence[0].sha256='0'.repeat(64),
  e=>e.structuredEvidence[0].path=f.raw,
  e=>e.evidenceDir=f.out,
 ]){const e=JSON.parse(unchanged);edit(e);assert.throws(()=>verifyCurrentRunParentBaselineReference({...f.currentOptions(),parentExecution:e}),/CURRENT_RUN_BASELINE_REFERENCE|BASELINE_REFERENCE_PIN_CHANGED/);assert(!fs.existsSync(f.scopeFile));}
});
test('current parent subset refuses raw parent failure without starting or referencing another producer',()=>{
 for(const edit of [f=>f.parent.status='FAIL',f=>f.parent.fullPersistentAcceptance=false,f=>f.parent.passed=false]){
  const f=currentRunParentFixture();edit(f);f.refreshCurrent();assert.throws(f.verifyCurrent,/CURRENT_RUN_BASELINE_REFERENCE_PARENT/);assert(!fs.existsSync(f.scopeFile));
  assert.equal(f.parent.processes.length,1);assert.equal(f.original.passed,true);
 }
});
test('current parent subset preserves a fresh failed verification file and rejects scope collisions',()=>{
 const f=currentRunParentFixture();fs.appendFileSync(path.join(f.project,f.required),'source drift');
 assert.throws(f.verifyCurrent,/SOURCE_CHANGED/);assert(fs.existsSync(f.scopeFile));const preserved=hash(f.scopeFile);
 assert.throws(f.verifyCurrent,/EEXIST/);assert.equal(hash(f.scopeFile),preserved);
 const g=currentRunParentFixture();fs.writeFileSync(g.scopeFile,'HISTORICAL SCOPE MUST REMAIN');
 assert.throws(g.verifyCurrent,/EEXIST/);assert.equal(fs.readFileSync(g.scopeFile,'utf8'),'HISTORICAL SCOPE MUST REMAIN');
});
test('current parent subset retains original source tool environment negative and reused-producer refusals',()=>{
 const controls=[
  ['original-fail',f=>{f.original.passed=false;write(f.raw,f.original);f.rebindRaw();f.refreshCurrent();},/BASELINE_ORIGINAL_FAIL/],
  ['imported',f=>{f.parent.originalUpdateRecovery.reused=true;f.refreshCurrent();},/IMPORTED_PREVIOUS_BASELINE/],
  ['duplicate',f=>{f.parent.processes.push({...f.processProof});f.refreshCurrent();},/EXECUTION_NOT_UNIQUE/],
  ['missing-negative-source',f=>{f.processProof.boundSourceInputs=f.sources.filter(row=>row.path!==f.required);f.refreshCurrent();},/SOURCE_PROOF_MISSING/],
  ['wrong-tool-scope',f=>{f.processProof.toolArtifacts=[];f.refreshCurrent();},/TOOL_SCOPE_CHANGED/],
  ['wrong-environment',f=>{f.processProof.baselineEnvironment.YMCC_SIM_POWERSHELL='INERT-other-host';f.refreshCurrent();},/ENVIRONMENT_CHANGED/],
  ['narrowed-arguments',f=>{f.processProof.arguments.push('--positive-only');f.refreshCurrent();},/ORIGINAL_ARGUMENTS/],
 ];
 for(const [name,edit,error]of controls){const f=currentRunParentFixture();edit(f);assert.throws(f.verifyCurrent,error,name);assert(fs.existsSync(f.scopeFile),name);}
});

test('current parent subset missing structured evidence or raw uses explicit admission codes',()=>{
 const missing=currentRunParentFixture();delete missing.parentExecution.structuredEvidence;
 assert.throws(missing.verifyCurrent,/CURRENT_RUN_BASELINE_REFERENCE_STRUCTURED_EVIDENCE_REQUIRED/);assert(!fs.existsSync(missing.scopeFile));
 const empty=currentRunParentFixture();empty.parentExecution.structuredEvidence=[];const emptyOptions=empty.currentOptions();
 fs.unlinkSync(empty.parentFile);
 assert.throws(()=>verifyCurrentRunParentBaselineReference(emptyOptions),/CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_MISSING/);assert(!fs.existsSync(empty.scopeFile));
 const missingRaw=currentRunParentFixture();const missingRawOptions=missingRaw.currentOptions();fs.unlinkSync(missingRaw.parentFile);
 assert.throws(()=>verifyCurrentRunParentBaselineReference(missingRawOptions),/CURRENT_RUN_BASELINE_REFERENCE_PARENT_RESULT_MISSING/);assert(!fs.existsSync(missingRaw.scopeFile));
});
