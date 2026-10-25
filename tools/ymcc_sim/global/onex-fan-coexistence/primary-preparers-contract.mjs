import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';

// Dedicated to the public123 / OneX29 preparers. No compiler, process or CLR launcher.
export const PRIMARY_PROFILES = Object.freeze({
  public:'public-original123-current-primary88-schema6',
  onex:'onex-original-lifecycle29-current-primary92-schema6'
});
export const ORIGINAL_BUDGETS = Object.freeze({business:60000,parent:360000,generation:30000,compile:120000});
const here=path.dirname(fileURLToPath(import.meta.url));
const sourceBasisFile=path.join(here,'primary-preparers-source-basis.json');
const lineageFile=path.join(here,'primary-preparers-reviewed-lineage.json');
const load=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));
const BASIS=load(sourceBasisFile), LINEAGE=load(lineageFile);
const key=p=>path.resolve(p).toLowerCase(), same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b), clone=v=>structuredClone(v);
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>({path:path.resolve(f),sha256:hash(fs.readFileSync(f))});
const samePin=(a,b)=>a&&b&&same(a.path,b.path)&&a.sha256===b.sha256;
const inside=(root,f)=>{const r=path.relative(root,f);return !!r&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
function need(ok,code,detail){if(!ok)throw Object.assign(Error(code),{code,detail});}
function plain(f){let p=path.resolve(f);for(;;){if(fs.existsSync(p)){need(!fs.lstatSync(p).isSymbolicLink(),'PRIMARY_REPARSE',p);need(same(fs.realpathSync(p),p),'PRIMARY_FORWARDED_PATH',p);}const up=path.dirname(p);if(up===p)break;p=up;}}
function exactPin(p){need(p&&path.isAbsolute(p.path??'')&&/^[a-f0-9]{64}$/.test(p.sha256??''),'PRIMARY_EXACT_PIN');return p;}
function noModel(v){if(!v||typeof v!=='object')return;for(const [k,x] of Object.entries(v)){need(!(['MODEL_ONLY','modelOnly','synthetic'].includes(k)&&x===true)&&!(k==='evidenceKind'&&x==='MODEL_ONLY'),'PRIMARY_MODEL_NOT_ACTUAL');if(x&&typeof x==='object')noModel(x);}}
function reader(){
  const verified=new Map();
  const check=p=>{exactPin(p);plain(p.path);const old=verified.get(key(p.path));need(!old||old.sha256===p.sha256,'PRIMARY_CONFLICTING_PIN',p);if(!old){need(fs.statSync(p.path).isFile(),'PRIMARY_NOT_FILE',p);need(pin(p.path).sha256===p.sha256,'PRIMARY_PIN_DRIFT',p);verified.set(key(p.path),{path:path.resolve(p.path),sha256:p.sha256});}return p;};
  const read=p=>{check(p);need(fs.statSync(p.path).size<=64*1024*1024,'PRIMARY_JSON_LIMIT');const j=load(p.path);noModel(j);return j;};
  return {check,read,verified};
}
function rowsEqual(a,b){const sort=x=>x.map(p=>[key(p.path),p.sha256]).sort((x,y)=>x[0].localeCompare(y[0]));return equal(sort(a),sort(b));}

/** Capture the real G closure, plus this explicitly named source-only overlay.
 * After import, the same function requires the exact declared current base + four source members.
 * No old D144 source tree is accepted as the current repository.
 */
export async function capturePrimarySourceBinding({repoRoot}={}){
  need(same(repoRoot,BASIS.repoRoot),'PRIMARY_REAL_G_ROOT_REQUIRED');
  const r=reader();for(const p of BASIS.sources)r.check(p);
  const runtime=BASIS.sources.find(p=>p.relative.endsWith('/tool-policy-runtime.mjs'));
  r.check(runtime);const {toolSourceInputs}=await import(pathToFileURL(runtime.path).href);
  const currentBase=[...BASIS.sources,...(BASIS.concurrentPrimarySourceExtensions??[])];
  for(const p of currentBase)r.check(p);
  const actual=toolSourceInputs(repoRoot), base=new Map(currentBase.map(p=>[key(p.path),p]));
  const extensions=new Map(BASIS.addedSourceRelatives.map(relative=>[key(path.join(repoRoot,relative)),relative]));
  const active=[];
  for(const p of actual){
    if(base.has(key(p.path)))need(p.sha256===base.get(key(p.path)).sha256,'PRIMARY_G_BASE_DRIFT',p);
    else{const relative=extensions.get(key(p.path));need(relative,'PRIMARY_UNREVIEWED_SOURCE_MEMBER',p);need(p.sha256===pin(path.join(here,path.basename(relative))).sha256,'PRIMARY_IMPORTED_OVERLAY_DRIFT',p);active.push(relative);}
    r.check(p);
  }
  need(actual.length===currentBase.length+active.length&&[0,4].includes(active.length),'PRIMARY_PARTIAL_IMPORT');
  need(currentBase.every(p=>actual.some(a=>samePin(a,p))),'PRIMARY_SOURCE_SET');
  const preparerSources=BASIS.addedSourceRelatives.map(relative=>({relative,...pin(path.join(here,path.basename(relative)))}));
  for(const p of preparerSources)r.check(p);
  return {schemaVersion:1,kind:'current-primary-source-binding',repoRoot:path.resolve(repoRoot),
    baseSourceCount:144,currentPrimaryBaseSourceCount:currentBase.length,toolSourceCount:actual.length,toolSources:actual,preparerSources,
    declarationPins:[pin(sourceBasisFile),pin(lineageFile)],
    sharedCLI:actual.find(p=>same(p.path,path.join(repoRoot,'tools/ymcc_sim/global/cli.mjs'))),
    currentBusinessAccepted:false,executionApprovalGranted:false};
}
function terminal(p,budget,code){need(p.code===code&&p.signal===null&&p.error===null&&p.expired===false&&p.naturalExit===true&&p.shell===false&&p.budgetMs===budget&&Number.isFinite(p.durationMs)&&p.durationMs>=0&&p.durationMs<=budget,'PRIMARY_ORIGINAL_PROCESS_TERMINAL');}
function checkedAudit(r,before,after){
  r.check(before);r.check(after);need(before.sha256===after.sha256,'PRIMARY_COMPILER_AUDIT_CHANGED');
  let count=0;
  for(const line of fs.readFileSync(before.path,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/).filter(Boolean)){
    const at=line.lastIndexOf('|');need(at>0&&/^[A-Fa-f0-9]{64}$/.test(line.slice(at+1)),'PRIMARY_AUDIT_ROW');
    r.check({path:line.slice(0,at),sha256:line.slice(at+1).toLowerCase()});count++;
  }
  need(count>0,'PRIMARY_EMPTY_COMPILER_AUDIT');return count;
}
function checkCurrentBuilds(r,binding){
  const tools=r.read(LINEAGE.currentBuilds.tools),host=r.read(LINEAGE.currentBuilds.host);
  need(tools.sourceCount===144&&tools.tools.length===5&&tools.compilerInputs.length===8,'PRIMARY_FIVE_TOOL_BUILD');
  const source=r.read(tools.sourceBefore);need(rowsEqual(source.sources,BASIS.sources),'PRIMARY_BUILD_SOURCE_NOT_G144');
  need(source.sources.every(p=>binding.toolSources.some(a=>samePin(a,p))),'PRIMARY_BUILD_CURRENT_SOURCE_BINDING');
  for(const p of source.sources)r.check(p);
  r.read(tools.generated);
  const projectAudits=tools.compilerInputs.map(a=>({name:a.name,inputs:checkedAudit(r,a.before,a.after)}));
  for(const t of tools.tools){for(const k of ['entry','deps','runtimeconfig','helper'])if(t[k])r.check(t[k]);if(t.depsAbsent)need(!fs.existsSync(t.depsAbsent),'PRIMARY_LAUNCHER_DEPS_PRESENT');}
  r.check(tools.sharedAuthority);r.check(LINEAGE.currentBuilds.ports);r.check(tools.dotnet);
  need(host.sourceFileCount===55&&same(host.sourceRoot,binding.repoRoot)&&host.sourceBound===true,'PRIMARY_CURRENT_HOST_SOURCE');
  for(const p of [host.host,host.backend,host.payloadBinding,host.staging,host.buildOutputs])r.check(p);
  const before=r.read(host.inputBefore),after=r.read(host.inputAfter),audit=r.read(host.compilerInputAudit);
  need(before.sourceFileCount===55&&audit.projects.length===2,'PRIMARY_HOST_55_INPUTS');
  for(const p of before.pins)r.check(p);for(const p of after.pins??[])r.check(p);
  const deployment=new Set(['install-fan-host-payload.ps1','run-emergency-fan-restore.ps1','YeManFanHost.authorization.md']);
  for(const p of before.pins.filter(p=>p.relativePath)){const relative=deployment.has(p.relativePath)?'PowerControl/fan-host/'+p.relativePath:p.relativePath;need(same(p.path,path.join(binding.repoRoot,relative)),'PRIMARY_HOST_PRIMARY_SOURCE_PATH',p);}
  for(const a of audit.projects){checkedAudit(r,a.before,a.after);r.check(a.commandLineArguments);for(const p of a.pins)r.check(p);}
  comparison(r,LINEAGE.currentBuilds.hostManagedComparison,LINEAGE.canonicalCleanHost,host.host);
  return {tools,host,projectAudits,compilerInputsActuallyRehashed:true,hostManagedSourceEquivalent:true,currentCompiledPinsQualified:false};
}

function safeRoot({ownedParent,outDir},oldRoot){
  need(path.isAbsolute(ownedParent??'')&&path.isAbsolute(outDir??'')&&same(path.dirname(outDir),ownedParent),'PRIMARY_FRESH_DIRECT_CHILD');
  need(/^[dD]:[\\/]/.test(outDir)&&!outDir.split(/[\\/]/).includes('..')&&!/[<>"|?*]/.test(outDir.slice(3))&&!outDir.slice(2).includes(':'),'PRIMARY_LOCAL_D_OUTPUT');
  plain(ownedParent);plain(outDir);need(fs.statSync(ownedParent).isDirectory()&&!fs.existsSync(outDir),'PRIMARY_FRESH_OUTPUT_REQUIRED');
  need(!same(oldRoot,outDir)&&!inside(oldRoot,outDir)&&!inside(outDir,oldRoot),'PRIMARY_OVERLAPPING_ROOT');
  need(path.join(outDir,'Build','positive','case','n').length<239,'PRIMARY_ORIGINAL_259_BOUNDARY');
}
function validateLegacy(stage,role,b,p,n,argv){
  const s=LINEAGE.stages[stage],approved=s.roles[role];need(approved,'PRIMARY_UNKNOWN_ROLE');
  need(b.role===role&&equal(b.budgets,ORIGINAL_BUDGETS)&&equal(s.budgets,ORIGINAL_BUDGETS),'PRIMARY_ORIGINAL_BUDGET');
  need(b.initializeRole==='UNKNOWN'&&b.nativeBoundaryLength===259&&Array.isArray(b.unknownReferences),'PRIMARY_UNKNOWN_AND_BOUNDARY');
  need(equal(b.expectedCaseIds,s.ids)&&new Set(s.ids).size===s.caseCount&&equal(b.expectedFailureMask,approved.expectedFailureMask)&&equal(b.failureMasks,s.failureMasks)&&b.expectedExitCode===approved.expectedExitCode,'PRIMARY_ORIGINAL_CASES_AND_MASKS');
  need(Object.entries(approved.artifacts).every(([k,h])=>b.artifacts[k]?.sha256===h)&&b.host.sha256===approved.hostSHA256&&b.hc.sha256===approved.hcSHA256,'PRIMARY_REVIEWED_IMAGE_IDENTITY');
  need(p.schemaVersion===6&&same(p.ownedRoot,b.ownedRoot)&&same(p.entry,b.artifacts.producer.path)&&p.contexts.length===1,'PRIMARY_SCHEMA6_DEFAULT');
  const d=p.contexts[0];need(d.id==='Default'&&d.kind==='default'&&d.registrationType===null&&d.registrationAssembly===null,'PRIMARY_REAL_DEFAULT');
  need(d.managed.length===s.managedCount&&d.managed.filter(x=>x.role==='framework').length===s.frameworkCount&&new Set(d.managed.map(x=>key(x.path))).size===s.managedCount,'PRIMARY_MANAGED_FRAMEWORK_PAIR');
  need(p.exactAliases.length===6&&p.exactAliases.every(a=>equal(a.requestingContexts,['Default'])&&a.targetContext==='Default'&&d.managed.some(m=>equal(m,a.provider))&&d.requests.some(q=>q.requestedIdentity===a.requestedIdentity&&same(q.targetPath,a.provider.path)&&q.targetContext==='Default')),'PRIMARY_EXACT_ALIAS_CONSERVATION');
  const frameworks=new Set(d.managed.filter(x=>x.role==='framework').map(x=>key(x.path)));
  need(Object.keys(n).length===116&&Object.keys(n).every(f=>!frameworks.has(key(f))),'PRIMARY_NATIVE_GUARD116');
  need(d.managed.filter(x=>x.role!=='framework').every(x=>n[x.path]===x.sha256),'PRIMARY_TARGET_NATIVE_GUARD');
  need(b.args.length===9&&b.args[8]===(stage==='public'?'public':'lifecycle')&&equal(argv.args,b.args)&&argv.argvSha256===hash(JSON.stringify(b.args))&&same(argv.producer,b.artifacts.producer.path),'PRIMARY_NINE_CONCRETE_ARGUMENTS');
  const expected=[b.host.path,b.hc.path,path.join(b.ownedRoot,'runtime'),path.join(b.ownedRoot,'shared'),b.runtimePins.path,path.join(b.ownedRoot,'inputs','inventory.json'),path.join(b.ownedRoot,'inputs','YeManFanHost.json'),b.resultPath,b.args[8]];
  need(expected.every((x,k)=>k===8?x===b.args[k]:same(x,b.args[k])),'PRIMARY_ARGUMENT_OWNERSHIP');
}
function legacyEvidence(r,stage,role){
  const s=LINEAGE.stages[stage],approved=s.roles[role],summary=r.read(approved.summary),b=r.read(approved.prepared);
  const p=r.read(b.policy),n=r.read(b.runtimePins),argv=r.read(b.argv);validateLegacy(stage,role,b,p,n,argv);
  const seal=r.read(s.historicalDeliverySeal);r.read(s.historicalDeliveryState);
  need(seal.referencedPins.some(p=>samePin(p,s.joint)),'PRIMARY_HISTORICAL_SEAL_JOINT_LINEAGE');
  const result=r.read(approved.businessResult);need(equal(result.cases.map(x=>x.id),s.ids)&&equal(result.cases.filter(x=>x.status==='FAIL').map(x=>x.id),approved.expectedFailureMask),'PRIMARY_HISTORICAL_ACTUAL_MASK');
  need(samePin(summary.prepared,approved.prepared)&&summary.cases===s.caseCount&&summary.managedCount===s.managedCount&&summary.frameworkCount===s.frameworkCount&&summary.unknownManaged===0&&summary.forcedThreadCleanup===0&&summary.deniedNative===0,'PRIMARY_ACCEPTED_QUALIFIER');
  for(const [i,pin] of approved.processPins.entries())terminal(r.read(pin),60000,i===2?approved.expectedExitCode:0);
  r.read(approved.graph);r.read(approved.launch);r.read(s.joint);
  for(const f of s.frameworkQualifications)r.read(f);
  for(const p of [...s.reviewedSources,...s.extensionSourceFiles])r.check(p);
  for(const item of [...Object.values(b.artifacts),b.host,b.hc,b.program,...p.contexts[0].managed])r.check(item);
  for(const [f,h]of Object.entries(n))r.check({path:f,sha256:h});
  // The old schema5 G authority source reference is historical. Replace only this
  // metadata permission after the exact independent current schema6 qualifier.
  const oldCompile=r.read(LINEAGE.compilationQualifications.qualifiedCompilation);
  const auth=r.read(LINEAGE.compilationQualifications.authorityCompilation);
  r.read(LINEAGE.compilationQualifications.authorityRawApi);r.read(LINEAGE.compilationQualifications.staCompilation);
  const obsolete=oldCompile.sources.find(s=>s.relative==='load-authority/ScopedLoadAuthority.cs').compilerInput;
  const actualAuthoritySource=pin(path.join(BASIS.repoRoot,'tools/ymcc_sim/global/onex-fan-coexistence/load-authority/ScopedLoadAuthority.cs'));
  need(auth.source.sha256===actualAuthoritySource.sha256&&b.artifacts.authority.sha256===auth.authority.sha256,'PRIMARY_SEPARATE_SCHEMA6_QUALIFICATION');
  r.check(auth.source);r.check(auth.authority);r.check(actualAuthoritySource);
  const metadata=[];let superseded=0;
  for(const a of p.metadata){if(samePin(a,obsolete)){superseded++;metadata.push(actualAuthoritySource);}else{r.check(a);metadata.push(a);}}
  need(superseded===1,'PRIMARY_ONE_EXACT_SUPERSEDED_METADATA');
  return {b,p,n,argv,metadata,obsolete,actualAuthoritySource,summary};
}

// Consume the parent's pinned full managed comparison. Never rerun its inspector.
export function assertManagedCompilationComparison(result,before,after){
  need(result?.status==='EQUAL_MANAGED_IL_METADATA_AND_DATA_ONLY'&&result.before?.sha256===before.sha256&&result.after?.sha256===after.sha256,'PRIMARY_MANAGED_COMPARISON_PAIR');
  need(result.targetCodeExecuted===false&&result.executionApprovalGranted===false,'PRIMARY_COMPARISON_NOT_RUNTIME_AUTHORITY');
  need(equal(result.changes,[])&&result.allMetadataRowsExceptMethodAndFieldRvasEqual===true&&(equal(result.heapsCompared,['#Strings','#US','#Blob'])||equal(result.heapsCompared,['#Strings exact bytes','#US exact bytes','#Blob all values with multiplicity and each table reference resolved by value']))&&result.mvidDifference?.onlySingleMvidGuidHeap===true,'PRIMARY_MANAGED_FULL_TABLE_COMPARISON');
  need(Array.isArray(result.metadataRows)&&result.metadataRows.length===64&&Number.isInteger(result.methods)&&result.methods>0&&result.methods===result.metadataRows[6]&&Number.isInteger(result.methodsWithBodies)&&result.methodsWithBodies>0&&result.methodsWithBodies<=result.methods&&Number.isInteger(result.initializedFields)&&result.initializedFields===result.metadataRows[29]&&Number.isInteger(result.initializedFieldBytes)&&result.initializedFieldBytes>=0,'PRIMARY_MANAGED_TABLE_COUNTS');
  return true;
}
function comparison(r,p,before,after){
  const document=r.read(p);need(document.schemaVersion===1&&Array.isArray(document.results),'PRIMARY_MANAGED_COMPARISON_DOCUMENT');
  const row=document.results.find(x=>x.before?.sha256===before.sha256&&x.after?.sha256===after.sha256);assertManagedCompilationComparison(row,before,after);
  r.check(row.before);r.check(row.after);r.check(row.reader);r.check(LINEAGE.comparisonHelper);
  r.check(before);r.check(after);return row;
}
function qualify(r,stage,role,legacy,builds,input){
  if(!input.compiledQualificationPin)return {qualified:false,missing:['CURRENT_COMPILED_IMAGE_WHOLE_TYPED_EQUIVALENCE','CURRENT_AUTHORITY_API_PAIR','CURRENT_PORTED_HOST_LEAF_LINEAGE']};
  const q=r.read(input.compiledQualificationPin),b=legacy.b;
  need(q.kind==='public-onex-current-compiled-images-qualification'&&q.schemaVersion===1&&q.stage===stage&&q.role===role&&samePin(q.toolBuild,LINEAGE.currentBuilds.tools)&&samePin(q.hostBuild,LINEAGE.currentBuilds.host),'PRIMARY_CURRENT_QUALIFICATION_BINDING');
  const joined=builds.tools.tools.find(t=>t.kind==='JoinedFactoryProbe'),sta=builds.tools.tools.find(t=>t.kind==='STA');
  const replacements={producer:joined.entry,deps:joined.deps,runtimeconfig:joined.runtimeconfig,authority:builds.tools.sharedAuthority,producerAuthority:builds.tools.sharedAuthority,ports:LINEAGE.currentBuilds.ports,launcher:sta.entry,launcherRuntimeconfig:sta.runtimeconfig};
  const backend=legacy.p.contexts[0].managed.find(p=>path.basename(p.path)==='YeManFanBackend.dll');need(backend,'PRIMARY_BACKEND_DESCRIPTOR');
  exactPin(q.hostLeaf);r.check(q.hostLeaf);
  const pairs=[['cleanHost',LINEAGE.canonicalCleanHost,builds.host.host],['backend',backend,builds.host.backend],...['producer','authority','ports','launcher'].map(k=>[k,b.artifacts[k],replacements[k]]),['hostLeaf',b.host,q.hostLeaf]];
  need(Array.isArray(q.comparisons)&&q.comparisons.length===pairs.length,'PRIMARY_EXACT_TYPED_COMPARISONS');
  for(const [purpose,before,after]of pairs){
    const row=q.comparisons.find(c=>c.purpose===purpose);need(row&&samePin(row.before,before)&&samePin(row.after,after),'PRIMARY_TYPED_DOUBLE_PIN',purpose);
    need(row.comparisonPin,'PRIMARY_WHOLE_MANAGED_COMPARISON_REQUIRED',purpose);
    comparison(r,row.comparisonPin,before,after);
  }
  const api=r.read(q.authorityApiReview),authorityPair=q.comparisons.find(c=>c.purpose==='authority');
  need(api.kind==='current-authority-api-equivalence-bridge'&&samePin(api.originalApiReview,LINEAGE.compilationQualifications.authorityRawApi)&&samePin(api.before,b.artifacts.authority)&&samePin(api.after,replacements.authority)&&samePin(api.comparison,authorityPair.comparisonPin),'PRIMARY_CURRENT_AUTHORITY_API');
  r.read(api.originalApiReview);
  const generation=r.read(q.hostLeafGeneration),publicHost=q.publicHost??q.hostLeaf;r.check(publicHost);
  need(samePin(generation.source,builds.host.host)&&samePin(generation.target,publicHost),'PRIMARY_NEW_HOST_LEAF_SOURCE');
  const proof=r.read(generation.proof),oldGeneration=r.read(LINEAGE.canonicalGeneration),old=r.read(oldGeneration.roles[0].summary),oldProof=r.read(old.proof);
  need(proof.status==='PASS'&&proof.sourceSha256===builds.host.host.sha256&&proof.instrumentedSha256===publicHost.sha256&&equal(proof.changedMethods,oldProof.changedMethods)&&proof.hardwareIo===false,'PRIMARY_ORIGINAL_HOST_PORT_SCOPE');
  terminal(r.read(generation.process),30000,0);
  if(stage==='public'&&role!=='positive'){
    const fault=r.read(q.hostFaultGeneration),prior=r.read(b.variantQualification.proof),faultProof=r.read(fault.proof);
    need(samePin(fault.source,publicHost)&&samePin(fault.target,q.hostLeaf)&&faultProof.status==='PASS'&&faultProof.sourceSha256===publicHost.sha256&&faultProof.faultSha256===q.hostLeaf.sha256&&equal(faultProof.changedMethods,prior.changedMethods),'PRIMARY_ORIGINAL_SINGLE_METHOD_FAULT');
    terminal(r.read(fault.process),30000,0);
  }else need(samePin(publicHost,q.hostLeaf),'PRIMARY_POSITIVE_HOST_LEAF');
  return {qualified:true,replacements,backendBefore:backend,backendAfter:builds.host.backend,hostLeaf:q.hostLeaf,qualificationPin:input.compiledQualificationPin};
}

function project(stage,role,legacy,input,qualification){
  const {b,p,n}=legacy,root=input.outDir,move=f=>inside(b.ownedRoot,f)?path.join(root,path.relative(b.ownedRoot,f)):f;
  const swaps=new Map();
  if(qualification?.qualified){for(const [k,a]of Object.entries(qualification.replacements))swaps.set(key(b.artifacts[k].path),a);swaps.set(key(qualification.backendBefore.path),qualification.backendAfter);swaps.set(key(b.host.path),qualification.hostLeaf);}
  const source=a=>swaps.get(key(a.path))??a;
  const descriptor=a=>{const next=source(a);return {...clone(a),path:move(a.path),sha256:next.sha256};};
  const files=Object.entries(n).filter(([f])=>inside(b.ownedRoot,f)).map(([f,h])=>({source:source({path:f,sha256:h}),destination:move(f)}));
  need(files.length===115,'PRIMARY_ORIGINAL_115_PAYLOAD_MEMBERS');
  const policy=clone(p);policy.ownedRoot=root;policy.entry=move(p.entry);policy.metadata=clone(legacy.metadata??p.metadata);
  policy.contexts[0].managed=p.contexts[0].managed.map(descriptor);
  policy.contexts[0].requests=p.contexts[0].requests.map(q=>({...q,targetPath:move(q.targetPath)}));
  policy.exactAliases=p.exactAliases.map(a=>({...a,provider:descriptor(a.provider)}));
  const nativePins=Object.fromEntries(Object.entries(n).map(([f,h])=>[move(f),source({path:f,sha256:h}).sha256]));
  const resultPath=path.join(root,'Build',role,'case','result.json'),runtimePath=path.join(root,'runtime-pins.json');
  const args=b.args.map(move);args[4]=runtimePath;args[7]=resultPath;
  return {files,policy,nativePins,args,resultPath,runtimePath,artifacts:Object.fromEntries(Object.entries(b.artifacts).map(([k,a])=>[k,descriptor(a)])),host:descriptor(b.host),hc:descriptor(b.hc)};
}
export function projectPrimaryStageMODEL_ONLY({stage,role,bundle,input}){
  need(bundle?.evidenceKind==='MODEL_ONLY','PRIMARY_MODEL_LABEL_REQUIRED');
  validateLegacy(stage,role,bundle.b,bundle.p,bundle.n,bundle.argv);
  return {evidenceKind:'MODEL_ONLY',...project(stage,role,bundle,input,null),expectedCaseIds:clone(bundle.b.expectedCaseIds),expectedFailureMask:clone(bundle.b.expectedFailureMask),budgets:clone(bundle.b.budgets),executionApprovalGranted:false};
}
async function inspect(stage,{inputPin}={}){
  need(Object.hasOwn(PRIMARY_PROFILES,stage),'PRIMARY_STAGE');const r=reader(),input=r.read(inputPin);
  need(input.schemaVersion===1&&input.profile===PRIMARY_PROFILES[stage]&&LINEAGE.stages[stage].roles[input.role],'PRIMARY_PROFILE_ROLE');
  const captured=await capturePrimarySourceBinding({repoRoot:input.repoRoot}),binding=r.read(input.sourceBindingPin);
  need(equal(binding,captured),'PRIMARY_CURRENT_SOURCE_BINDING_DRIFT');
  for(const p of [...binding.toolSources,...binding.preparerSources,...binding.declarationPins])r.check(p);
  const builds=checkCurrentBuilds(r,binding),legacy=legacyEvidence(r,stage,input.role);
  safeRoot(input,legacy.b.ownedRoot);
  const qualification=qualify(r,stage,input.role,legacy,builds,input),projection=qualification.qualified?project(stage,input.role,legacy,input,qualification):null;
  return {r,input,binding,builds,legacy,qualification,projection};
}
function report(stage,x){const s=LINEAGE.stages[stage];return {schemaVersion:1,status:x.qualification.qualified?'CURRENT_SOURCE_AND_COMPILED_INPUTS_QUALIFIED_PREPARE_ONLY':'BLOCKED_CURRENT_COMPILED_PINS_NOT_QUALIFIED',stage,role:x.input.role,sourceBinding:x.input.sourceBindingPin,currentGSourceCount:x.binding.toolSourceCount,sharedCLI:x.binding.sharedCLI,currentBuilds:clone(LINEAGE.currentBuilds),currentCleanHost:x.builds.host.host,currentBackend:x.builds.host.backend,currentAuthority:x.builds.tools.sharedAuthority,hostManagedSourceEquivalent:x.builds.hostManagedSourceEquivalent,currentCompiledPinsQualified:x.qualification.qualified,missing:x.qualification.missing??[],caseCount:s.caseCount,expectedFailureMask:clone(s.roles[x.input.role].expectedFailureMask),managedCount:s.managedCount,frameworkCount:s.frameworkCount,nativeGuardCount:116,aliasCount:6,budgets:clone(ORIGINAL_BUDGETS),metadataBudgetMs:60000,stageCreated:false,targetExecutions:0,metadataExecuted:false,currentBusinessAccepted:false,fullFan360Passed:false,initializeRole:'UNKNOWN',cpuPowerBenefit:null,verifiedPinCount:x.r.verified.size};}
export async function inspectPrimaryStage(stage,options){return report(stage,await inspect(stage,options));}
export async function preparePrimaryStage(stage,options){
  const x=await inspect(stage,options);need(x.qualification.qualified,'PRIMARY_CURRENT_COMPILED_PINS_NOT_QUALIFIED',report(stage,x));
  const {r,input,projection:q}=x,root=input.outDir;
  for(const p of r.verified.values())need(pin(p.path).sha256===p.sha256,'PRIMARY_PREWRITE_INPUT_DRIFT',p);
  // Recheck dynamic source membership as well as all bytes before creating output.
  need(equal(x.binding,await capturePrimarySourceBinding({repoRoot:input.repoRoot})),'PRIMARY_PREWRITE_SOURCE_SET_DRIFT');
  safeRoot(input,x.legacy.b.ownedRoot);fs.mkdirSync(root);
  const writes=[];const write=(name,obj)=>{const f=path.join(root,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,JSON.stringify(obj,null,2)+'\n',{flag:'wx'});const p=pin(f);writes.push(p);return p;};
  for(const item of q.files){plain(item.source.path);need(pin(item.source.path).sha256===item.source.sha256,'PRIMARY_COPY_SOURCE_DRIFT');fs.mkdirSync(path.dirname(item.destination),{recursive:true});fs.copyFileSync(item.source.path,item.destination,fs.constants.COPYFILE_EXCL);const p=pin(item.destination);need(p.sha256===item.source.sha256,'PRIMARY_COPY_DRIFT');writes.push(p);}
  const runtimePins=write('runtime-pins.json',q.nativePins);
  const currentSourceBinding=write('CURRENT-SOURCE-BINDING.json',x.binding);
  const sourceProjectionProof=write('SOURCE-PROJECTION-PROOF.json',{kind:'CURRENT_INPUTS_PREPARED_NOT_EXECUTED',historicalPrepared:LINEAGE.stages[stage].roles[input.role].prepared,historicalAcceptance:LINEAGE.stages[stage].roles[input.role].summary,currentSourceBinding,currentBuilds:LINEAGE.currentBuilds,compiledQualification:input.compiledQualificationPin,copyRecords:q.files,supersededHistoricalSourceMetadata:{before:x.legacy.obsolete,after:x.legacy.actualAuthoritySource},originalCasesAndMasksUnchanged:true,metadataApprovalGranted:false,executionApprovalGranted:false});
  const metadata=new Map(q.policy.metadata.map(p=>[key(p.path),p]));
  for(const p of [...r.verified.values(),...writes]){const prior=metadata.get(key(p.path));need(!prior||prior.sha256===p.sha256,'PRIMARY_METADATA_CONFLICT',p);metadata.set(key(p.path),p);}
  q.policy.metadata=[...metadata.values()];const policy=write('POLICY.json',q.policy);
  const argv=write('business-argv.json',{schemaVersion:1,kind:'JoinedFactoryProbe',producer:q.artifacts.producer.path,args:q.args,argvSha256:hash(JSON.stringify(q.args)),executed:false});
  const metadataReceiptPath=path.join(root,'receipts','metadata.json'),preloadReceiptPath=path.join(root,'receipts','preload.json'),launchPath=path.join(root,'receipts','launch.json');fs.mkdirSync(path.dirname(metadataReceiptPath));fs.mkdirSync(path.dirname(q.resultPath),{recursive:true});
  const b=x.legacy.b,plan={...clone(b),status:'CURRENT_PRIMARY_PREPARED_METADATA_AND_BUSINESS_NOT_EXECUTED',profile:PRIMARY_PROFILES[stage],ownedRoot:root,artifacts:q.artifacts,host:q.host,hc:q.hc,policy,runtimePins,argv,args:q.args,resultPath:q.resultPath,nativeProbeRoot:path.join(path.dirname(q.resultPath),'n'),nativeMath:{path:path.join(root,'runtime','GamepadMotion.dll'),sha256:q.nativePins[path.join(root,'runtime','GamepadMotion.dll')]},metadataReceiptPath,preloadReceiptPath,launchPath,sourceProjectionProof,sourceBinding:currentSourceBinding,toolSources:x.binding.toolSources,currentBuilds:clone(LINEAGE.currentBuilds),compiledQualification:input.compiledQualificationPin,historicalPrepared:LINEAGE.stages[stage].roles[input.role].prepared,compileReceipt:LINEAGE.currentBuilds.tools,originalCompileReceipt:LINEAGE.currentBuilds.tools,historicalOriginalCompileReceipt:b.originalCompileReceipt,historicalCompileReceipt:b.compileReceipt,compileReceiptScope:'CURRENT_G144_FIVE_TOOL_ACTUAL_COMPILER_AUDIT_WITH_REQUIRED_COMPILED_QUALIFICATION',sourceEvidencePins:[...r.verified.values()],outputPins:writes,metadataArguments:[q.artifacts.launcher.path,'validate',policy.path,policy.sha256,metadataReceiptPath],preloadArguments:[q.artifacts.launcher.path,'preload',policy.path,policy.sha256,preloadReceiptPath],businessArguments:[q.artifacts.launcher.path,'run',policy.path,policy.sha256,launchPath,...q.args],metadataBudgetMs:60000,metadataApprovalGranted:false,executionApprovalGranted:false,currentCompiledPinsQualified:true,currentPrimaryAccepted:false,publicFactoryAccepted:false,lifecycleBusinessAccepted:false,wholeFanPassed:false,fullFan360Passed:false,targetExecutions:0,cpuPowerBenefit:null};
  // Keep the old compile receipt with its real pin and explicit historical scope.
  const planPin=write('PREPARED-PLAN.json',plan);return {planPin,plan};
}

export async function primaryPreparerMain(stage,args=process.argv.slice(2)){
  if(args[0]==='--capture-source-binding'){
    need(args.length===3,'PRIMARY_CAPTURE_ARGUMENTS');const binding=await capturePrimarySourceBinding({repoRoot:args[2]});plain(args[1]);fs.writeFileSync(args[1],JSON.stringify(binding,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({sourceBinding:pin(args[1]),toolSourceCount:binding.toolSourceCount}));return;
  }
  need(args.length===6&&args[0]==='--input'&&args[2]==='--sha256'&&args[4]==='--mode'&&['inspect','prepare'].includes(args[5]),'PRIMARY_CONCRETE_ARGUMENTS');
  const options={inputPin:{path:path.resolve(args[1]),sha256:args[3]}};
  const result=args[5]==='inspect'?await inspectPrimaryStage(stage,options):await preparePrimaryStage(stage,options);
  console.log(JSON.stringify(result,null,2));if(result.currentCompiledPinsQualified===false)process.exitCode=2;
}
