import fs from 'node:fs';
import {verifyLaterParentToolStages} from './parent-later-stages.mjs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {pathToFileURL, fileURLToPath} from 'node:url';

export const BUDGETS=Object.freeze({business:45000,metadata:60000,parent:360000});
const SHA=/^[a-f0-9]{64}$/;
const key=p=>path.resolve(p).toLowerCase();
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const inside=(root,p)=>{const r=path.relative(path.resolve(root),path.resolve(p));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export const fileHash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const pin=p=>({path:path.resolve(p),sha256:fileHash(p)});
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const need=(ok,code,detail)=>{if(!ok)throw Object.assign(Error(code),{code,detail,currentFirstOldBlocked:true});};
function plain(p){let cursor=path.resolve(p);for(;;){if(fs.existsSync(cursor))need(!fs.lstatSync(cursor).isSymbolicLink()&&same(fs.realpathSync(cursor),cursor),'CURRENT_FIRST_OLD_REDIRECT',cursor);const parent=path.dirname(cursor);if(parent===cursor)break;cursor=parent;}}
export function checked(p){need(p&&typeof p.path==='string'&&path.isAbsolute(p.path)&&SHA.test(p.sha256??''),'CURRENT_FIRST_OLD_EXACT_PIN_REQUIRED',p);plain(p.path);need(fs.existsSync(p.path)&&fs.statSync(p.path).isFile(),'CURRENT_FIRST_OLD_PIN_MISSING',p);need(fileHash(p.path)===p.sha256,'CURRENT_FIRST_OLD_PIN_CHANGED',p);return p;}
function samePin(a,b){return a&&b&&same(a.path,b.path)&&a.sha256===b.sha256;}
function unique(rows,code){const seen=new Set;for(const r of rows){need(!seen.has(key(r.path)),code,r);seen.add(key(r.path));}}
function noModel(d){if(!d||typeof d!=='object')return;need(d.MODEL_ONLY!==true&&d.evidenceKind!=='MODEL_ONLY','CURRENT_FIRST_OLD_MODEL_NOT_AUTHORITY');for(const v of Object.values(d))noModel(v);}
function auditRows(p){checked(p);const rows=fs.readFileSync(p.path,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/).filter(Boolean).map(line=>{const parts=line.split('|');need(parts.length===2,'CURRENT_FIRST_OLD_COMPILER_AUDIT_ROW',line);return {path:parts[0],sha256:parts[1].toLowerCase()};});const distinct=new Map;for(const r of rows){const old=distinct.get(key(r.path));need(!old||old.sha256===r.sha256,'CURRENT_FIRST_OLD_CONFLICTING_COMPILER_INPUT',r);distinct.set(key(r.path),r);}return [...distinct.values()];}
function fixedRuntime(p){const r=read(checked(p).path).runtimeOptions;need(r?.tfm==='net10.0'&&r.rollForward==='Disable'&&equal(r.frameworks,[{name:'Microsoft.NETCore.App',version:'10.0.11'},{name:'Microsoft.WindowsDesktop.App',version:'10.0.11'}]),'CURRENT_FIRST_OLD_FIXED_RUNTIME_REQUIRED');}


export function validateParentBuildBudget(m,{format,manifestPin}={}){
 const protocol=read(new URL('./current-first-old-contract.json',import.meta.url)).parentBuildBudgetProtocol;
 const selected=format??protocol.currentFormat;
 need(selected===protocol.currentFormat||selected===protocol.historicalFormat,'CURRENT_FIRST_OLD_PARENT_BUDGET_FORMAT',selected);
 need(m.parentBudgetMs===BUDGETS.parent&&Array.isArray(m.buildProcesses)&&m.buildProcesses.length===3&&m.buildProcesses.every(r=>r.exitCode===0&&r.signal===null&&r.error===null&&r.timedOut===false&&r.compileOnly===true&&r.targetExecuted===false&&Number.isSafeInteger(r.durationMs)&&r.durationMs>=0),'CURRENT_FIRST_OLD_PARENT_COMPILE_OR_BUDGET');
 const later=Object.hasOwn(m,'laterStages')?m.laterStages:[];
 need(Array.isArray(later)&&(!Object.hasOwn(m,'laterStages')||(m.preparedToolCount===5&&later.length===2&&new Set(later.map(s=>s.kind)).size===2&&['BootstrapFixture','JoinedFactoryProbe'].every(kind=>later.some(s=>s.kind===kind)))),'CURRENT_FIRST_OLD_PARENT_LATER_STAGE_ACCOUNTING');
 need(later.every(s=>s.process&&s.process.exitCode===0&&s.process.signal===null&&s.process.error===null&&s.process.timedOut===false&&s.process.compileOnly===true&&s.process.targetExecuted===false&&Number.isSafeInteger(s.process.durationMs)&&s.process.durationMs>=0&&s.process.budgetMs===120000&&(!Object.hasOwn(s.process,'effectiveBudgetMs')||(Number.isSafeInteger(s.process.effectiveBudgetMs)&&s.process.effectiveBudgetMs>0&&s.process.effectiveBudgetMs<=120000&&s.process.durationMs<=s.process.effectiveBudgetMs))),'CURRENT_FIRST_OLD_PARENT_LATER_STAGE_ACCOUNTING');
 const initialCompileMs=m.buildProcesses.reduce((n,r)=>n+r.durationMs,0),laterCompileMs=later.reduce((n,s)=>n+s.process.durationMs,0),compileSum=initialCompileMs+laterCompileMs;
 need(Number.isSafeInteger(compileSum)&&Number.isSafeInteger(m.preparedDurationMs)&&m.preparedDurationMs>=0&&Number.isSafeInteger(m.remainingParentBudgetMs)&&m.remainingParentBudgetMs===BUDGETS.parent-m.preparedDurationMs&&m.remainingParentBudgetMs>0,'CURRENT_FIRST_OLD_PARENT_COMPILE_OR_BUDGET');
 const historical=selected===protocol.historicalFormat;
 if(historical){
  need(!Object.hasOwn(m,'preparedCompileMs')&&!Object.hasOwn(m,'laterStages'),'CURRENT_FIRST_OLD_PARENT_HISTORICAL_FORMAT_MISMATCH');
  need(protocol.historicalReceipts.some(row=>samePin(row.manifest,manifestPin)),'CURRENT_FIRST_OLD_PARENT_HISTORICAL_RECEIPT_NOT_PINNED');checked(manifestPin);need(equal(m,read(manifestPin.path)),'CURRENT_FIRST_OLD_PARENT_HISTORICAL_DOCUMENT_MISMATCH');
  need(m.preparedDurationMs===compileSum,'CURRENT_FIRST_OLD_PARENT_COMPILE_OR_BUDGET');
 }else{
  need(Object.hasOwn(m,'preparedCompileMs')&&Number.isSafeInteger(m.preparedCompileMs)&&m.preparedCompileMs>=0&&m.preparedCompileMs===compileSum&&m.preparedDurationMs>=m.preparedCompileMs,'CURRENT_FIRST_OLD_PARENT_COMPILE_OR_BUDGET');
 }
 return {format:selected,historicalReceipt:historical,initialCompileMs,laterCompileMs,compiledToolCount:3+later.length,preparedCompileMs:historical?compileSum:m.preparedCompileMs,preparedCompileMsDerivedFromHistoricalProcesses:historical,preparedDurationMs:m.preparedDurationMs,parentBudgetMs:m.parentBudgetMs,remainingParentBudgetMs:m.remainingParentBudgetMs};
}

export function adaptParentBuildCompilation(manifestPin,input){
 const m=read(checked(manifestPin).path);noModel(m);
 need(m.schemaVersion===1&&m.kind==='ymcc-original-fan-parent-build-handoff'&&m.status==='PREPARED_INITIAL_THREE_TOOLS_COMPILED_NOT_EXECUTED','CURRENT_FIRST_OLD_PARENT_COMPILATION_SCHEMA');
 const parentBudgetAccounting=validateParentBuildBudget(m,{format:input.parentBuildBudgetFormat,manifestPin});
 for(const p of [m.owner,m.request,m.preparer,m.sourceInputBefore,m.generated,m.hc,m.cecil,...m.sourcePins,...m.toolchainPins,...m.artifacts,...m.preparationPins,...m.commandPins.map(r=>r.pin)])checked(p);
 const owner=read(m.owner.path);need(same(owner.evidenceDir,m.evidenceDir)&&same(owner.repoRoot,m.repoRoot)&&samePin(owner.preparedBy,m.preparer),'CURRENT_FIRST_OLD_PARENT_OWNER_MISMATCH');
 if(m.laterStages?.length)verifyLaterParentToolStages({repoRoot:m.repoRoot,evidenceDir:m.evidenceDir,parentOwner:m.owner,stages:m.laterStages});
 for(const stage of m.laterStages??[]){
  const expectedRoot=path.join(m.evidenceDir,stage.kind==='BootstrapFixture'?'original-bootstrap':'joined-factory'),expectedBuild=path.join(expectedRoot,stage.kind==='BootstrapFixture'?'build':'probe-build');
  need(same(stage.root,expectedRoot)&&same(stage.build,expectedBuild),'CURRENT_FIRST_OLD_PARENT_LATER_STAGE_PATH');
  // Historical pre-unification pins are verified against their archived bytes by the stage verifier.
  const {processPin,...raw}=stage.process,command=read(stage.command.path),stageOwner=read(stage.owner.path);
  need(equal(raw,read(processPin.path))&&equal(command.args,raw.args)&&samePin(command.program,raw.program)&&samePin(raw.program,m.buildProcesses[0].program)&&command.onlyCompile===true&&command.args[0]==='build','CURRENT_FIRST_OLD_PARENT_LATER_RAW_COMPILE_BINDING');
  need(stageOwner.ownerId===owner.ownerId&&same(stageOwner.root,expectedRoot)&&same(stageOwner.build,expectedBuild)&&samePin(stageOwner.parentOwner,m.owner),'CURRENT_FIRST_OLD_PARENT_LATER_OWNER_BINDING');
 }
 const tool=kind=>{const rows=m.tools.filter(t=>t.kind===kind);need(rows.length===1,'CURRENT_FIRST_OLD_PARENT_TOOL_MISSING',kind);return rows[0];},h=tool('Harness'),launcher=tool('MTA');
 need(samePin(h.entry,input.producer)&&samePin(launcher.entry,input.launcher)&&samePin(launcher.helper,input.authority)&&samePin(h.deps,input.deps)&&samePin(h.runtimeconfig,input.rc),'CURRENT_FIRST_OLD_PARENT_EXACT_ARTIFACT_MISMATCH');
 const projects=['Harness','MTA','ScopedLoadAuthority'].map(kind=>{const audit=m.compilerAudits.find(a=>a.project===kind),command=m.commandPins.find(r=>r.kind===(kind==='ScopedLoadAuthority'?'MTA':kind)),process=m.buildProcesses.find(r=>r.project===(kind==='Harness'?'Harness.csproj':'load-authority/MTA.csproj'));need(audit&&command&&process,'CURRENT_FIRST_OLD_PARENT_ACTUAL_AUDIT_MISSING',kind);for(const p of audit.inputs)checked(p);return {kind,projectName:kind==='Harness'?'Harness.csproj':'MTA.csproj',artifact:kind==='Harness'?h.entry:kind==='MTA'?launcher.entry:launcher.helper,command:command.pin,processInline:process,before:audit.before,after:audit.after,inputCount:audit.inputs.length};});
 return {schemaVersion:1,kind:'onex-current-first-old-qualified-compilation',artifacts:{producer:h.entry,launcher:launcher.entry,authority:launcher.helper,deps:h.deps,rc:h.runtimeconfig},program:m.buildProcesses[0].program,outputs:m.artifacts,projects,sourceSnapshot:m.sourceInputBefore,parentManifest:manifestPin,originalTargets:m.baseline.files,rawHc:m.hc,parentBudgetMs:m.parentBudgetMs,preparedCompileMs:parentBudgetAccounting.preparedCompileMs,preparedDurationMs:m.preparedDurationMs,remainingParentBudgetMs:m.remainingParentBudgetMs,parentBudgetAccounting};
}

// This adapter consumes actual compiler receipts; it never builds or launches a target.
export function validateCurrentCompilation(input){
 const document=read(checked(input.currentActualCompilerReceipt).path);const c=document.kind==='ymcc-original-fan-parent-build-handoff'?adaptParentBuildCompilation(input.currentActualCompilerReceipt,input):document;noModel(c);
 need(c.schemaVersion===1&&c.kind==='onex-current-first-old-qualified-compilation','CURRENT_FIRST_OLD_ACTUAL_COMPILER_RECEIPT_REQUIRED');
 if(!c.parentManifest){need(c.originGraph&&c.originalBuild&&c.auxiliaryBuild,'CURRENT_FIRST_OLD_COMPILATION_ORIGINS_REQUIRED');const graph=read(checked(c.originGraph).path),build=read(checked(c.originalBuild).path),aux=read(checked(c.auxiliaryBuild).path);need(samePin(graph.fullSourceSetBefore,c.sourceSnapshot)&&samePin(build.sourceBefore,c.sourceSnapshot),'CURRENT_FIRST_OLD_COMPILER_SNAPSHOT_RELABEL_FORBIDDEN');need(build.tools.some(t=>t.kind==='Harness'&&samePin(t.entry,input.producer))&&aux.results.some(t=>t.kind==='MTA'&&samePin(t.entry,input.launcher)),'CURRENT_FIRST_OLD_COMPILER_ORIGINAL_ARTIFACT_BINDING');}
 for(const [field,wanted] of Object.entries({producer:input.producer,launcher:input.launcher,authority:input.authority,deps:input.deps,rc:input.rc}))need(samePin(c.artifacts[field],wanted),'CURRENT_FIRST_OLD_COMPILED_ARTIFACT_MISMATCH',{field});
 need(samePin(c.program,input.dotnet),'CURRENT_FIRST_OLD_COMPILER_PROGRAM_MISMATCH');checked(c.program);
 need(Array.isArray(c.outputs)&&c.outputs.length>=7,'CURRENT_FIRST_OLD_COMPILER_OUTPUTS_REQUIRED');unique(c.outputs,'CURRENT_FIRST_OLD_DUPLICATE_OUTPUT');for(const p of c.outputs)checked(p);
 for(const p of [input.producer,input.launcher,input.authority,input.deps,input.rc])need(c.outputs.some(r=>samePin(r,p)),'CURRENT_FIRST_OLD_ARTIFACT_NOT_COMPILER_BOUND',p);
 need(Array.isArray(c.projects)&&['Harness','MTA','ScopedLoadAuthority'].every(k=>c.projects.some(r=>r.kind===k)),'CURRENT_FIRST_OLD_THREE_COMPILER_GRAPHS_REQUIRED');
 const allInputs=[];
 for(const project of c.projects){
  const cmd=read(checked(project.command).path),process=project.processInline??read(checked(project.process).path);
  need((process.code??process.exitCode)===0&&process.signal===null&&process.error===null&&process.timedOut===false,'CURRENT_FIRST_OLD_COMPILE_NOT_NATURAL',project.kind);
  need(samePin(cmd.program,input.dotnet)&&Array.isArray(cmd.args)&&cmd.args[0]==='build'&&cmd.args.some(x=>typeof x==='string'&&x.includes(project.projectName)),'CURRENT_FIRST_OLD_COMPILER_COMMAND_BINDING',project.kind);
  const artifactsIndex=cmd.args.indexOf('--artifacts-path');need(artifactsIndex>0&&path.isAbsolute(cmd.args[artifactsIndex+1])&&inside(cmd.args[artifactsIndex+1],project.artifact.path),'CURRENT_FIRST_OLD_COMPILER_ARTIFACT_PATH');
  need(c.outputs.some(p=>samePin(p,project.artifact)),'CURRENT_FIRST_OLD_PROJECT_ARTIFACT_NOT_PINNED',project.kind);
  const before=auditRows(project.before),after=auditRows(project.after);need(before.length===project.inputCount&&after.length===before.length,'CURRENT_FIRST_OLD_COMPILER_INPUT_COUNT',project.kind);
  const b=new Map(after.map(p=>[key(p.path),p.sha256]));for(const p of before){need(b.get(key(p.path))===p.sha256,'CURRENT_FIRST_OLD_COMPILER_INPUT_CHANGED_DURING_BUILD',p);checked(p);allInputs.push(p);}
  need(before.some(p=>/\.cs$/i.test(p.path))&&before.some(p=>/\.dll$/i.test(p.path)),'CURRENT_FIRST_OLD_COMPILER_SOURCE_AND_REFERENCE_GRAPH',project.kind);
 }
 const snapshot=read(checked(c.sourceSnapshot).path);need(Array.isArray(snapshot.sources)&&(snapshot.sourceCount??snapshot.sources.length)===snapshot.sources.length,'CURRENT_FIRST_OLD_SOURCE_SNAPSHOT_REQUIRED');
 const current=read(checked(input.currentSourceSnapshot).path);need(Array.isArray(current.sources)&&(current.sourceCount??current.sources.length)===current.sources.length,'CURRENT_FIRST_OLD_CURRENT_SOURCE_SET_REQUIRED');
 const original=new Map(snapshot.sources.map(p=>[key(p.path),p]));unique(current.sources,'CURRENT_FIRST_OLD_DUPLICATE_CURRENT_SOURCE');for(const p of current.sources)checked(p);
 const changes=[];for(const p of current.sources){const old=original.get(key(p.path));if(!old||old.sha256!==p.sha256)changes.push({kind:old?'changed':'added',before:old??null,after:p});original.delete(key(p.path));}for(const p of original.values())changes.push({kind:'removed',before:p});
 return {receipt:c,sourceCountAtCompilation:snapshot.toolSourceCount??snapshot.sourceCount,currentSourceCount:current.toolSourceCount??current.sourceCount,currentSourceSetVerified:changes.length===0,changes,allInputs};
}

async function originals(input,contract){
 const o=input.pinnedOriginals;
 need(o&&samePin(o.stage,contract.originalStage)&&samePin(o.stageModule,contract.stageModule),'CURRENT_FIRST_OLD_PINNED_ORIGINALS_NOT_ACCEPTED');
 const stage=read(checked(o.stage).path),invocation=read(checked(stage.invocation).path),policy=read(checked(invocation.policy).path),review=read(checked(stage.review).path);
 const preload=read(checked(stage.preloadReceipt).path),preloadStep=read(checked(stage.preloadStep).path),preloadProcess=read(checked(stage.preloadProcess).path),argvMetadata=read(checked(invocation.argumentMetadata).path),frameworkMetadata=read(checked(stage.frameworkMetadata).path);
 const old=await import(pathToFileURL(checked(o.stageModule).path));
 old.validateStageDocuments({stage,invocation,policy,review,preload,preloadStep,preloadProcess,argvMetadata,frameworkMetadata});
 need(old.BUSINESS_BUDGET_MS===BUDGETS.business,'CURRENT_FIRST_OLD_LEGACY_BUDGET_CHANGED');
 const business=read(checked(contract.originalBusiness).path),launch=read(checked(contract.originalLaunch).path),processReceipt=read(checked(contract.originalProcess).path);
 const allowance=old.deriveManagedAllowance({policy,preload,invocation,frameworkMetadata});const assessment=old.assessBusinessResult({business,launch,processReceipt,allowance,invocation});need(assessment.passed,'CURRENT_FIRST_OLD_RECORDED_ORIGINAL_NOT_ACCEPTED',assessment);
 // The original outer failure is evidence too: never rewrite it as a successful current run.
 checked(contract.originalOuterFailure);checked(contract.originalAcceptedAudit);
 for(const p of [...policy.metadata,...policy.contexts.flatMap(c=>c.managed),...review.metadataReports,...review.images])checked(p);
 return {stage,invocation,policy,review,business,preloadStep,assessment,baseClrIdentities:old.BASE_CLR_IDENTITIES};
}

function typedBoundary(input,old,replacements,currentArgvPrefix){
 if(!input.typedEquivalenceProof)return {proven:false,blockers:['EXACT_OLD_TO_CURRENT_TYPED_NATIVE_NOIO_PROOF_MISSING']};
 const proof=read(checked(input.typedEquivalenceProof).path);noModel(proof);
 need(proof.schemaVersion===1&&proof.kind==='onex-current-first-old-typed-native-equivalence'&&samePin(proof.originalReview,old.stage.review)&&samePin(proof.compilation,input.currentActualCompilerReceipt),'CURRENT_FIRST_OLD_TYPED_PROOF_SCOPE');
 need(equal(proof.currentArgvPrefix,currentArgvPrefix),'CURRENT_FIRST_OLD_TYPED_CURRENT_ARGV');
 need(equal(proof.originalArgvPrefix,old.invocation.firstOldArgs.slice(0,4)),'CURRENT_FIRST_OLD_TYPED_PROOF_ARGV');
 const raw=read(checked(proof.rawBoundaryReport).path);noModel(raw);
 need(raw.kind==='onex-first-old-closed-typed-native-frontier'&&raw.status==='REVIEWED_EXACT_FIRST_OLD_NOIO'&&Array.isArray(raw.methods)&&raw.methods.length>=old.review.reviewedMethodBodies&&Array.isArray(raw.openMethodEdges)&&raw.openMethodEdges.length===0,'CURRENT_FIRST_OLD_TYPED_FRONTIER_OPEN');
 need(Array.isArray(proof.bindings)&&replacements.every(r=>proof.bindings.some(b=>samePin(b.before,r.before)&&samePin(b.after,r.after))),'CURRENT_FIRST_OLD_TYPED_REPLACEMENT_UNPROVEN');
 for(const field of ['cecilMetadataOnly','ioDenyguardPreserved','originalAssertionsPreserved','originalFailureMaskPreserved','reflectionBindingsClosed','nativeBoundaryComplete','syntheticFixtureOnly'])need(raw[field]===true,'CURRENT_FIRST_OLD_TYPED_BOUNDARY_INCOMPLETE',field);
 for(const field of ['hostMainInvoked','hostOptionsCtorInvoked','rawHcClrLoadPermitted','hostFactoryStartPermitted','realManagerStartPermitted','osSleepPermitted','realDeviceIoPermitted','installationPermitted'])need(raw[field]===false,'CURRENT_FIRST_OLD_TYPED_UNSAFE_BRANCH',field);
 need(Array.isArray(raw.subjects)&&replacements.every(r=>raw.subjects.some(p=>samePin(p,r.after))),'CURRENT_FIRST_OLD_TYPED_SUBJECT_MISMATCH');for(const p of raw.subjects)checked(p);
 need(Array.isArray(raw.metadataReports)&&raw.metadataReports.length>0,'CURRENT_FIRST_OLD_TYPED_RAW_METADATA_REQUIRED');for(const p of raw.metadataReports)checked(p);
 return {proven:true,proof:input.typedEquivalenceProof,rawReport:proof.rawBoundaryReport,blockers:[]};
}

export async function prepareCurrentFirstOld(input){
 need(input?.schemaVersion===1&&input.mode==='prepare-only','CURRENT_FIRST_OLD_PREPARE_ONLY');noModel(input);
 const contract=read(new URL('./current-first-old-contract.json',import.meta.url));
 for(const f of ['producer','launcher','authority','deps','rc','dotnet','currentActualCompilerReceipt','currentSourceSnapshot'])checked(input[f]);
 need(path.basename(input.producer.path)==='OneXFanCoexistence.dll'&&path.basename(input.launcher.path)==='MTA.dll'&&path.basename(input.authority.path)==='ScopedLoadAuthority.dll','CURRENT_FIRST_OLD_ENTRY_NAMES');
 need(same(input.deps.path,input.producer.path.replace(/\.dll$/i,'.deps.json'))&&same(input.rc.path,input.producer.path.replace(/\.dll$/i,'.runtimeconfig.json')),'CURRENT_FIRST_OLD_PRODUCER_COMPANION_PATHS_REQUIRED');
 need(same(input.authority.path,path.join(path.dirname(input.launcher.path),'ScopedLoadAuthority.dll')),'CURRENT_FIRST_OLD_ACTUAL_LAUNCHER_HELPER_REQUIRED');
 need(input.dotnet.sha256===contract.dotnetSha256,'CURRENT_FIRST_OLD_DOTNET_NOT_REVIEWED');fixedRuntime(input.rc);fixedRuntime({path:input.launcher.path.replace(/\.dll$/i,'.runtimeconfig.json'),sha256:fileHash(input.launcher.path.replace(/\.dll$/i,'.runtimeconfig.json'))});need(!fs.existsSync(input.launcher.path.replace(/\.dll$/i,'.deps.json')),'CURRENT_FIRST_OLD_MTA_DEPS_MUST_BE_ABSENT');
 need(typeof input.ownedRoot==='string'&&path.isAbsolute(input.ownedRoot),'CURRENT_FIRST_OLD_OWNED_ROOT_REQUIRED');plain(input.ownedRoot);
 const owner=path.resolve(input.ownedRoot),policyOwner=path.resolve(input.evidenceRoot??owner),out=path.resolve(input.preparationDir??path.join(policyOwner,'current-first-old-policy'));plain(policyOwner);plain(out);need(inside(policyOwner,out)&&!same(policyOwner,out),'CURRENT_FIRST_OLD_PREPARATION_OUTSIDE_OWNER');need(!fs.existsSync(out),'CURRENT_FIRST_OLD_FRESH_OUTPUT_ROOT_REQUIRED');
 const old=await originals(input,contract),compiled=validateCurrentCompilation(input);
 const historicalRoots=[path.dirname(input.pinnedOriginals.stage.path),...old.policy.contexts.flatMap(c=>c.managed).map(r=>path.dirname(r.path)),contract.readonlySourceRoot];
 need(historicalRoots.every(root=>!inside(root,out)&&!inside(out,root)),'CURRENT_FIRST_OLD_OUTPUT_OVERLAPS_INPUT');
 const replacements=[];const change=(before,after,role)=>{checked(before);checked(after);replacements.push({role,before:{path:before.path,sha256:before.sha256},after:{path:after.path,sha256:after.sha256}});return {...before,path:after.path,sha256:after.sha256};};
 change(old.invocation.launcher,input.launcher,'MTA');
 const q=structuredClone(old.policy);q.ownedRoot=owner;q.entry=input.producer.path;
 const oldCustom=q.contexts.find(c=>c.kind==='custom'),targets=input.pinnedOriginals.targets??compiled.receipt.originalTargets;
 if(targets){need(Array.isArray(targets)&&targets.length===oldCustom.managed.length,'CURRENT_FIRST_OLD_EXACT_ORIGINAL_TARGET_SET');for(let n=0;n<oldCustom.managed.length;n++){const before=oldCustom.managed[n],after=targets.find(p=>path.basename(p.path)===path.basename(before.path));need(after&&after.sha256===before.sha256,'CURRENT_FIRST_OLD_ORIGINAL_TARGET_BYTES_CHANGED',before);oldCustom.managed[n]=change(before,after,'pinned-original:'+before.identity.split(',')[0]);}const oldId=oldCustom.id;oldCustom.id='OldBinaryProbe.Context|'+path.dirname(oldCustom.managed.find(p=>path.basename(p.path)==='YeManFanHost.dll').path).toUpperCase();for(const c of q.contexts)for(const r of c.requests)if(r.targetContext===oldId)r.targetContext=oldCustom.id;}
 const rawHc=input.pinnedOriginals.rawHc??compiled.receipt.rawHc??old.policy.metadata.find(p=>p.sha256===contract.rawHcSha256);checked(rawHc);need(rawHc.sha256===contract.rawHcSha256,'CURRENT_FIRST_OLD_RAW_HC_BYTES_CHANGED');
 const defaultContext=q.contexts.find(c=>c.id==='Default');
 for(let n=0;n<defaultContext.managed.length;n++){
  const before=defaultContext.managed[n],name=before.identity.split(',')[0];let after;
  if(name==='OneXFanCoexistence')after=input.producer;
  else if(name==='ScopedLoadAuthority')after=input.authority;
  else if(before.role!=='framework'){
   const candidate=path.join(path.dirname(input.producer.path),path.basename(before.path));after=compiled.receipt.outputs.find(p=>same(p.path,candidate));need(after,'CURRENT_FIRST_OLD_RUNTIME_MEMBER_NOT_COMPILER_BOUND',candidate);
   if(['Mono.Cecil','Newtonsoft.Json'].includes(name))need(after.sha256===before.sha256,'CURRENT_FIRST_OLD_ORDINARY_LIBRARY_BYTES_CHANGED',name);
  }
  if(after)defaultContext.managed[n]=change(before,after,name);
 }
 for(const context of q.contexts){
  if(context.kind==='custom'){need(context.registrationType==='OldBinaryProbe+Context','CURRENT_FIRST_OLD_CUSTOM_OWNER');context.registrationAssembly={...context.registrationAssembly,path:input.producer.path,sha256:input.producer.sha256};}
  for(const request of context.requests){const r=replacements.find(r=>same(r.before.path,request.targetPath));if(r)request.targetPath=r.after.path;}
 }
 unique(q.contexts.flatMap(c=>c.managed),'CURRENT_FIRST_OLD_DUPLICATE_MANAGED_PATH');
 const resultPath=path.resolve(input.resultPath??path.join(owner,'old-binary-result.json')),launchPath=path.resolve(input.launchReceipt??path.join(owner,'first-old-launch-receipt.json'));for(const p of [resultPath,launchPath]){plain(p);need(inside(owner,p)&&!same(owner,p)&&!fs.existsSync(p),'CURRENT_FIRST_OLD_FRESH_OWNED_RESULT_REQUIRED',p);}need(!same(resultPath,launchPath),'CURRENT_FIRST_OLD_OUTPUT_ALIAS');
 const args=['old',oldCustom.managed.find(p=>path.basename(p.path)==='YeManFanHost.dll').path,oldCustom.managed.find(p=>path.basename(p.path)==='YeManLightSetter.dll').path,rawHc.path,resultPath];
 const boundary=typedBoundary(input,old,replacements,args.slice(0,4));
 need(!q.contexts.flatMap(c=>c.managed).some(p=>p.sha256===contract.rawHcSha256),'CURRENT_FIRST_OLD_RAW_HC_LOAD_FORBIDDEN');
 // Entry artifacts keep their actual compiler locations, including future parent out/test-build.
 // Only policy documents and result destinations belong to ownedRoot.
 fs.mkdirSync(out,{recursive:true});const written=[];
 const write=(name,value)=>{const file=path.join(out,name);fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});const p=pin(file);written.push(p);return p;};
 const sourceBinding=write('source-binding.json',{schemaVersion:1,kind:'onex-current-first-old-source-binding',producer:input.producer,launcher:input.launcher,authority:input.authority,compilation:input.currentActualCompilerReceipt,parentBudgetAccounting:compiled.receipt.parentBudgetAccounting??null,sourceSnapshot:compiled.receipt.sourceSnapshot,currentSourceSnapshot:input.currentSourceSnapshot,sourceCountAtCompilation:compiled.sourceCountAtCompilation,currentSourceCount:compiled.currentSourceCount,currentSourceSetVerified:compiled.currentSourceSetVerified,changes:compiled.changes,oldStage:input.pinnedOriginals.stage,replacements,typedBoundary:boundary,legacySourceSnapshotIsNotCurrent:!compiled.currentSourceSetVerified});
 const argv=write('first-old-argv.json',{schemaVersion:1,kind:'Harness',producer:input.producer.path,args,argvSha256:createHash('sha256').update(JSON.stringify(args)).digest('hex'),executed:false,expectedBusinessExit:0,output:args.at(-1)});
 q.metadata=q.metadata.map(p=>p.sha256===contract.rawHcSha256?rawHc:same(p.path,old.invocation.argumentMetadata.path)?argv:/OneXFanCoexistence\.deps\.json$/i.test(p.path)?input.deps:p);
 q.metadata.push(sourceBinding,input.currentActualCompilerReceipt,input.currentSourceSnapshot,...(input.typedEquivalenceProof?[input.typedEquivalenceProof]:[]));unique(q.metadata,'CURRENT_FIRST_OLD_DUPLICATE_METADATA');for(const p of q.metadata)checked(p);
 const policy=write('POLICY.json',q),metadataResult=path.join(out,'metadata-result.json'),launchResult=launchPath;
 const preloadTool=input.preloadTool??old.preloadStep.tool;checked(preloadTool);
 const blockers=[...boundary.blockers,...(!compiled.currentSourceSetVerified?['COMPILATION_SOURCE_SET_DIFFERS_FROM_CURRENT_SOURCE_SET']:[]),'CURRENT_HARNESS_MTA_DEFAULT_METADATA_ADMISSION_PENDING','CURRENT_DEFAULT_PRELOAD_ADMISSION_PENDING'];
 if(!input.preloadTool)blockers.push('LEGACY_PRELOAD_TOOL_AUTHORITY_SHA_LOCK_REQUIRES_SEPARATE_CURRENT_TOOL_REVIEW');
 const result={schemaVersion:1,kind:'onex-current-first-old-prepared',status:'SOURCE_BOUND_PREPARED_NOT_EXECUTED_ADMISSION_PENDING',sourceBound:true,currentSourceSetVerified:compiled.currentSourceSetVerified,ownedRoot:owner,preparationDir:out,program:input.dotnet,producer:input.producer,launcher:input.launcher,authority:input.authority,deps:input.deps,rc:input.rc,policy,argumentMetadata:argv,sourceBinding,compilation:input.currentActualCompilerReceipt,parentBudgetAccounting:compiled.receipt.parentBudgetAccounting??null,typedBoundary:boundary,budgets:BUDGETS,args,resultPath:args.at(-1),launchReceipt:launchResult,metadataArguments:[input.launcher.path,'validate',policy.path,policy.sha256,metadataResult],preloadArguments:[preloadTool.path,'check-default-metadata',policy.path,policy.sha256,input.authority.path,input.authority.sha256,path.join(out,'preload-result.json')],preloadTool,preloadExecutable:false,businessArguments:[input.launcher.path,'run',policy.path,policy.sha256,launchResult,...args],originalContract:{stage:input.pinnedOriginals.stage,policy:old.invocation.policy,review:old.stage.review,minimumCases:8,minimumChecks:63,recordedCases:old.business.cases.map(c=>c.id??c.name),recordedCaseMask:old.business.cases.map(c=>({id:c.id,status:c.status})),recordedChecks:old.business.checks.map(c=>c.name??c.id),baseClrIdentities:old.baseClrIdentities,currentBaseClrAdmissionPending:true,syntheticFixture:{before:replacements.find(r=>r.role==='HandheldCompanion').before,after:replacements.find(r=>r.role==='HandheldCompanion').after,identity:'HandheldCompanion, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'},ioDenyguardRequired:true,cecilMetadataOnlyRequired:true,expectedStatus:'DEFECT_REPRODUCED_IN_MEMORY',nativeAttempts:0,allChecksRequired:true,rawHcMetadataOnly:true,originalOuterFailure:contract.originalOuterFailure},blockers,targetExecutions:0,producerExecuted:false,passed:false,executionApprovalGranted:false,fullHostApproved:false,fullFan360Passed:false};
 for(const p of [...compiled.receipt.outputs,...compiled.allInputs,...q.contexts.flatMap(c=>c.managed),...q.metadata])checked(p);
 const plan=write('PREPARED-PLAN.json',result);return {...result,plan,written};
}

if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){
 try{const a=process.argv.slice(2);need(a.length===4&&a[0]==='--input'&&a[2]==='--sha256','CURRENT_FIRST_OLD_EXACT_CLI');const p={path:a[1],sha256:a[3]};const result=await prepareCurrentFirstOld(read(checked(p).path));console.log(JSON.stringify(result,null,2));}
 catch(e){console.error(JSON.stringify({status:'BLOCKED',code:e.code??null,detail:e.detail??null,error:e.message,targetExecutions:0,passed:false}));process.exitCode=3;}
}
