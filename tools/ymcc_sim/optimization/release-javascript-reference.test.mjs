// INERT metadata controls only: never import or run a business producer.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {verifyParentBaselineReference,releaseJavascriptNames,releaseJavascriptEnvironmentKeys} from './release-baseline-reference.mjs';
import {baselineEnvironment} from '../global/decky-persistent-lifecycle/baseline-environment.mjs';
import {PARENT_ID} from '../global/decky-persistent-lifecycle/current-parent-global.mjs';
const root=process.env.YMCC_JAVASCRIPT_REFERENCE_UNIT_ROOT;
assert(typeof root==='string'&&path.isAbsolute(root)&&fs.statSync(root).isDirectory(),'Explicit owned INERT evidence directory required');
const repo=path.resolve(import.meta.dirname,'../../..'),names=releaseJavascriptNames(),envKeys=releaseJavascriptEnvironmentKeys();
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
// Match current-parent-global's real two-field pin schema throughout parent metadata.
const pin=file=>({path:path.resolve(file),sha256:sha(file)});
const requestPin=file=>({...pin(file),bytes:fs.statSync(file).size});
const put=(file,text)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,text);};
const write=(file,value)=>put(file,JSON.stringify(value,null,2)+'\n');
const clone=structuredClone;
const reports=['OFFICIAL-LOADER-CONSUMER.json','SAFETY-NOTIFICATION.json','validation/MIRROR-CLIENT-SELFTEST.json',null,'validation/DECKY-STABILITY-MATRIX.json'];
function fixture(){
 const own=fs.mkdtempSync(path.join(root,'inert-')),project=path.join(own,'repo'),core=path.join(own,'core'),run=path.join(core,'Evidence/run');
 const out=path.join(run,'cases',PARENT_ID,'decky-persistent-lifecycle'),parentFile=path.join(out,'result.json'),globalFile=path.join(run,'result.json'),work=path.join(core,'Workspace/parent');
 fs.mkdirSync(work,{recursive:true});fs.mkdirSync(out,{recursive:true});
 const alias='tools/decky_update_recovery_selftest.mjs',entry='tools/ymcc_sim/global/decky-update-recovery/run.mjs',negative='native/original-negative.h';
 for(const relative of [alias,entry,negative,...names.map(name=>'tools/'+name+'.mjs'),'src/override.js'])put(path.join(project,relative),relative===alias?fs.readFileSync(path.join(repo,alias)):'INERT metadata bytes; never executed.\n');
 const compilerEntry=path.join(project,'node_modules/esbuild/lib/main.js'),compilerBinary=path.join(project,'node_modules/@esbuild/win32-x64/esbuild.exe'),pkg=path.join(project,'node_modules/esbuild/package.json');
 put(compilerEntry,'INERT esbuild entry; never loaded.\n');put(compilerBinary,'INERT backend; never executed.\n');write(pkg,{name:'esbuild',main:'lib/main.js'});
 const sources=[alias,entry,negative,...names.map(name=>'tools/'+name+'.mjs'),'src/override.js'].sort().map(relative=>({path:relative,sha256:sha(path.join(project,relative))}));
 const tools=[pin(process.execPath),pin(compilerEntry),pin(compilerBinary),pin(pkg)];
 const authority={schemaVersion:1,repoRoot:project,compilerEntry,compilerBinary,tools:tools.slice(1)};
 const start=Date.parse('2026-11-04T00:00:00.000Z'),at=offset=>new Date(start+offset).toISOString();
 const stamp=file=>fs.utimesSync(file,new Date(start+2000),new Date(start+2000));
 const raw=path.join(core,'Evidence/baseline/result.json'),original={passed:true,fieldRegression:'decky-update-disconnect-20261020',nativeChecks:1,sourceInputs:sources,inputsUnchanged:true,realSteamActions:0,userSettingsWrites:0,hardwareWrites:0};write(raw,original);
 const baseId='original.plugins.decky-update-recovery',baseFile=path.join(out,'processes',baseId+'.json');
 const baseStdout=path.join(out,'processes',baseId+'.stdout.bin'),baseStderr=path.join(out,'processes',baseId+'.stderr.bin');put(baseStdout,'INERT');put(baseStderr,'');
 const baseline={id:baseId,pid:101,startedAt:at(100),finishedAt:at(200),program:tools[0],arguments:[path.join(project,entry),'--evidence-dir',path.dirname(raw)],cwd:project,originalBudgetMs:240000,timeoutMs:240000,elapsedMs:100,exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false,stdout:pin(baseStdout),stderr:pin(baseStderr),boundSourceInputs:sources,toolArtifacts:tools,baselineEnvironment:baselineEnvironment(),originalResult:pin(raw)};
 const environment=Object.fromEntries(envKeys.map(name=>[name,null]));environment.PATH='INERT_PATH';environment.YMCC_DECKY_ARTIFACT_ROOT=path.join(own,'release/Build/Validation/DeckyUpdate/scenarios');environment.YEMAN_WORKSPACE_ROOT=path.join(own,'outer-output');
 const request={schemaVersion:1,kind:'ymcc-release-javascript-reference-request',program:requestPin(process.execPath),cwd:project,environment,steps:[...names]};
 const values=[{status:'passed',cases:2,results:[{name:'ordinary',passed:true},{name:'negative rejects failed input',passed:true}]},{passed:true,cases:2,results:['ordinary','failed state is correctly rejected']},{productionClientSource:true,cases:2,results:[{name:'ordinary',status:'passed'},{name:'negative protocol',status:'passed'}]},{status:'passed',cases:['ordinary','failed command correctly rejected']},{status:'passed',cases:2,passed:2,failed:0,results:[{group:'ordinary',name:'ordinary',status:'passed'},{group:'negative',name:'rejected command',status:'passed'}]}];
 const parent={id:PARENT_ID,status:'PASS',passed:true,fullPersistentAcceptance:true,sourceUnchanged:true,toolsUnchanged:true,metadataIntegrity:true,startedAt:at(0),finishedAt:at(10000),work,sourceInputs:sources,toolArtifacts:tools,processes:[baseline],rawEvidence:[pin(raw)],cases:[{id:baseId,passed:true}],originalUpdateRecovery:{raw:pin(raw),process:baseline,sourceVerified:true,reused:false},legacyTypedMigration:{status:'INERT_METADATA_TEST_ONLY'}};
 const js=[],rawFiles=[];
 for(let index=0;index<names.length;index++){
  const name=names[index],id='original.'+name,artifact=path.join(work,name),entryFile=path.join(project,'tools',name+'.mjs');
  fs.mkdirSync(artifact,{recursive:true});
  const stdout=path.join(out,'processes',id+'.stdout.bin'),stderr=path.join(out,'processes',id+'.stderr.bin');
  if(index===3)write(stdout,values[index]);else put(stdout,'INERT output; client stdout is deliberately not JSON.\n');put(stderr,'');stamp(stdout);stamp(stderr);
  const rawFile=index===3?stdout:path.join(artifact,reports[index]);if(index!==3){write(rawFile,values[index]);stamp(rawFile);parent.rawEvidence.push(pin(rawFile));}rawFiles.push(rawFile);
  const observedEnvironment={...environment,YMCC_DECKY_ARTIFACT_ROOT:artifact,YEMAN_WORKSPACE_ROOT:path.join(own,'parent-output')};
  const before={schemaVersion:1,meaning:'INERT finite observations; never a runtime grant',environment:observedEnvironment,program:{...tools[0],parentSourceOrToolBound:false},entry:{...pin(entryFile),parentSourceOrToolBound:false},overrides:[],nodeOptionsPreloadClosureEstablished:true,nodePathResolutionClosureEstablished:true,actualSharedReferenceQualified:false};
  const p={id,pid:200+index,startedAt:at(1000),finishedAt:at(2000),program:tools[0],arguments:[entryFile,artifact],cwd:project,originalBudgetMs:60000,timeoutMs:60000,elapsedMs:1000,exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false,stdout:pin(stdout),stderr:pin(stderr),javascriptInputs:{before,after:clone(before),observedStable:true,sharedReferenceQualified:false}};
  js.push(p);parent.processes.push(p);parent.cases.push({id,passed:true,originalCases:2});
 }
 const global={startedAt:at(-100),finishedAt:at(11000),verdict:{status:'PASS_SELECTED_SCOPE',exitCode:0},results:[{id:PARENT_ID,status:'PASS',exitCode:0}]};
 function refresh(){
  for(const p of parent.processes){const file=path.join(out,'processes',p.id+'.json');write(file,p);stamp(file);}
  for(const row of parent.cases)if(row.id.startsWith('original.decky_'))row.process=pin(path.join(out,'processes',row.id+'.json'));
  write(path.join(out,'SOURCE-PINS.json'),parent.sourceInputs);write(path.join(out,'TOOL-PINS.json'),parent.toolArtifacts);
  write(path.join(out,'REGRESSION-COVERAGE.json'),{oldFailsNotPromoted:true,historical47:47});write(path.join(out,'LEGACY-TYPED-MIGRATION.json'),parent.legacyTypedMigration);
  write(path.join(out,'RAW-EVIDENCE.json'),{files:parent.rawEvidence,processes:parent.processes.map(p=>pin(path.join(out,'processes',p.id+'.json'))),rawVerdictsPreserved:true});
  write(path.join(out,'HISTORY.json'),{status:'HISTORICAL_FAIL_PRESERVED',oldSummary:{failed:1},files:[]});write(parentFile,parent);write(globalFile,global);
 }
 const options=()=>({repoRoot:project,coreRoot:core,globalResult:globalFile,globalSha256:sha(globalFile),parentResult:parentFile,parentSha256:sha(parentFile),declaration:{evidenceInputs:sources.map(row=>row.path),artifactInputs:tools.map(row=>row.path),baselineRequiredSources:[entry,negative],authority},requireJavascript:true,javascriptRequest:request});
 const rebindRaw=index=>{const file=rawFiles[index];write(file,values[index]);stamp(file);const p=pin(file);if(index===3)js[index].stdout=p;else parent.rawEvidence[parent.rawEvidence.findIndex(row=>row.path===file)]=p;};
 refresh();return {own,project,core,out,work,parent,global,js,rawFiles,values,tools,sources,request,authority,baseFile,parentFile,globalFile,refresh,options,rebindRaw,verify:()=>verifyParentBaselineReference(options())};
}
test('INERT full five references preserve string cases, output roles and false runtime flags',()=>{
 const f=fixture();
 for(const p of f.js)for(const row of [p.program,p.stdout,p.stderr,p.javascriptInputs.before.program,p.javascriptInputs.before.entry])assert.deepEqual(Object.keys(row).filter(key=>key!=='parentSourceOrToolBound').sort(),['path','sha256']);
 for(const row of [...f.parent.rawEvidence,...f.parent.cases.filter(row=>row.process).map(row=>row.process),...JSON.parse(fs.readFileSync(path.join(f.out,'RAW-EVIDENCE.json'),'utf8')).processes])assert.deepEqual(Object.keys(row).sort(),['path','sha256']);
 assert(Number.isSafeInteger(f.request.program.bytes));
 const result=f.verify();assert.equal(result.javascript.length,5);assert(Number.isFinite(result.verificationWallMs));
 assert.deepEqual(result.javascript.map(row=>row.requiredReleaseStep),names);
 for(const row of result.javascript){assert.equal(row.name,row.requiredReleaseStep);assert.equal(row.status,'SHARED_REFERENCE');for(const key of ['cached','actualPassedThisRun','executedThisRun'])assert.equal(row[key],false);assert.equal(row.originalProducerExecutionsWithinParent,1);assert.equal(row.originalProducerExecutionsByThisReference,0);assert.equal(row.originalCases,2);assert.equal(row.originalProcess.sha256,sha(row.originalProcess.path));assert.equal(row.originalResult.sha256,sha(row.originalResult.path));assert(!('verificationWallMs' in row));}
 assert.equal(typeof result.javascript[1].originalCaseResults[1],'string');assert.equal(typeof result.javascript[3].originalCaseResults[1],'string');assert.equal(result.javascript[3].originalResult.path,f.js[3].stdout.path);
 delete f.js[0].javascriptInputs;f.refresh();
 const legacy=verifyParentBaselineReference({...f.options(),requireJavascript:false,javascriptRequest:undefined});assert(!('javascript' in legacy));
});
const rejected=[
 ['missing producer',f=>f.parent.processes.pop(),/PROCESS_NOT_UNIQUE/],
 ['duplicate producer',f=>f.parent.processes.push(clone(f.js[0])),/PROCESS_NOT_UNIQUE/],
 ['duplicate request step',f=>f.request.steps[4]=f.request.steps[0],/STEPS/],
 ['different inherited environment',f=>{f.js[0].javascriptInputs.before.environment.PATH='OTHER';f.js[0].javascriptInputs.after=clone(f.js[0].javascriptInputs.before);},/ENVIRONMENT_CHANGED/],
 ['unknown NODE_OPTIONS closure',f=>{f.request.environment.NODE_OPTIONS='--require private.cjs';},/UNKNOWN_NODE_CLOSURE/],
 ['different Node executable',f=>{f.request.program=requestPin(f.authority.compilerBinary);},/FOREIGN_FILE/],
 ['changed entry source',f=>put(path.join(f.project,'tools',names[0]+'.mjs'),'CHANGED INERT SOURCE'),/SOURCE_CHANGED/],
 ['changed raw bytes',f=>fs.appendFileSync(f.rawFiles[0],' '),/EVIDENCE_FILE_CHANGED/],
 ['old parent process time',f=>{f.js[0].startedAt='2000-01-01T00:00:00.000Z';},/PROCESS_NOT_CURRENT/],
 ['different before and after',f=>{f.js[0].javascriptInputs.after.environment.PATH='OTHER';},/OBSERVATION_CHANGED/],
 ['metadata without runtime PID',f=>{f.js[0].pid=null;},/RUNTIME_PID_REQUIRED/],
 ['unbound default esbuild backend despite observed booleans',f=>{f.authority.tools=f.authority.tools.filter(row=>row.path!==f.authority.compilerBinary);},/ESBUILD_NOT_BOUND/],
 ['actual case failure under top-level PASS',f=>{f.values[4].results[0].status='failed';f.rebindRaw(4);},/CASE_NOT_PASS/],
 ['optional incorrect bytes',f=>{f.js[0].javascriptInputs.before.entry.bytes=0;f.js[0].javascriptInputs.after=clone(f.js[0].javascriptInputs.before);},/SIZE_CHANGED/],
 ['global FAIL',f=>{f.global.verdict.status='FAIL_SELECTED_SCOPE';},/GLOBAL_NOT_PASS/],
 ['partial parent',f=>{f.parent.fullPersistentAcceptance=false;},/PARENT_fullPersistentAcceptance/]
];
for(const [name,mutate,pattern] of rejected)test('INERT rejects '+name,()=>{const f=fixture();mutate(f);f.refresh();assert.throws(f.verify,pattern);});
test('INERT bound source and esbuild overrides qualify by actual pins, not observation booleans',()=>{
 const f=fixture(),overrides={YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE:path.join(f.project,'src/override.js'),ESBUILD_BINARY_PATH:f.authority.compilerBinary};Object.assign(f.request.environment,overrides);
 for(const p of f.js){const before=p.javascriptInputs.before;Object.assign(before.environment,overrides);before.overrides=Object.entries(overrides).map(([name,file])=>({name,...pin(file),parentSourceOrToolBound:false}));p.javascriptInputs.after=clone(before);}
 f.refresh();assert.equal(f.verify().javascript.length,5);
});
test('INERT independent process JSON cannot disagree with the parent row',()=>{
 const f=fixture();write(path.join(f.out,'processes',f.js[0].id+'.json'),{...f.js[0],exitCode:7});
 const file=path.join(f.out,'processes',f.js[0].id+'.json');fs.utimesSync(file,new Date(f.js[0].finishedAt),new Date(f.js[0].finishedAt));
 const metadata=path.join(f.out,'RAW-EVIDENCE.json'),value=JSON.parse(fs.readFileSync(metadata,'utf8'));value.processes[1]=pin(path.join(f.out,'processes',f.js[0].id+'.json'));write(metadata,value);
 assert.throws(f.verify,/PROCESS_FILE_MISMATCH/);
});
test('INERT release gate waits for parent and shared proof before remaining JS commands',()=>{
 const owned=fs.mkdtempSync(path.join(root,'gate-order-')),script=path.join(owned,'actual-gate-order.ps1');
 const source=String.raw`
param([Parameter(Mandatory=$true)][string]$Gate,[Parameter(Mandatory=$true)][string]$OwnedRoot)
$ErrorActionPreference='Stop'
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($Gate,[ref]$tokens,[ref]$parseErrors)
if(@($parseErrors).Count){throw 'GATE_PARSE_REQUIRED'}
$blocks=@($ast.FindAll({param($n) $n -is [System.Management.Automation.Language.TryStatementAst] -and $n.Body.Statements.Count -gt 0 -and $n.Body.Statements[0] -is [System.Management.Automation.Language.AssignmentStatementAst] -and $n.Body.Statements[0].Left.Extent.Text -ceq '$env:YMCC_DECKY_ARTIFACT_ROOT'},$true))
if($blocks.Count -ne 1){throw 'ACTUAL_RELEASE_JS_BLOCK_NOT_UNIQUE'}
$getter=@($ast.FindAll({param($n)$n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Get-DeckyJavascriptReferenceSteps'},$true))
if($getter.Count -ne 1){throw 'ACTUAL_SHARED_REQUIREMENT_NAMES_REQUIRED'}
. ([scriptblock]::Create($getter[0].Extent.Text))
$body=[scriptblock]::Create($blocks[0].Extent.Text.Replace('$PSScriptRoot','$GateScriptRoot'))
$originalNames=@('decky_ymcc_update_selftest','decky_sidebar_safety_owner_selftest','decky_sidebar_health_dom_selftest','decky_sidebar_bootstrap_expression_selftest','decky_updater_service_selftest','decky_updater_ui_selftest')
$rows=[Collections.Generic.List[object]]::new()
foreach($fault in @('none','parent','reference','outer')){
 $GateScriptRoot=Split-Path -Parent $Gate;$gateRoot=Join-Path $OwnedRoot 'INERT-NO-BUSINESS-OUTPUT';$priorArtifactRoot='INERT-PREEXISTING-ROOT';$env:YMCC_DECKY_ARTIFACT_ROOT=$priorArtifactRoot
 $events=[Collections.Generic.List[string]]::new();$commands=[Collections.Generic.List[object]]::new();$steps=[Collections.Generic.List[hashtable]]::new();$script:recoveryRequirement=$null;$script:javascriptRequirements=@{}
 function New-DeckyJavascriptReferenceRequest {return @{scope='INERT_REQUEST_ONLY'}}
 function Invoke-DeckyLifecycleRequired {
  $events.Add('parent');if($fault -eq 'parent'){throw 'INERT_PARENT_FAILURE'}
  $steps.Add(@{name='decky-persistent-lifecycle';passed=$true;inert=$true})
 }
 function Complete-DeckyRecoverySharedRequirement($Step){
  $events.Add('reference')
  if($steps.Count -ne 1 -or -not [object]::ReferenceEquals($Step,$steps[0]) -or $script:recoveryRequirement.status -cne 'NOT_RUN_DEFERRED_TO_PARENT' -or $script:javascriptRequirements.Count -ne 5){throw 'INERT_ORIGINAL_PARENT_REFERENCE_BINDING_REQUIRED'}
  if($fault -eq 'reference'){throw 'INERT_REFERENCE_FAILURE'}
  $script:recoveryRequirement.status='SHARED_REFERENCE';foreach($name in @(Get-DeckyJavascriptReferenceSteps)){$script:javascriptRequirements[$name].status='SHARED_REFERENCE'}
 }
 function Invoke-Required([string]$Name,[string]$Program,[string[]]$Arguments){
  $events.Add('js:'+$Name);$commands.Add(@{name=$Name;program=$Program;arguments=$Arguments})
  if($fault -eq 'outer'){throw 'INERT_OUTER_FAILURE'}
 }
 $errorText=$null
 try{. $body}catch{$errorText=$_.Exception.Message}
 $expected=@('parent','reference')+@($originalNames|ForEach-Object{'js:'+$_})
 if($fault -eq 'parent'){$expected=@('parent')}
 elseif($fault -eq 'reference'){$expected=@('parent','reference')}
 elseif($fault -eq 'outer'){$expected=@('parent','reference',('js:'+$originalNames[0]))}
 if(($events.ToArray() -join '|') -cne ($expected -join '|')){throw ('INERT_PARENT_BEFORE_REMAINING_JAVASCRIPT_REQUIRED '+$fault+' observed='+($events.ToArray() -join '|'))}
 $expectedError=@{none=$null;parent='INERT_PARENT_FAILURE';reference='INERT_REFERENCE_FAILURE';outer='INERT_OUTER_FAILURE'}[$fault]
 if($errorText -cne $expectedError){throw ('INERT_ORIGINAL_FAILURE_PRESERVED '+$fault+' '+$errorText)}
 if($env:YMCC_DECKY_ARTIFACT_ROOT -cne $priorArtifactRoot){throw 'INERT_ENVIRONMENT_NOT_RESTORED'}
 foreach($command in $commands){if($command.program -cne 'node' -or $command.arguments.Count -ne 2 -or $command.arguments[0] -cne (Join-Path $GateScriptRoot ($command.name+'.mjs')) -or $command.arguments[1] -cne $gateRoot){throw 'INERT_ORIGINAL_COMMAND_CHANGED'}}
 $deferred=if($fault -in @('parent','reference')){'NOT_RUN_DEFERRED_TO_PARENT'}else{'SHARED_REFERENCE'}
 if($script:recoveryRequirement.status -cne $deferred -or @($script:javascriptRequirements.Values|Where-Object{$_.status -cne $deferred}).Count -ne 0){throw 'INERT_DEFERRED_VERDICT_CHANGED'}
 $rows.Add(@{name=$fault;passed=$true;events=@($events.ToArray());inertCallbacks=$commands.Count;expectedError=$expectedError;environmentRestored=$true})
}
@{schemaVersion=1;scope='ACTUAL_GATE_AST_WITH_INERT_CALLBACKS_ONLY';rows=@($rows.ToArray());actualBusinessRuns=0;actualParentRuns=0;actualFullRuns=0;newRequiredIds=0}|ConvertTo-Json -Depth 10 -Compress
`;
 put(script,source);
 const pwsh=process.env.YMCC_SIM_RAW_PROGRAM_PWSH||'pwsh.exe',run=spawnSync(pwsh,['-NoLogo','-NoProfile','-NonInteractive','-File',script,'-Gate',path.join(repo,'tools/decky_update_release_gate.ps1'),'-OwnedRoot',owned],{windowsHide:true,timeout:4000,maxBuffer:1024*1024});
 put(path.join(owned,'stdout.txt'),run.stdout??Buffer.alloc(0));put(path.join(owned,'stderr.txt'),run.stderr??Buffer.alloc(0));
 assert.equal(run.status,0,'ACTUAL_GATE_ORDER_CONTROL_FAILED '+String(run.error??'')+' '+String(run.stderr??''));
 const result=JSON.parse(run.stdout.toString('utf8').trim());assert.deepEqual(result.rows.map(row=>row.name),['none','parent','reference','outer']);assert(result.rows.every(row=>row.passed===true));assert.equal(result.actualBusinessRuns,0);assert.equal(result.actualParentRuns,0);assert.equal(result.actualFullRuns,0);
 console.log('YMCC_RELEASE_PARENT_FIRST_CONTROL_RAW '+JSON.stringify(result));
});
