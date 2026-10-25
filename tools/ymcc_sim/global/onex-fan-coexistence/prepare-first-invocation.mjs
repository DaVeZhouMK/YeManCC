import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createDefaultPreloadMetadataStep} from './preload-metadata.mjs';

const REL='tools/ymcc_sim/global/onex-fan-coexistence';
const SHA=/^[a-f0-9]{64}$/;
const VERSION='10.0.11';
const APPROVED_DOTNET='ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135';
const APPROVED_CSC='2c196b47e0ade88940af111beaad6a6ee5fa0df6152a847e7d3c81059ec15848';
const LOCKED_JSON='a28c251dfe36d881e9e2462e171441b8b0ec156fe3f452602c9149b1b9efe05b';
export const prepareHash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const pin=file=>({path:path.resolve(file),sha256:prepareHash(file)});
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>key(a)===key(b);
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
function fail(code,detail){throw Object.assign(new Error(code),{code,detail,prepareBlocked:true});}
function need(value,code,detail){if(!value)fail(code,detail);}
function checked(file,sha,code='ONEX_PREPARE_INPUT_CHANGED'){
 need(typeof file==='string'&&path.isAbsolute(file),'ONEX_PREPARE_ABSOLUTE_INPUT_REQUIRED',{path:file});
 need(fs.existsSync(file)&&fs.statSync(file).isFile(),'ONEX_PREPARE_INPUT_MISSING',{missing:[file]});
 if(sha!==undefined)need(SHA.test(sha)&&prepareHash(file)===sha,code,{path:file,expected:sha});return file;
}
export function validatePrepareOptions(options){
 need(options.prepareOnly===true,'ONEX_PREPARE_EXPLICIT_MODE_REQUIRED');
 need(!options.toolLoadPlan&&!options.toolLoadPlanSha256,'ONEX_PREPARE_CANNOT_INSTALL_EXECUTION_PLAN');
 need(typeof options.fixedRunBindings==='string'&&SHA.test(options.fixedRunBindingsSha256??''),'ONEX_PREPARE_FIXED_BINDINGS_AND_SHA_REQUIRED');
 checked(options.fixedRunBindings,options.fixedRunBindingsSha256,'ONEX_PREPARE_BINDINGS_SHA_MISMATCH');
 if(options.evidenceDir!==undefined){need(typeof options.evidenceDir==='string'&&options.evidenceDir.trim(),'ONEX_PREPARE_EVIDENCE_DIRECTORY_REQUIRED');const bound=read(options.fixedRunBindings),out=path.resolve(options.evidenceDir);need([bound.sourceRoot,bound.snapshotRoot].every(p=>typeof p==='string'&&path.isAbsolute(p)),'ONEX_PREPARE_BOUND_ROOTS_REQUIRED');need(!inside(bound.snapshotRoot,out)&&!inside(bound.sourceRoot,out),'ONEX_PREPARE_OUTPUT_MUST_NOT_WRITE_BOUND_SOURCE');}
}
export function assertPrepareOperation(kind,args,{csc,project,metadataTool}={}){
 if(kind==='build')need(args[0]==='build'&&same(args[1],project)&&args.includes('--artifacts-path'),'ONEX_PREPARE_BUILD_COMMAND_INVALID');
 else if(kind==='metadata-compile')need(same(args[0],csc)&&args.length===2&&args[1].startsWith('@'),'ONEX_PREPARE_METADATA_COMPILER_INVALID');
 else if(kind==='metadata-inspect')need(same(args[0],metadataTool),'ONEX_PREPARE_METADATA_TOOL_INVALID');
 else fail('ONEX_PREPARE_BUSINESS_EXECUTION_FORBIDDEN',{kind});
}
export async function verifyPrepareBindings(options,out){
 validatePrepareOptions(options);const bound=read(options.fixedRunBindings);
 need(bound.schemaVersion===1&&bound.creationStatus==='PASS'&&bound.captureComplete===true&&bound.snapshotIntegrityVerified===true&&bound.noExecutionApprovalFromCapture===true&&bound.sourceApprovalGranted===false,'ONEX_PREPARE_CAPTURE_NOT_EXECUTION_AUTHORITY');
 checked(bound.manifest,bound.manifestSha256);checked(bound.bindingManifest,bound.bindingManifestSha256);
 const manifest=read(bound.manifest),binding=read(bound.bindingManifest);
 need(same(manifest.snapshotRoot,bound.snapshotRoot)&&same(manifest.sourceRoot,bound.sourceRoot)&&manifest.policy.version===bound.sourcePolicy&&manifest.files.length===bound.files&&manifest.files.reduce((n,f)=>n+f.size,0)===bound.bytes,'ONEX_PREPARE_FIXED_MANIFEST_BINDING_MISMATCH');
 need(binding.graphAfterSha256===bound.bindingGraphSha256&&binding.graphBeforeSha256===bound.bindingGraphSha256&&digest(JSON.stringify(binding.graph))===bound.bindingGraphSha256&&binding.graph.packageCount===bound.bindingPackageCount&&binding.graph.fileCount===bound.bindingFiles,'ONEX_PREPARE_NODE_GRAPH_BINDING_MISMATCH');
 need(binding.verifierCodeSha256===bound.verifierCodeSha256&&same(binding.verifierSource.path,path.join(bound.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')),'ONEX_PREPARE_VERIFIER_PATH_BINDING_MISMATCH');
 const verifier=binding.verifierSource.path,nodeCode=path.join(bound.sourceRoot,'tools/ymcc_sim/global/snapshot-node-tools.mjs');
 checked(verifier,bound.verifierCodeSha256);checked(nodeCode,bound.nodeToolsCodeSha256);
 need(!inside(bound.snapshotRoot,out)&&!inside(bound.sourceRoot,out),'ONEX_PREPARE_OUTPUT_MUST_NOT_WRITE_BOUND_SOURCE');
 const api=await import(pathToFileURL(nodeCode).href),proof=api.verifySnapshotNodeTools({bindingManifestFile:bound.bindingManifest});
 write(path.join(out,'fixed-bindings-verification.json'),proof);
 need(proof.status==='PASS'&&proof.graphSha256===bound.bindingGraphSha256&&proof.packageCount===bound.bindingPackageCount&&proof.fileCount===bound.bindingFiles&&proof.bindingManifestSha256===bound.bindingManifestSha256,'ONEX_PREPARE_FULL_BINDING_VERIFY_FAILED',{proof});
 need(proof.captureVerification?.status==='FROZEN_SOURCE_VERIFIED'&&proof.captureVerification.snapshotIntegrityVerified===true&&proof.captureVerification.fileCount===bound.files&&proof.captureVerification.sourceApprovalGranted===false,'ONEX_PREPARE_COMPLETE_CAPTURE_NOT_VERIFIED');
 checked(options.fixedRunBindings,options.fixedRunBindingsSha256);checked(bound.manifest,bound.manifestSha256);checked(bound.bindingManifest,bound.bindingManifestSha256);checked(verifier,bound.verifierCodeSha256);checked(nodeCode,bound.nodeToolsCodeSha256);
 return {bound,manifest,proof,bindingInput:pin(options.fixedRunBindings),proofPin:pin(path.join(out,'fixed-bindings-verification.json'))};
}
function copyFile(source,dest,records,role){fs.mkdirSync(path.dirname(dest),{recursive:true});const p=pin(source);fs.writeFileSync(dest,fs.readFileSync(source));need(prepareHash(dest)===p.sha256,'ONEX_PREPARE_COPY_CHANGED');records.push({...p,copy:dest,copySha256:p.sha256,role});}
function projectClosure(sourceRoot,view,records){
 const seen=new Set,base=path.join(sourceRoot,REL),generated=[];
 function take(relative){const src=path.resolve(base,relative);need(inside(base,src),'ONEX_PREPARE_PROJECT_SOURCE_ESCAPE',{path:src});const id=key(src);if(seen.has(id))return;seen.add(id);checked(src);copyFile(src,path.join(view,REL,relative),records,'exact fixed tool project/source');
  if(/\.(csproj|props)$/.test(src))for(const match of fs.readFileSync(src,'utf8').matchAll(/<(Compile|ProjectReference|Import)\s+(?:Include|Project)="([^"]+)"/g)){
   const name=match[2];if(name==='$(FixtureSource)'){generated.push(name);continue;}
   need(!/[$*?;]/.test(name),'ONEX_PREPARE_DYNAMIC_PROJECT_SOURCE',{project:src,name});take(path.relative(base,path.resolve(path.dirname(src),name)));
  }
 }
 for(const name of ['Harness.csproj','load-authority/STA.csproj','load-authority/MTA.csproj'])take(name);
 need(generated.includes('$(FixtureSource)'),'ONEX_PREPARE_FIXTURE_PROJECT_INPUT_CHANGED');return [...seen];
}
function allFiles(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{const f=path.join(dir,e.name);return e.isDirectory()?allFiles(f):[f];});}
function makePolicy(metadata,{out,harness,launcher,baseline,rawHc,sourceMetadata,sourceRoot,fixtureProof}){
 const byPath=new Map(metadata.rows.map(r=>[key(r.path),r]));
 const assembly=(file,role)=>{const row=byPath.get(key(file));need(row,'ONEX_PREPARE_OWNED_IDENTITY_MISSING',{path:file});checked(file,row.sha256);return {...pin(file),identity:row.identity,role};};
 const dir=path.dirname(harness),helper=path.join(path.dirname(launcher),'ScopedLoadAuthority.dll');
 const defaultFiles=[harness,helper,...fs.readdirSync(dir).filter(n=>/\.dll$/i.test(n)&&n!=='ScopedLoadAuthority.dll'&&n!==path.basename(harness)).map(n=>path.join(dir,n))];
 const defaults=defaultFiles.map(file=>assembly(file,same(file,harness)?'tool':'infrastructure'));
 // Framework metadata is supplied explicitly by the inspection phase, never by
 // a general name fallback. Exact request mismatch remains reviewable evidence.
 for(const row of metadata.rows.filter(r=>r.role==='framework'))defaults.push(assembly(row.path,'framework'));
 const defaultUnique=new Map;for(const p of defaults){need(!defaultUnique.has(p.identity.split(',')[0]),'ONEX_PREPARE_DEFAULT_DUPLICATE_SIMPLE',{identity:p.identity});defaultUnique.set(p.identity.split(',')[0],p);}
 const custom=baseline.files.map(p=>assembly(p.path,'target'));
 const scope='OldBinaryProbe.Context|'+path.resolve(baseline.root).replace(/[\\/]+$/,'').toUpperCase();
 const request=(p,target)=>({requestedIdentity:p.identity,targetContext:target,targetPath:p.path});
 const unresolved=[];
 for(const row of metadata.rows.filter(r=>custom.some(p=>same(p.path,r.path))))for(const identity of row.references){
  if(![...custom,...defaults].some(p=>p.identity===identity))unresolved.push({requester:row.path,requestedIdentity:identity,reason:'No exact identity/path pin; runtime unification or additional dependency requires review, not a simple-name grant'});
 }
 const args=['old',path.join(baseline.root,'YeManFanHost.dll'),path.join(baseline.root,'YeManLightSetter.dll'),rawHc,path.join(out,'old-binary-result.json')];
 const invocationFile=path.join(out,'first-old-argv.json');write(invocationFile,{schemaVersion:1,kind:'Harness',producer:harness,args,argvSha256:digest(JSON.stringify(args)),executed:false,expectedBusinessExit:0,output:args.at(-1)});
 const policy={schemaVersion:2,ownedRoot:out,entry:harness,contexts:[{id:'Default',kind:'default',registrationType:null,registrationAssembly:null,managed:defaults,requests:defaults.map(p=>request(p,'Default'))},{id:scope,kind:'custom',registrationType:'OldBinaryProbe+Context',registrationAssembly:assembly(harness,'tool'),managed:custom,requests:[...custom.map(p=>request(p,scope)),...defaults.map(p=>request(p,'Default'))]}],metadata:[pin(harness.replace(/\.dll$/i,'.deps.json')),pin(invocationFile),pin(rawHc),pin(fixtureProof),...sourceMetadata]};
 const policyFile=path.join(out,'first-old-policy.candidate.json');write(policyFile,policy);
 const invocation={schemaVersion:1,status:'PREPARED_NOT_EXECUTED',producer:pin(harness),launcher:pin(launcher),policy:pin(policyFile),args,argsSha256:digest(JSON.stringify(args)),dotnetArguments:[launcher,'run',policyFile,prepareHash(policyFile),path.join(out,'first-old-launch-receipt.NOT_EXECUTED.json'),...args],businessInvocationExecuted:false,sourceApprovalGranted:false,executionApprovalGranted:false};
 write(path.join(out,'first-old-invocation.json'),invocation);
 write(path.join(out,'first-old-policy-review.json'),{status:'REVIEW_REQUIRED_NOT_APPROVED',candidatePolicy:pin(policyFile),managedIdentityAndPathPinsConcrete:true,unresolvedRequests:unresolved,executablePolicyApproved:false,historicalBinarySourceLineage:'Existing fixed provenance binds DLL bytes only; it does not provide historical source commit/build lineage',sourceRoot,fullHostApproved:false});
 return {invocation,policy:pin(policyFile),unresolved};
}
export async function prepareFirstInvocation(options){
 const out=path.resolve(options.evidenceDir),commands=[],copies=[];let verification,inputEvidence,fixture;
 const report=(status,extra={})=>({schemaVersion:1,kind:'onex-first-invocation-prepare',status,prepareOnly:true,stopPoint:'Immediately before the original first Harness old launch',producerEntryExecuted:false,HCSDKHostLoaded:false,hostMainStarted:false,physicalPorts:[7,2,3],outerBudgetMs:360000,toolArtifactGate:3072,sourceApprovalGranted:false,executionApprovalGranted:false,fullHostApproved:false,commands,copies,...extra});
 try{
  verification=await verifyPrepareBindings(options,out);const {bound,manifest}=verification,sourceRoot=bound.snapshotRoot,original=bound.originalProductRoot;
  const inputs=await import(pathToFileURL(path.join(sourceRoot,REL,'inputs.mjs')).href);
  const hc=options.hcAssembly??path.join(original,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll');
  const json=path.join(original,'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll'),jsonManifest=path.join(original,'PowerControl/ymcc-shared-runtime/manifest.json');
  const expected=[path.join(sourceRoot,inputs.baselineRelativeRoot,'provenance.json'),...Object.keys(inputs.baselinePins).map(n=>path.join(options.legacyRoot??path.join(sourceRoot,inputs.baselineRelativeRoot),n)),hc,json,jsonManifest];const missing=expected.filter(f=>!fs.existsSync(f));need(!missing.length,'ONEX_PREPARE_INPUT_MISSING',{missing});
  inputEvidence=inputs.resolveInputs({repoRoot:sourceRoot,legacyRoot:options.legacyRoot,hcAssembly:hc,cecilPath:options.cecilPath});
  checked(json,LOCKED_JSON);const jsonRecord=read(jsonManifest).files?.filter(p=>p.packagedPath==='Newtonsoft.Json.dll');need(jsonRecord?.length===1&&jsonRecord[0].sha256===LOCKED_JSON&&jsonRecord[0].version==='13.0.4'&&jsonRecord[0].nugetBinaryPath==='lib/net6.0/Newtonsoft.Json.dll','ONEX_PREPARE_SHARED_JSON_NOT_LOCKED_ORIGINAL');
  const dotnetRoot=path.join(process.env.ProgramFiles??'C:/Program Files','dotnet'),dotnet=path.join(dotnetRoot,'dotnet.exe'),csc=path.join(dotnetRoot,'sdk/10.0.400/Roslyn/bincore/csc.dll');checked(dotnet,APPROVED_DOTNET);checked(csc,APPROVED_CSC);
  const toolRefs=path.join(dotnetRoot,'packs/Microsoft.NETCore.App.Ref',VERSION,'ref/net10.0');need(fs.existsSync(toolRefs),'ONEX_PREPARE_COMPILER_REFERENCES_MISSING',{missing:[toolRefs]});
  const fixturePath=path.join(out,'generated'),fixedModule=path.join(sourceRoot,REL),generator=await import(pathToFileURL(path.join(fixedModule,'generate.mjs')).href);
  fixture=generator.generateFixture(sourceRoot,fixturePath,path.join(fixedModule,'Fixture.template.cs'));
  write(path.join(out,'source-input-provenance.json'),{fixedRunBindings:verification.bindingInput,manifest:pin(bound.manifest),nodeBinding:pin(bound.bindingManifest),verification:verification.proofPin,fixedSourceApprovalGranted:false,generator:pin(path.join(fixedModule,'generate.mjs')),template:pin(path.join(fixedModule,'Fixture.template.cs')),fixture,baseline:inputEvidence.baseline,rawHcMetadataOnly:inputEvidence.hc,sharedJson:pin(json),sharedJsonManifest:pin(jsonManifest),historicalSourceLineageClaimed:false});
  const view=path.join(out,'source-view');projectClosure(sourceRoot,view,copies);copyFile(json,path.join(view,'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll'),copies,'locked unchanged compiler reference');
  const toolDir=path.join(out,'metadata-tools');fs.mkdirSync(toolDir);copyFile(inputEvidence.cecil.path,path.join(toolDir,'Mono.Cecil.dll'),copies,'approved existing Cecil metadata tool reference');
  fs.writeFileSync(path.join(out,'global.json'),JSON.stringify({sdk:{version:'10.0.400',rollForward:'disable'}}));fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear/></packageSources></configuration>');const empty=path.join(out,'empty-feed');fs.mkdirSync(empty);
  const env={...process.env,DOTNET_CLI_HOME:path.join(out,'dotnet-home'),DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_CLI_USE_MSBUILD_SERVER:'0',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true',MSBUILDDISABLENODEREUSE:'1',DOTNET_EnableDiagnostics:'0'};for(const name of Object.keys(env))if(/^(DOTNET_STARTUP_HOOKS|DOTNET_ADDITIONAL_DEPS|DOTNET_SHARED_STORE|CORECLR_.*PROFIL.*|COR_.*PROFIL.*|COMPLUS_.*)$/i.test(name))delete env[name];
  const metadataTool=path.join(toolDir,'PrepareMetadata.dll');
  function run(kind,args,extra={}){assertPrepareOperation(kind,args,{csc,metadataTool,...extra});const start=Date.now(),child=spawnSync(dotnet,args,{cwd:out,env,shell:false,windowsHide:true,encoding:'utf8',timeout:kind==='build'?90000:30000,maxBuffer:8*1024*1024}),log=path.join(out,'command-'+commands.length+'.log');fs.writeFileSync(log,(child.stdout??'')+'\n'+(child.stderr??''));const row={kind,program:pin(dotnet),args,exitCode:child.status,error:child.error?.message??null,signal:child.signal,naturalExit:child.status!==null&&!child.error&&!child.signal,durationMs:Date.now()-start,log:pin(log),businessExecution:false};commands.push(row);write(path.join(out,'prepare-commands.json'),commands);need(child.status===0&&row.naturalExit,'ONEX_PREPARE_TOOL_FAILED',{command:row});}
  const artifacts=path.join(out,'test-build');for(const projectName of ['Harness.csproj','load-authority/STA.csproj','load-authority/MTA.csproj']){const project=path.join(view,REL,projectName);run('build',['build',project,'--artifacts-path',artifacts,'--source',empty,'--packages',path.join(out,'nuget-packages'),'-c','Release','--nologo','-v:q','-m:1','-p:UseSharedCompilation=false','-nodeReuse:false','-p:ImportDirectoryBuildProps=false','-p:ImportDirectoryBuildTargets=false','-p:NuGetAudit=false','-p:NuGetConfigFile='+path.join(out,'NuGet.Config'),'-p:CecilPath='+inputEvidence.cecil.path,'-p:FixtureSource='+path.join(fixturePath,'Fixture.g.cs')],{project});}
  const ownSource=path.join(toolDir,'PrepareMetadata.cs');fs.writeFileSync(ownSource,METADATA_SOURCE);const refs=fs.readdirSync(toolRefs).filter(n=>n.endsWith('.dll')).map(n=>path.join(toolRefs,n));const rsp=path.join(toolDir,'PrepareMetadata.rsp');fs.writeFileSync(rsp,['/nologo','/target:exe','/out:"'+metadataTool+'"',...refs.map(p=>'/reference:"'+p+'"'),'/reference:"'+path.join(toolDir,'Mono.Cecil.dll')+'"','"'+ownSource+'"'].join('\n'));run('metadata-compile',[csc,'@'+rsp]);fs.writeFileSync(path.join(toolDir,'PrepareMetadata.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:'net10.0',rollForward:'Disable',framework:{name:'Microsoft.NETCore.App',version:VERSION}}}));
  const owned=allFiles(path.join(artifacts,'bin')).filter(p=>p.endsWith('.dll')&&!/[\\/]ref(int)?[\\/]/.test(p));const metaFile=path.join(out,'metadata.json'),targets=[...owned,...inputEvidence.baseline.files.map(p=>p.path)];run('metadata-inspect',[metadataTool,metaFile,...targets]);let metadata=read(metaFile);need(metadata.targetRuntimeLoaded===false,'ONEX_PREPARE_METADATA_LOADED_TARGET');
  const requestedIdentities=new Set(metadata.rows.flatMap(r=>r.references)),needed=new Set([...requestedIdentities].map(n=>n.split(',')[0])),framework=[];const runtime=path.join(dotnetRoot,'shared/Microsoft.NETCore.App',VERSION),desktop=path.join(dotnetRoot,'shared/Microsoft.WindowsDesktop.App',VERSION);for(const name of needed)framework.push(...[path.join(runtime,name+'.dll'),path.join(desktop,name+'.dll')].filter(f=>fs.existsSync(f)));
  if(framework.length){run('metadata-inspect',[metadataTool,path.join(out,'framework-metadata.json'),...framework]);const inspected=read(path.join(out,'framework-metadata.json'));need(inspected.targetRuntimeLoaded===false,'ONEX_PREPARE_METADATA_LOADED_TARGET');const selected=inspected.rows.filter(r=>requestedIdentities.has(r.identity));write(path.join(out,'framework-metadata-selection.json'),{selection:'Exact requested full identity among the two fixed runtime roots; no simple-name permission',requestedIdentities:[...requestedIdentities],candidates:inspected.rows.map(r=>({...r,selected:selected.includes(r)}))});metadata.rows.push(...selected.map(r=>({...r,role:'framework'})));write(metaFile,metadata);}
  const harness=owned.filter(f=>path.basename(f)==='OneXFanCoexistence.dll');need(harness.length===1,'ONEX_PREPARE_HARNESS_OUTPUT_NOT_UNIQUE');const launcher=owned.find(f=>path.basename(f)==='MTA.dll');need(launcher,'ONEX_PREPARE_LAUNCHER_MISSING');for(const name of ['STA','MTA'])need(!fs.existsSync(path.join(artifacts,'bin',name,'release',name+'.deps.json')),'ONEX_PREPARE_TRACER_DEPS_MUST_BE_ABSENT');const copiesOfAuthority=owned.filter(f=>path.basename(f)==='ScopedLoadAuthority.dll');need(new Set(copiesOfAuthority.map(prepareHash)).size===1,'ONEX_PREPARE_SHARED_AUTHORITY_COPIES_DIFFER');
  for(const p of copies)checked(p.path,p.sha256);for(const p of inputEvidence.baseline.files)checked(p.path,p.sha256);checked(hc,inputEvidence.hc.sha256);checked(json,LOCKED_JSON);for(const [relative,sha]of Object.entries(fixture.sourceHashes))checked(path.join(sourceRoot,relative),sha);
  const capturedPins=new Map(manifest.files.map(row=>[key(path.join(sourceRoot,row.path)),row.sha256]));const usedSourceFiles=[...copies.map(p=>p.path).filter(p=>inside(sourceRoot,p)),path.join(fixedModule,'inputs.mjs'),path.join(fixedModule,'generate.mjs'),path.join(fixedModule,'Fixture.template.cs'),inputEvidence.baseline.manifestPath,...Object.keys(fixture.sourceHashes).map(p=>path.join(sourceRoot,p))];for(const file of new Set(usedSourceFiles)){const sha=capturedPins.get(key(file));need(sha,'ONEX_PREPARE_USED_SOURCE_NOT_IN_CAPTURE',{path:file});checked(file,sha);};checked(options.fixedRunBindings,options.fixedRunBindingsSha256);checked(bound.manifest,bound.manifestSha256);checked(bound.bindingManifest,bound.bindingManifestSha256);
  const sourceMetadata=[verification.proofPin,pin(path.join(out,'source-input-provenance.json')),pin(path.join(out,'prepare-commands.json')),pin(path.join(out,'framework-metadata-selection.json'))];const prepared=makePolicy(metadata,{out,harness:harness[0],launcher,baseline:inputEvidence.baseline,rawHc:hc,sourceMetadata,sourceRoot,fixtureProof:path.join(fixturePath,'fixture-provenance.json')});
  const preloadStep=createDefaultPreloadMetadataStep({outDir:path.join(out,'default-preload-metadata'),policyPin:prepared.policy,authorityPin:pin(path.join(path.dirname(launcher),'ScopedLoadAuthority.dll')),runtimeconfigPin:pin(launcher.replace(/\.dll$/i,'.runtimeconfig.json')),dotnetPin:pin(dotnet),cscPin:pin(csc)});
  write(path.join(out,'owned-output-manifest.json'),{status:'PREPARED_NOT_EXECUTED',sourceCopies:copies,outputs:allFiles(path.join(artifacts,'bin')).map(pin),identities:metadata.rows,sharedAuthorityCopyPaths:copiesOfAuthority,sharedAuthoritySha256:prepareHash(copiesOfAuthority[0]),compiledProjectsOnly:['Harness','STA','MTA'],unbuiltBecauseAfterStopPoint:['JoinedFactoryProbe','BootstrapFixture'],targetRuntimeLoaded:false});
  return report('PREPARED_NOT_EXECUTED',{fixedBindings:verification.bindingInput,fixedBindingsVerification:verification.proofPin,frozenSourceRoot:sourceRoot,originalInputEvidence:inputEvidence,fixture,ownedOutputs:pin(path.join(out,'owned-output-manifest.json')),firstInvocation:pin(path.join(out,'first-old-invocation.json')),candidatePolicy:prepared.policy,defaultPreloadMetadataStep:preloadStep,policyReview:pin(path.join(out,'first-old-policy-review.json')),remainingPolicyIdentityReviews:prepared.unresolved,executablePolicyApproved:false,sourceCurrentAtVerification:verification.proof.isCurrent,initialSourceApprovalGranted:false});
 }catch(error){return report(error.prepareBlocked?'BLOCKED':'FAIL',{code:error.code??'ONEX_PREPARE_FAILED',error:error.stack,detail:error.detail??(error.path?{missing:[error.path]}:null),fixedBindings:verification?.bindingInput??null,originalInputEvidence:inputEvidence??null,fixture:fixture??null});}
}
const METADATA_SOURCE=`using System;using System.IO;using System.Linq;using System.Reflection;using System.Security.Cryptography;using System.Text.Json;using Mono.Cecil;
static class PrepareMetadata {static int Main(string[] args){var targets=args.Skip(1).Select(Path.GetFullPath).ToArray();var rows=targets.Select(file=>{using var image=AssemblyDefinition.ReadAssembly(file);return new{path=file,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant(),identity=image.Name.FullName,references=image.MainModule.AssemblyReferences.Select(a=>a.FullName).ToArray(),entryPoint=image.EntryPoint?.FullName,moduleInitializers=image.MainModule.Types.Where(t=>t.Name=="<Module>").SelectMany(t=>t.Methods).Where(m=>m.Name==".cctor").Select(m=>m.FullName).ToArray()};}).ToArray();var loaded=AppDomain.CurrentDomain.GetAssemblies().Where(a=>!a.IsDynamic&&!string.IsNullOrEmpty(a.Location)).Select(a=>new{path=a.Location,identity=a.FullName,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(a.Location))).ToLowerInvariant()}).ToArray();bool targetLoaded=loaded.Any(a=>(targets.Contains(Path.GetFullPath(a.path),StringComparer.OrdinalIgnoreCase)&&!Path.GetFullPath(a.path).StartsWith(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(typeof(object).Assembly.Location),"..",".."))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))||new[]{"HandheldCompanion","YeManFanHost","Microsoft.Windows.SDK.NET","WinRT.Runtime","OneXFanCoexistence","STA","MTA"}.Contains(new AssemblyName(a.identity).Name));File.WriteAllText(args[0],JsonSerializer.Serialize(new{status=targetLoaded?"FAIL":"PASS",targetRuntimeLoaded=targetLoaded,rows,actualLoadedManaged=loaded},new JsonSerializerOptions{WriteIndented=true}));return targetLoaded?2:0;}}
`;
