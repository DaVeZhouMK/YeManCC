import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual as same} from 'node:util';
import {legacyManagedContract,legacyManagedArtifactRows,legacyManagedToolFiles,managedExpectedUnit} from './managed-terminal-receipts.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const schema=()=>read(new URL('./consumer-schema.json',import.meta.url));
const sha=b=>createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p));
const norm=p=>path.resolve(p).toLowerCase(),need=(ok,code)=>assert.ok(ok,code);
const pin=p=>({path:p,sha256:hash(p),bytes:fs.statSync(p).size});
const files=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>e.isDirectory()?files(path.join(root,e.name)):[path.join(root,e.name)]).sort();
function below(p,root,code){need(typeof p==='string'&&path.isAbsolute(p),code);const r=path.relative(root,p);need(r&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep),code);}
function fixed(p,code){need(p&&typeof p.path==='string'&&hash(p.path)===p.sha256,code);}
const mask=rt=>({passMask:rt.cases.map(c=>c.pass?'1':'0').join(''),failedCases:rt.cases.filter(c=>!c.pass).map(c=>c.name),errorPrefixes:Object.fromEntries(rt.cases.filter(c=>!c.pass).map(c=>[c.name,c.error?.split(/\r?\n/)[0]]))});
const oracle=row=>Object.fromEntries(['name','pass','exit','receipts','terminalReceipts','beforeFixtureCleanup','costDisposals','costAfterSettle','stageCreates','stageDisposes','stageAfterSettle'].filter(k=>Object.hasOwn(row,k)).map(k=>[k,row[k]]));
function fixedRows(c){return [...(c.qualifiedAggregate?[c.qualifiedAggregate]:[]),c.identityManifest,c.basisCurrent,c.basisExtractions,c.actualFiveNegatives,...Object.values(c.generatedUnits),...c.negativeContracts.map(n=>n.receipt),c.initializationBoundary.receipt,c.initializationBoundary.extractions,c.initializationBoundary.oldMainSource,...Object.values(c.initializationBoundary.generatedUnits)];}
// This reads retained execution evidence, not approval of the current control code.
// Its controlSources describe the original run and must never be repinned here.
export function validateManagedV2IndependentExecution(execution,c){
 const expected=Object.fromEntries(c.deliveredSources.map(row=>[row.path,row.sha256]));
 need(execution?.schemaVersion===1&&execution.version===2&&execution.status==='PASS'&&execution.pass===true&&execution.scope==='independent-managed-terminal22-v2-only'&&execution.parentLifecycleAcceptance==='INCOMPLETE','MANAGED_V2_INDEPENDENT_SCOPE');
 need(same(execution.sourceHashes,expected),'MANAGED_V2_INDEPENDENT_SOURCE_IDENTITY');
 const v=execution.validation;
 need(v?.id==='actual-current-managed-terminal-v2'&&v.pass===true&&v.version===2&&v.parentLifecycleAcceptance==='INCOMPLETE'&&v.wholeLifecycleAcceptance===false&&v.processExitUsedAsCleanup===false&&v.currentLegacyParentCancellationAccepted===false,'MANAGED_V2_INDEPENDENT_VALIDATION_SCOPE');
 need(v.profiles===c.profiles.length&&v.currentCasesPerProfile===c.casesPerProfile&&v.currentChecks===c.profiles.length*c.casesPerProfile&&v.specifiedNegativeBuilds===c.negativeContracts.length*c.profiles.length,'MANAGED_V2_INDEPENDENT_VALIDATION_COUNTS');
 return execution;
}
export function managedV2IndependentResult(c){
 const file=c.actualIndependentResult;
 below(file,c.evidenceRoot,'MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION');
 need(path.basename(file)==='EXECUTION.json','MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION');
 need(fs.existsSync(file)&&fs.statSync(file).isFile(),'MANAGED_V2_INDEPENDENT_REFERENCE_MISSING');
 const execution=validateManagedV2IndependentExecution(read(file),c),receipt=execution.receipt;
 below(receipt?.path,path.dirname(file),'MANAGED_V2_INDEPENDENT_RECEIPT_LOCATION');
 need(norm(receipt.path)===norm(path.join(path.dirname(file),c.child)),'MANAGED_V2_INDEPENDENT_RECEIPT_LOCATION');
 fixed(receipt,'MANAGED_V2_INDEPENDENT_RECEIPT_PIN');
 const aggregate=read(receipt.path);
 need(aggregate.schemaVersion===1&&aggregate.version===2&&aggregate.status==='PASS'&&aggregate.pass===true&&aggregate.parentLifecycleAcceptance==='INCOMPLETE'&&aggregate.wholeLifecycleAcceptance===false&&aggregate.productionMainExecuted===false&&aggregate.processExitUsedAsCleanup===false,'MANAGED_V2_INDEPENDENT_AGGREGATE_SCOPE');
 need(same(aggregate.sourceHashes,execution.sourceHashes)&&same(aggregate.identity,c.identityManifest),'MANAGED_V2_INDEPENDENT_AGGREGATE_IDENTITY');
 return execution;
}
export function managedV2Contract(repo){
 const s=schema(),c=s.managedTerminalCurrentV2,old=s.managedTerminalCurrent;
 need(c?.groupVersion===2&&c.schemaVersion===1&&c.casesPerProfile===42&&c.sourceInputs.length===39&&c.caseNames.length===42&&new Set(c.caseNames).size===42&&c.registeredSignalGroup===true&&c.registrationState==='REGISTERED_SIGNAL_CHILD_JOB_VALIDATOR_V2'&&c.parentLifecycleAcceptance==='INCOMPLETE'&&c.wholeLifecycleAcceptance===false&&c.processExitUsedAsCleanup===false&&c.currentLegacyParentCancellationAccepted===false,'MANAGED_V2_CONTRACT');
 need(sha(JSON.stringify(old))===c.legacyContractSha256&&old.groupVersion===1&&old.casesPerProfile===40&&same(c.sourceInputs,old.sourceInputs)&&same(c.faults,old.faults)&&same(c.profiles,old.profiles)&&same(c.caseNames.filter(n=>!n.startsWith('terminal-initialization-')),old.caseNames),'MANAGED_V2_PRESERVED_V1');
 managedV2IndependentResult(c);
 // v1 keeps its own 40-case source/identity/generated-unit authority. Its old
 // current bytes are checked at the immutable source snapshot, never repinned.
 legacyManagedContract(repo,{historical:true});
 for(const p of fixedRows(c))fixed(p,'MANAGED_V2_FIXED_EVIDENCE_PIN');
 const identity=read(c.identityManifest.path),expected=Object.fromEntries(c.deliveredSources.map(p=>[p.path,p.sha256]));
 need(identity.groupVersion===2&&identity.sourceIdentityVersion===1&&identity.parentMainApproval===false&&identity.productionMainExecuted===false&&same(identity.sourceBindings,expected)&&same(Object.keys(expected),c.sourceInputs),'MANAGED_V2_SOURCE_IDENTITY');
 for(const p of c.deliveredSources)need(hash(path.join(repo,p.path))===p.sha256,'MANAGED_V2_CURRENT_SOURCE_CHANGED:'+p.path);
 need(c.dotnetToolchain===old.dotnetToolchain&&c.dotnetToolchainSha256===old.dotnetToolchainSha256,'MANAGED_V2_TOOL_AUTHORITY');
 const {rows}=legacyManagedToolFiles(repo,c),acquired=read(c.actualFiveNegatives.path);
 need(acquired.status==='PASS_ACTUAL_EXECUTIONS_EXPECTED_REJECTIONS'&&acquired.sourceStable===true&&acquired.jobs.length===5&&same(acquired.sourceHashes,expected)&&same(acquired.identity,c.identityManifest)&&same(acquired.toolsBefore,rows)&&same(acquired.toolsAfter,rows),'MANAGED_V2_ACTUAL_FIVE_EXECUTIONS');
 for(const n of c.negativeContracts){const previous=old.negativeContracts.find(x=>x.fault===n.fault),job=acquired.jobs.find(j=>j.fault===n.fault);need(previous&&n.requiredFailedCase===previous.requiredFailedCase&&n.requiredErrorPrefix===previous.requiredErrorPrefix&&same(n.legacyReceipt,previous.receipt)&&same(job?.receipt,n.receipt),'MANAGED_V2_ORIGINAL_NEGATIVE_CONTRACT');}
 validateManagedV2Initialization(read(c.initializationBoundary.receipt.path),c);
 return c;
}
export function managedV2ArtifactRows(repo){const c=managedV2Contract(repo);return [...legacyManagedArtifactRows(repo,{historical:true}),...fixedRows(c).map(p=>({...p,origin:'managed-terminal.v2-fixed-evidence'}))];}
function scope(r,status,fault){need(r.sourceStable===true&&r.runtimeSourceClosureStable===true&&r.status===status&&r.fault===fault&&r.eligibilityOnly===false&&r.resourceAcceptance==='BOUNDED_TERMINAL_RESULT_ONLY_OS_FALLBACK_UNKNOWN'&&r.productionMainExecuted===false&&r.productOrInstalledProcessesTouched===false&&r.ownedFixtureChildProcessesOnly===true&&r.realDriverIo===false,'MANAGED_V2_BOUNDARY_OR_STATUS');}
function profile(rt,c){need(rt?.cases?.length===42&&same(rt.cases.map(x=>x.name),c.caseNames)&&rt.cases.every(x=>typeof x.pass==='boolean'&&(x.pass?x.error===null:typeof x.error==='string')),'MANAGED_V2_ACTUAL_42_CASES');need(rt.productionMainExecuted===false&&rt.actualMainTerminalBlockExecuted===true&&rt.realSdkAssemblyLoaded===false&&rt.realDriverIo===false&&rt.eligibilityOnly===false,'MANAGED_V2_TERMINAL_SCOPE');}
export function validateManagedV2Profiles(r,c,fault){
 const negative=fault!=='none',required=c.negativeContracts.find(n=>n.fault===fault);need(c.groupVersion===2&&c.faults.includes(fault),'MANAGED_V2_PROFILE_VERSION');scope(r,negative?'FAIL':'PASS',fault);need(r.profiles?.length===2,'MANAGED_V2_TWO_CONFIGURATIONS');
 const basis=read(negative?required.receipt.path:c.basisCurrent.path);
 for(const config of c.profiles){const matches=r.profiles.filter(p=>p.config===config);need(matches.length===1,'MANAGED_V2_CONFIG_MATRIX');const p=matches[0],rt=p.result;profile(rt,c);need(p.exit===(negative?1:0)&&rt.passed===!negative,'MANAGED_V2_PROFILE_EXIT');
  if(negative){const hit=rt.cases.find(x=>x.name===required.requiredFailedCase);need(hit?.pass===false&&hit.error.startsWith(required.requiredErrorPrefix),'MANAGED_SPECIFIED_SEMANTIC_NEGATIVE:'+fault);need(same(mask(rt),required.profiles[config]),'MANAGED_V2_EXACT_NEGATIVE_MASK:'+fault);}
  else need(rt.cases.every(x=>x.pass===true),'MANAGED_V2_CURRENT_CASE_FAILED');
  const approved=basis.profiles.find(p=>p.config===config).result;
  for(const [i,row]of rt.cases.entries()){
   need(same(oracle(row),oracle(approved.cases[i])),'MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT:'+row.name);
   const resources=row.beforeFixtureCleanup;need(resources&&Array.isArray(resources.workers)&&resources.UnsafeFakeUnmaps===0&&resources.UnsafeNeutralWrites===0,'MANAGED_V2_UNSAFE_DESTRUCTION');
   if(!negative){for(const receipt of [...row.receipts??[],...row.terminalReceipts??[]])need(receipt.ManagedReleaseComplete||receipt.ExitCode!==0,'MANAGED_V2_ZERO_FALSE_CANNOT_PASS');for(const log of row.actualProductionLogRows??[])if(log.reason.startsWith('terminal-cleanup-'))need(log.kind==='resource-settlement'&&same(JSON.parse(log.detail),(row.receipts??[]).find(x=>x.ManagedReleaseComplete===(log.reason==='terminal-cleanup-complete'))),'MANAGED_V2_ACTUAL_LOGGER_RECEIPT');}
  }
  const healthy=rt.cases.find(x=>x.name==='terminal-initialization-healthy'),failed=rt.cases.find(x=>x.name==='terminal-initialization-throws');
  need(healthy.pass&&failed.pass&&healthy.exit===0&&failed.exit===4&&healthy.costDisposals===1&&failed.costDisposals===1&&healthy.costAfterSettle===true&&failed.costAfterSettle===true&&healthy.stageDisposes===1&&failed.stageDisposes===0&&healthy.stageAfterSettle===true,'MANAGED_V2_INITIALIZATION_LIFETIMES');
  need(failed.receipts.length===1&&failed.receipts[0].SourceExitCode===4&&failed.receipts[0].Reason==='fatal-exception'&&failed.receipts[0].ReleaseAttempts===1&&failed.receipts[0].DirectReleaseAttempts===1&&failed.receipts[0].ManagedReleaseComplete===true&&failed.beforeFixtureCleanup.ContextCalls===1&&failed.beforeFixtureCleanup.workers.every(w=>!w.outputAlive&&!w.clockAlive&&w.outputCtsClosed&&w.clockEventClosed&&w.callbacks===0),'MANAGED_V2_INITIALIZATION_EXACT_SETTLEMENT');
 }
}
export function validateManagedV2Initialization(r,c){
 scope(r,'FAIL','none');need(r.profiles?.length===2,'MANAGED_V2_INIT_ORIGINAL_CONFIGS');
 for(const config of c.profiles){const matches=r.profiles.filter(p=>p.config===config);need(matches.length===1,'MANAGED_V2_INIT_ORIGINAL_CONFIGS');const p=matches[0];profile(p.result,c);need(p.exit===1&&p.result.passed===false&&same(mask(p.result),c.initializationBoundary.profiles[config])&&p.result.cases.filter(x=>x.pass).length===41,'MANAGED_V2_INIT_ORIGINAL_MASK');const bad=p.result.cases.find(x=>x.name===c.initializationBoundary.requiredFailedCase);need(bad.pass===false&&bad.error.startsWith('System.IO.IOException: injected-host-initialization')&&bad.receipts.length===0&&bad.costAfterSettle===false&&bad.beforeFixtureCleanup.ContextCalls===0&&bad.beforeFixtureCleanup.workers.some(w=>w.outputAlive&&w.clockAlive&&!w.outputCtsClosed&&!w.clockEventClosed&&w.callbacks===1),'MANAGED_V2_INIT_ORIGINAL_BYPASSED_SETTLE');}
}
function inputSnapshots(r,c,repo,output,{original=false}={}){
 const seen=new Set();need(r.inputs?.length===38,'MANAGED_V2_38_SOURCE_SNAPSHOTS');const bindings=Object.fromEntries(c.deliveredSources.map(p=>[p.path,p.sha256]));
 for(const row of r.inputs){const name=row.relative.replaceAll('\\','/'),expected=original&&name==='InputHost/Program.cs'?c.initializationBoundary.oldMainSource.sha256:bindings[name];need(!seen.has(name)&&c.sourceInputs.includes(name)&&row.sha256===expected&&hash(row.snapshot)===expected&&norm(row.snapshot)===norm(path.join(output,'sources',name))&&norm(row.path)===norm(path.join(repo,name))&&(original&&name==='InputHost/Program.cs'||hash(row.path)===expected),'MANAGED_V2_ACTUAL_SOURCE_SNAPSHOT:'+name);seen.add(name);}
 need(c.sourceInputs.every(n=>n==='deps/hidmaestro/HIDMaestro.Core.dll'||seen.has(n)),'MANAGED_V2_ACTUAL_SOURCE_CLOSURE');
 need(r.sdk.loaded===false&&norm(r.sdk.path)===norm(path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll'))&&r.sdk.sha256===bindings['deps/hidmaestro/HIDMaestro.Core.dll']&&hash(r.sdk.path)===r.sdk.sha256,'MANAGED_V2_PINNED_SDK_NOT_LOADED');
}
export function validateManagedV2Artifacts(r,c,fault,output,repo,context,{original=false}={}){
 const {spec,rows:dotnet}=legacyManagedToolFiles(repo,c),dotnetExe=path.join(spec.dotnetRoot,'dotnet.exe');inputSnapshots(r,c,repo,output,{original});
 need(same(r.toolchain,dotnet.map(p=>({path:p.path,expected:p.sha256,actual:p.sha256})))&&same(read(path.join(output,'toolchain.json')),r.toolchain)&&same(read(path.join(output,'inputs-before.json')),r.inputs),'MANAGED_V2_COMPLETE_PRODUCER_TOOL_INPUT_LOGS');
 const units=original?c.initializationBoundary.generatedUnits:c.generatedUnits,actualUnits=files(path.join(output,'integrated')).filter(p=>path.dirname(p)===path.join(output,'integrated')).map(p=>path.basename(p)).sort();
 need(same(actualUnits,Object.keys(units).sort()),'MANAGED_V2_COMPLETE_GENERATED_UNITS');
 for(const [name,p]of Object.entries(units)){fixed(p,'MANAGED_V2_GENERATED_BASIS_PIN');const expected=original?fs.readFileSync(p.path,'utf8'):managedExpectedUnit(c,name,fault);need(fs.readFileSync(path.join(output,'integrated',name),'utf8')===expected,'MANAGED_V2_WHOLE_ACTUAL_GENERATED_UNIT:'+name);}
 const extra=read(path.join(output,'consumer-extractions.json')),basisFile=(original?c.initializationBoundary.extractions:c.basisExtractions).path,basis=read(basisFile);
 // Only source snapshot location changes between executions; keep every other
 // extraction field, including hashes, offsets, substitutions and Main text.
 for(const row of basis.generated){below(row.file,path.join(path.dirname(basisFile),'sources'),'MANAGED_V2_EXTRACTION_SOURCE_LOCATION');row.file=path.join(output,path.relative(path.dirname(basisFile),row.file));}
 need(same(extra,basis)&&extra.eligibilityOnly===false,'MANAGED_V2_COMPLETE_ACTUAL_EXTRACTIONS');
 const commands=read(path.join(output,'commands.json'));need(commands.length===4,'MANAGED_V2_FOUR_BUILD_RUN_COMMANDS');
 for(const config of c.profiles)for(const role of ['build','tests']){const matches=commands.filter(p=>p.label===config+'-'+role);need(matches.length===1,'MANAGED_V2_BUILD_RUN_MATRIX');const command=matches[0],argv=role==='build'?['build','Fixture.csproj','-c',config,'--nologo','-v:q']:[path.join(output,'integrated','bin',config,'net10.0','Fixture.dll')];need(norm(command.exe)===norm(dotnetExe)&&same(command.argv,argv)&&norm(command.cwd)===norm(path.join(output,'integrated'))&&command.status===(role==='tests'&&(original||fault!=='none')?1:0)&&command.signal===null&&command.error===null,'MANAGED_V2_ACTUAL_BUILD_RUN_COMMAND');need(fs.statSync(path.join(output,config+'-'+role+'.log')).isFile(),'MANAGED_V2_BUILD_RUN_LOG');if(role==='tests'){need(fs.statSync(argv[0]).isFile(),'MANAGED_V2_ACTUAL_EXECUTED_ASSEMBLY');const lines=fs.readFileSync(path.join(output,config+'-tests.log'),'utf8').trim().split(/\r?\n/);need(same(JSON.parse(lines.at(-1)),r.profiles.find(p=>p.config===config).result),'MANAGED_V2_ACTUAL_RUNTIME_LOG');}}
 if(context){for(const [name,p]of Object.entries(context.entryArtifacts)){need(norm(p.path)===norm(path.join(output,name)),'MANAGED_V2_ENTRY_ARTIFACT_LOCATION');fixed(p,'MANAGED_V2_ENTRY_ARTIFACT_PIN');}const names=['inputs-before.json','consumer-extractions.json','toolchain.json','commands.json','Release-build.log','Release-tests.log','Debug-build.log','Debug-tests.log',...(r.registrationBinding?['registration-binding-before.json']:[])];need(same(Object.keys(context.entryArtifacts).sort(),names.sort()),'MANAGED_V2_COMPLETE_ENTRY_ARTIFACTS');const tree=files(path.join(output,'integrated'));need(context.integratedArtifacts?.length===tree.length&&new Set(context.integratedArtifacts.map(p=>norm(p.path))).size===tree.length&&tree.every(file=>context.integratedArtifacts.some(p=>norm(p.path)===norm(file)&&p.sha256===hash(file)&&p.bytes===fs.statSync(file).size)),'MANAGED_V2_COMPLETE_GENERATED_AND_EXECUTED_ARTIFACTS');}
}
function toolsCheck(rows,expected,tools){need(rows?.length===Object.keys(tools.primary).length,'MANAGED_V2_PRIMARY_TOOLS');for(const p of rows)need(norm(p.path)===norm(tools.primary[p.role])&&hash(p.path)===p.sha256&&tools.files.some(t=>norm(t.path)===norm(p.path)&&t.sha256===p.sha256),'MANAGED_V2_PRIMARY_TOOL_PIN');}
function checkBoundEntry(context,r,c,fault,output,sourceHashes,repo,tools){
 const entry=context.entry,owned=context.ownedWorkspace,closure=context.runtimeSourceClosure;
 need(context.kind==='controller-signal-managed-terminal-v2'&&context.fault===fault&&same(context.sourceHashes,sourceHashes)&&same(context.identity,c.identityManifest)&&norm(owned.buildOutput)===norm(output)&&owned.id==='managed-terminal-'+fault&&owned.outputPathLength<=110&&owned.tempPathLength<=110,'MANAGED_V2_OWNED_CONTEXT');
 const allowed=[path.resolve(repo,'../../../Testing/YMCC/Evidence/VC-CaptureReceipt23/t'),path.join(c.evidenceRoot,'t')];need(allowed.some(root=>{const relative=path.relative(root,output);return relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);}),'MANAGED_V2_SHORT_OWNED_PATH');
 for(const p of [owned.owner,owned.jobIdentity,closure])fixed(p,'MANAGED_V2_OWNED_PIN');need(same(read(owned.owner.path).sourceHashes,sourceHashes)&&norm(read(owned.owner.path).repository)===norm(repo)&&read(owned.jobIdentity.path).id===owned.id&&same(read(closure.path),sourceHashes)&&norm(closure.path)===norm(path.join(owned.directory,'runtime-source-closure.json')),'MANAGED_V2_OWNER_SOURCE_BOUND');
 const binding=r.registrationBinding;need(binding?.identityMode==='explicit-managed-source-identity-and-runtime-closure'&&same(binding.approvedIdentityManifest,c.identityManifest)&&same(binding.runtimeSourceClosure,sourceHashes)&&binding.runtimeSourceClosureIdentity.sha256===closure.sha256&&norm(binding.runtimeSourceClosureIdentity.path)===norm(closure.path)&&same(read(path.join(output,'registration-binding-before.json')),binding),'MANAGED_V2_PRODUCER_BINDING');
 const args=[path.join(repo,c.consumer),'--out',output,'--fault',fault,'--runtime-source-closure',closure.path,'--source-identity-manifest',c.identityManifest.path,'--source-identity-sha256',c.identityManifest.sha256];need(norm(entry.executable)===norm(tools.primary.node)&&same(entry.args,args)&&entry.exitCode===(fault==='none'?0:1)&&entry.signal===null&&entry.error===null&&!entry.timedOut,'MANAGED_V2_ACTUAL_BOUND_INVOCATION');for(const phase of ['primaryToolsBefore','primaryToolsAfter'])toolsCheck(entry[phase],null,tools);
}
function checkQualifiedEntry(context,r,c,fault,output,repo,tools){
 need(context.kind==='controller-signal-managed-terminal-v2-qualified-reference'&&context.fault===fault&&same(context.identity,c.identityManifest),'MANAGED_V2_QUALIFIED_REFERENCE_CONTEXT');
 if(fault==='none'){need(norm(context.producerReceipt.path)===norm(c.basisCurrent.path)&&hash(context.producerReceipt.path)===c.basisCurrent.sha256&&r.registrationBinding===null&&context.referenceReason==='Previously executed patched-001; producer did not claim an approval binding','MANAGED_V2_ORIGINAL_POSITIVE_REFERENCE');return;}
 const acquired=read(c.actualFiveNegatives.path),job=acquired.jobs.find(p=>p.fault===fault);need(job&&same(context.actualEntry,job)&&same(context.acquisition,c.actualFiveNegatives)&&norm(job.receipt.path)===norm(path.join(output,'result.json'))&&hash(job.receipt.path)===job.receipt.sha256&&norm(job.executable)===norm(tools.primary.node)&&job.nodeSha256===hash(job.executable)&&job.exitCode===1&&job.signal===null&&job.error===null&&!job.timedOut&&job.startedAt&&job.finishedAt,'MANAGED_V2_ACTUAL_QUALIFIED_ENTRY');
 const args=[path.join(repo,c.consumer),'--out',output,'--fault',fault,'--runtime-source-closure',path.join(c.evidenceRoot,'runtime-source-closure.json'),'--source-identity-manifest',c.identityManifest.path,'--source-identity-sha256',c.identityManifest.sha256];const canonical=argv=>argv.map((p,i)=>[0,2,6,8].includes(i)?norm(p):p);need(same(canonical(job.args),canonical(args)),'MANAGED_V2_QUALIFIED_ENTRY_ARGS');
 const bindings=Object.fromEntries(c.deliveredSources.map(p=>[p.path,p.sha256])),b=r.registrationBinding;need(b?.identityMode==='explicit-managed-source-identity-and-runtime-closure'&&same(b.runtimeSourceClosure,bindings)&&b.approvedIdentityManifest.sha256===c.identityManifest.sha256&&norm(b.approvedIdentityManifest.path)===norm(c.identityManifest.path)&&hash(b.runtimeSourceClosureIdentity.path)===b.runtimeSourceClosureIdentity.sha256&&same(read(b.runtimeSourceClosureIdentity.path),bindings)&&same(read(path.join(output,'registration-binding-before.json')),b),'MANAGED_V2_QUALIFIED_PRODUCER_BINDING');
}
export function validateManagedV2Terminal(r,{repo,sourceHashes,tools}){
 const c=managedV2Contract(repo),{rows:dotnet}=legacyManagedToolFiles(repo,c);
 need(r.status==='PASS'&&r.pass===true&&r.version===2&&r.parentLifecycleAcceptance==='INCOMPLETE'&&r.wholeLifecycleAcceptance===false&&r.productionMainExecuted===false&&r.processExitUsedAsCleanup===false&&same(r.sourceHashes,sourceHashes)&&same(r.identity,c.identityManifest),'MANAGED_V2_AGGREGATE_SCOPE');
 for(const p of c.deliveredSources)need(sourceHashes[p.path]===p.sha256,'MANAGED_V2_AGGREGATE_SOURCE_IDENTITY');need(r.runs?.length===6&&new Set(r.runs.map(x=>x.fault)).size===6&&c.faults.every(f=>r.runs.some(x=>x.fault===f)),'MANAGED_V2_SIX_ROUTE_MATRIX');
 for(const phase of ['toolsBefore','toolsAfter'])need(same(r[phase],dotnet),'MANAGED_V2_COMPLETE_DOTNET_SEALS');for(const p of dotnet)fixed(p,'MANAGED_V2_DOTNET_ACTUAL_PIN');
 for(const run of r.runs){need(run.accepted===true,'MANAGED_V2_ACTUAL_RUN_ACCEPTED');fixed(run.receipt,'MANAGED_V2_ACTUAL_RUN_PIN');fixed(run.context,'MANAGED_V2_ACTUAL_CONTEXT_PIN');const receipt=read(run.receipt.path),context=read(run.context.path),output=path.dirname(run.receipt.path);validateManagedV2Profiles(receipt,c,run.fault);if(context.kind==='controller-signal-managed-terminal-v2-qualified-reference')checkQualifiedEntry(context,receipt,c,run.fault,output,repo,tools);else checkBoundEntry(context,receipt,c,run.fault,output,sourceHashes,repo,tools);validateManagedV2Artifacts(receipt,c,run.fault,output,repo,context);}
 // Original 41/42 is a retained regression witness, never a current PASS route.
 const original=read(c.initializationBoundary.receipt.path);validateManagedV2Artifacts(original,c,'none',path.dirname(c.initializationBoundary.receipt.path),repo,null,{original:true});
 return {id:'actual-current-managed-terminal-v2',pass:true,version:2,profiles:2,currentCasesPerProfile:42,currentChecks:84,specifiedNegativeBuilds:10,exactNegativeFailureCounts:c.negativeContracts.map(n=>({fault:n.fault,perProfile:n.profiles.Release.failedCases.length})),initializationOriginalCasesPerProfile:42,initializationOriginalPassedPerProfile:41,initializationOriginalRejectedBuilds:2,legacyCasesPerProfile:19,retainedOldZeroFalseFailures:4,currentLegacyParentCancellationAccepted:false,processExitUsedAsCleanup:false,privateOwnedFixtureCleanup:'Actual beforeFixtureCleanup resource/typed receipts checked; retained owner debt remains nonzero. OS fallback is UNKNOWN.',wholeLifecycleAcceptance:false,parentLifecycleAcceptance:'INCOMPLETE'};
}
export function qualifyManagedV2({repo,out,tools}){
 const c=managedV2Contract(repo),sourceHashes=Object.fromEntries(c.deliveredSources.map(p=>[p.path,p.sha256])),{rows:dotnet}=legacyManagedToolFiles(repo,c),acquisition=read(c.actualFiveNegatives.path);
 fs.mkdirSync(out,{recursive:true});const runs=[];
 for(const fault of c.faults){const producerReceipt=fault==='none'?c.basisCurrent:c.negativeContracts.find(p=>p.fault===fault).receipt,output=path.dirname(producerReceipt.path),receipt=read(producerReceipt.path);validateManagedV2Profiles(receipt,c,fault);
  const entryNames=['inputs-before.json','consumer-extractions.json','toolchain.json','commands.json','Release-build.log','Release-tests.log','Debug-build.log','Debug-tests.log',...(receipt.registrationBinding?['registration-binding-before.json']:[])];
  const context={schemaVersion:1,kind:'controller-signal-managed-terminal-v2-qualified-reference',fault,identity:c.identityManifest,sourceHashes,producerReceipt,...(fault==='none'?{referenceReason:'Previously executed patched-001; producer did not claim an approval binding'}:{acquisition:c.actualFiveNegatives,actualEntry:acquisition.jobs.find(p=>p.fault===fault)}),entryArtifacts:Object.fromEntries(entryNames.map(n=>[n,pin(path.join(output,n))])),integratedArtifacts:files(path.join(output,'integrated')).map(pin)};
  const file=path.join(out,fault+'-context.json');fs.writeFileSync(file,JSON.stringify(context,null,2)+'\n',{flag:'wx'});runs.push({fault,accepted:true,receipt:producerReceipt,context:pin(file)});
 }
 const result={schemaVersion:1,version:2,status:'PASS',pass:true,parentLifecycleAcceptance:'INCOMPLETE',wholeLifecycleAcceptance:false,productionMainExecuted:false,processExitUsedAsCleanup:false,sourceHashes,identity:c.identityManifest,runs,toolsBefore:dotnet,toolsAfter:dotnet,qualification:'Current bytes and actual producer artifacts verified; none reuses already-executed patched-001, five mutants executed with source identity binding'};
 const validation=validateManagedV2Terminal(result,{repo,sourceHashes,tools}),file=path.join(out,'RESULT.json');fs.writeFileSync(file,JSON.stringify(result,null,2)+'\n',{flag:'wx'});return {file,result,validation};
}
