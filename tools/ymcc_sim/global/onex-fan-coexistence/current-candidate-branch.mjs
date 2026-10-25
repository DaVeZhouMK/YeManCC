import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

// Read-only selected metadata/source audit. This module has no process launcher,
// target loader, policy generator or resolver fallback.
export const CURRENT_CANDIDATE_BINDINGS=Object.freeze({
 host:'2b59965fce6909b1a85058e1cf670477090ed858d64eda5484a2fda38eec484a',
 backend:'2a3ae2d1fa99c382841dfa6ee690ac3c3c9baf429b726b76a9c67c2d6fcfe236',
 producer:'fe96c6a564e74a1bafcfe4161f1f23f4d84ecaab26b1e510fe332217c6dd4846',
 synthetic:'6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6',
 light:'e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf',
 rawHc:'08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'
});
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const need=(ok,code,detail)=>{if(!ok)throw Object.assign(new Error(code),{code,detail});};
const key=p=>path.resolve(p).toUpperCase();
export function checkedCurrentCandidatePin(p){
 need(p&&path.isAbsolute(p.path??'')&&/^[a-f0-9]{64}$/.test(p.sha256??''),'CANDIDATE_PIN_SHAPE',p);
 need(fs.existsSync(p.path)&&fs.statSync(p.path).isFile(),'CANDIDATE_PIN_MISSING',p);
 need(key(fs.realpathSync(p.path))===key(p.path)&&!fs.lstatSync(p.path).isSymbolicLink(),'CANDIDATE_PIN_REDIRECTED',p);
 need(sha(p.path)===p.sha256,'CANDIDATE_PIN_CHANGED',p);return p;
}
const localName=t=>t.split(' [scope=')[0];
export function reviewCurrentCandidateDocuments({input,basis,reports,sources}){
 need(input.schemaVersion===1&&input.kind==='onex-current-candidate-terminal-review'&&input.mode==='metadata-only','CANDIDATE_REVIEW_SCHEMA');
 need(input.invocation.mode==='default-four-arguments'&&input.invocation.argumentCount===4&&input.invocation.budgetMs===60000,'CANDIDATE_DEFAULT60_CONTRACT');
 for(const [role,hash] of Object.entries(CURRENT_CANDIDATE_BINDINGS))need(input.images[role]?.sha256===hash,'CANDIDATE_CURRENT_IMAGE_REQUIRED',role);
 need(basis.hostSha256===CURRENT_CANDIDATE_BINDINGS.host&&basis.backendSha256===CURRENT_CANDIDATE_BINDINGS.backend,'CANDIDATE_BASIS_VERSION');
 need(reports.length===basis.reportCount&&reports.length<=12,'CANDIDATE_BOUNDED_REPORT_SET');
 const methods=new Map(),types=new Map();
 for(const r of reports){
  need(r.status==='PASS_RAW_SELECTED_METHOD_READ'&&r.failure===null&&r.targetAssembliesObserved===false&&r.targetCodeExecuted===false&&r.targetConstructorsInvoked===false&&r.externalResolutionAllowed===false,'CANDIDATE_RAW_METADATA_ONLY');
  for(const image of r.rows){
   need(image.moduleInitializers.length===0,'CANDIDATE_MODULE_INITIALIZER');
   need(Object.values(input.images).some(p=>p.sha256===image.sha256),'CANDIDATE_UNREVIEWED_IMAGE');
   for(const t of image.selections){const tk=image.sha256+'|'+t.type,oldType=types.get(tk);if(oldType)need(JSON.stringify(oldType.fields)===JSON.stringify(t.fields)&&oldType.baseType===t.baseType,'CANDIDATE_CONFLICTING_TYPE',t.type);types.set(tk,t);
    for(const m of t.methods){const mk=image.sha256+'|'+m.token,old=methods.get(mk);need(!old||JSON.stringify(old.method)===JSON.stringify(m),'CANDIDATE_CONFLICTING_METHOD',m.method);methods.set(mk,{image,type:t.type,method:m});}
   }
  }
 }
 const find=(role,part)=>{const v=[...methods.values()].filter(x=>x.image.sha256===input.images[role].sha256&&x.method.method.includes(part));need(v.length===1,'CANDIDATE_METHOD_MISSING_OR_AMBIGUOUS',part);return v[0].method;};
 const requiredType=(name)=>{const t=types.get(input.images.synthetic.sha256+'|'+name);need(t,'CANDIDATE_FIXTURE_TYPE_MISSING',name);return t;};
 for(const req of basis.fixtureTypes){const t=requiredType(req.type);need(t.baseType===req.baseType,'CANDIDATE_FIXTURE_BASE_MISMATCH',req.type);for(const field of req.fields)need(t.fields.some(f=>f.name===field.name&&f.type===field.type&&f.isStatic===field.isStatic),'CANDIDATE_FIXTURE_FIELD_MISMATCH',field);}
 for(const name of basis.fixtureMethods)need([...methods.values()].some(x=>x.image.sha256===input.images.synthetic.sha256&&x.method.method===name&&!x.method.isPInvoke),'CANDIDATE_FIXTURE_MEMBER_MISSING',name);
 const init=find('host','::<LoadAssemblyAndFactory>b__574_6(');
 need(JSON.stringify(init.instructions.map(i=>({opcode:i.opcode,operand:i.operand})))===JSON.stringify(basis.initializationInstructions),'CANDIDATE_INITIALIZE_FALSE_FALSE_CHANGED');
 const ctor=find('host','HWiNFOTemperatureMonitor::.ctor(');
 need(!ctor.instructions.some(i=>i.opcode==='stfld'&&/::(timer|cachedMapping|cachedMappingView) /.test(i.operand))&&!ctor.calls.some(c=>/Timer::|MemoryMappedFile::/.test(c.target)),'CANDIDATE_FRESH_MONITOR_RESOURCE_INTRUSION');
 const watchdog=find('host','::RequestPowerProfileWatchdogTick(');
 need(watchdog.instructions[1]?.operand.includes('::powerProfileWatchdog ')&&watchdog.instructions[2]?.opcode==='brtrue.s'&&watchdog.instructions[3]?.opcode==='ret','CANDIDATE_NULL_WATCHDOG_RETURN_CHANGED');
 const profile=find('host','::BuildPowerProfile(');
 need(!profile.calls.some(c=>/::(CaptureHcProfileTemplate|IsPowerLineOnline)\(/.test(c.target)),'CANDIDATE_REAL_POWER_READ_INTRUSION');
 const mark=find('host','::MarkReopenRequiredLocked(');
 need(!mark.calls.some(c=>/::(ScheduleRecoveryRetry|TryRequestControlledHostReplacementLocked)\(/.test(c.target)),'CANDIDATE_REOPEN_MARK_SCHEDULE_INTRUSION');
 const forbiddenProducer=['CaptureHcProfileTemplate','CaptureOemBaseline','ApplyCurveCore','EnsureCpuTemperatureLoop','EnsurePowerProfileWatchdog','MarkReopenRequired','recoveryReopenRequired','TryRequestControlledHostReplacementLocked'];
 const producer=[...methods.values()].filter(x=>x.image.sha256===input.images.producer.sha256&&!x.method.method.includes('::Main('));
 for(const row of producer)need(!row.method.instructions.some(i=>forbiddenProducer.some(n=>i.operand.includes(n))),'CANDIDATE_DEFAULT_PRODUCER_INTRUSION',row.method.method);
 const fixtureTerminals=[...methods.values()].filter(x=>x.image.sha256===input.images.synthetic.sha256);
 for(const row of fixtureTerminals)need(!row.method.isPInvoke&&!row.method.calls.some(c=>/RegistryKey::|Registry::|Process::Start|MemoryMappedFile::OpenExisting|WinRT|HidLibrary/.test(c.target)),'CANDIDATE_FIXTURE_EXTERNAL_IO',row.method.method);
 for(const witness of basis.sourceWitnesses){const source=sources[witness.role];need(typeof source==='string','CANDIDATE_SOURCE_ROLE_MISSING',witness.role);need(source.includes(witness.text),'CANDIDATE_SOURCE_GUARD_CHANGED',witness.id);}
 for(const p of basis.methodPins){const row=methods.get(input.images[p.role].sha256+'|'+p.token);need(row&&row.method.method===p.method&&row.method.instructionListingSha256===p.instructionListingSha256,'CANDIDATE_REVIEWED_IL_CHANGED',p.method);}
 return {schemaVersion:1,status:'SELECTED_DEFAULT_TERMINALS_CLOSED_STATIC',canProceedToParentControlledInvocation:true,
  decisionScope:'Exact current default four-argument candidate, synthetic HC, owned paths, parent-reviewed policy/validate/preload and unchanged 60000ms gate',
  imageBindings:input.images,invocation:input.invocation,claims:basis.claims,timerStateClasses:basis.timerStateClasses,
  reviewedMethodPins:basis.methodPins.length,rawUniqueMethods:methods.size,producerUniqueMethods:[...methods.values()].filter(x=>x.image.sha256===input.images.producer.sha256).length,fixtureSelectedMethods:fixtureTerminals.length,
  definiteDeviceRegistryForeignProcessIoRisks:[],requiredReflectionIncompatibilities:[],
  parentMustBind:['All images and current55 source bytes','Exact four argv, fresh owned result/auth/log root','MTA validate and Default44 receipt','Unknown dynamic requests reject; raw HC metadata-only; synthetic HC runtime-only','Natural original candidate exit/result/assertions and posthash after separately authorized parent run'],
  exclusions:basis.exclusions,targetAssembliesLoaded:0,targetExecuted:false,policyGenerated:false,executionApprovalGranted:false,fullHostApproved:false,businessPassed:false};
}

export function bindCurrentCandidatePlan({plan,policy,input}){
 need(plan.kind==='onex-current-source-candidate-prepared-invocation'&&plan.status==='PREPARED_NOT_EXECUTED','CANDIDATE_PARENT_PREPARED_PLAN_REQUIRED');
 for(const role of ['host','backend','light','rawHc','producer'])need(plan[role]?.sha256===CURRENT_CANDIDATE_BINDINGS[role],'CANDIDATE_PARENT_IMAGE_CHANGED',role);
 need(plan.gate?.candidateBudgetMs===60000&&plan.gate.parentBudgetMs===360000&&plan.gate.cases===123&&plan.gate.negativeCases===119&&plan.gate.nativeAttempts===0&&JSON.stringify(plan.gate.portGroups)==='[7,2,3]','CANDIDATE_ORIGINAL_GATE_CHANGED');
 need(plan.args?.length===4&&key(plan.args[0])===key(plan.host.path)&&key(plan.args[1])===key(plan.light.path)&&key(plan.args[2])===key(plan.rawHc.path)&&key(plan.args[3])===key(plan.businessResult),'CANDIDATE_EXACT_FOUR_ARGV');
 need(key(path.dirname(plan.host.path))===key(path.dirname(plan.backend.path)),'CANDIDATE_HOST_BACKEND_ROOT_SPLIT');
 const inside=(root,p)=>{const r=path.relative(root,p);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
 need(inside(plan.ownedRoot,plan.businessResult)&&inside(plan.ownedRoot,plan.launchReceipt),'CANDIDATE_OWNED_OUTPUT_REQUIRED');
 need(key(policy.entry)===key(plan.producer.path)&&key(policy.ownedRoot)===key(plan.ownedRoot),'CANDIDATE_POLICY_PLAN_LOCATION');
 need(policy.contexts?.length===3,'CANDIDATE_THREE_CONTEXTS_REQUIRED');const def=policy.contexts.find(c=>c.id==='Default');
 need(def?.kind==='default'&&def.managed.length===44,'CANDIDATE_DEFAULT44_REQUIRED');
 const hc=def.managed.filter(p=>p.identity.startsWith('HandheldCompanion,'));need(hc.length===1&&hc[0].sha256===CURRENT_CANDIDATE_BINDINGS.synthetic,'CANDIDATE_RUNTIME_SYNTHETIC_ONLY');
 for(const [roles,count] of [[['host','backend'],2],[['light'],1]]){
  const id='Harness.ProductContext|'+key(path.dirname(plan[roles[0]].path)),ctx=policy.contexts.find(c=>c.id===id);
  need(ctx?.kind==='custom'&&ctx.registrationType==='Harness+ProductContext'&&ctx.managed.length===count&&key(ctx.registrationAssembly?.path??'')===key(plan.producer.path)&&ctx.registrationAssembly?.sha256===plan.producer.sha256,'CANDIDATE_EXACT_CUSTOM_CONTEXT',id);
  for(const role of roles)need(ctx.managed.some(p=>key(p.path)===key(plan[role].path)&&p.sha256===plan[role].sha256&&p.identity===plan[role].identity),'CANDIDATE_CUSTOM_IMAGE_PIN',role);
 }
 need(!policy.contexts.flatMap(c=>c.managed).some(p=>p.sha256===CURRENT_CANDIDATE_BINDINGS.rawHc),'CANDIDATE_RAW_HC_RUNTIME_FORBIDDEN');
 need(policy.metadata.some(p=>key(p.path)===key(plan.rawHc.path)&&p.sha256===CURRENT_CANDIDATE_BINDINGS.rawHc),'CANDIDATE_RAW_HC_METADATA_PIN');
 return {plan:input.parentPlan,policy:plan.policy,gate:plan.gate,args:plan.args,producer:plan.producer,defaultPins:44,customPins:[2,1],ownedRoot:plan.ownedRoot,fullHostApproved:false};
}

export function loadCurrentCandidateReviewDocuments(inputPin){
 checkedCurrentCandidatePin(inputPin);const input=read(inputPin.path);checkedCurrentCandidatePin(input.basis);const basis=read(input.basis.path);
 for(const p of Object.values(input.images))checkedCurrentCandidatePin(p);
 for(const p of input.reportPins)checkedCurrentCandidatePin(p);
 checkedCurrentCandidatePin(input.sourceCandidate);const candidate=read(input.sourceCandidate.path);
 need(candidate.host.sha256===CURRENT_CANDIDATE_BINDINGS.host&&candidate.backend.sha256===CURRENT_CANDIDATE_BINDINGS.backend&&candidate.sourceFileCount===55&&candidate.sourceBound===true,'CANDIDATE_SOURCE_BOUND_CURRENT55');
 checkedCurrentCandidatePin(input.currentBuildInputs);const build=read(input.currentBuildInputs.path);need(build.sourcePins.length===55,'CANDIDATE_CURRENT55_COUNT');for(const p of build.sourcePins)checkedCurrentCandidatePin(p);
 const sources={};for(const [role,p] of Object.entries(input.sources)){checkedCurrentCandidatePin(p);if(p.compilerSource){checkedCurrentCandidatePin(p.compilerSource);need(p.sha256===p.compilerSource.sha256,'CANDIDATE_COMPILED_SOURCE_MISMATCH',role);}sources[role]=fs.readFileSync(p.path,'utf8');}
 checkedCurrentCandidatePin(input.originalCompileCommands);const commands=read(input.originalCompileCommands.path);need(commands.length===6&&commands.every(c=>c.exitCode===0&&c.naturalExit===true),'CANDIDATE_ORIGINAL_SIX_COMPILE_RECEIPTS');
 checkedCurrentCandidatePin(input.parentPlan);const plan=read(input.parentPlan.path);checkedCurrentCandidatePin(plan.policy);const policy=read(plan.policy.path);for(const p of [plan.producer,plan.launcher,plan.host,plan.backend,plan.light,plan.rawHc,...plan.outputs])checkedCurrentCandidatePin(p);
 const reports=input.reportPins.map(p=>read(p.path));return {input,basis,reports,sources,plan,policy};
}
export function auditCurrentCandidateTerminals({mode,inputsPin,outFile}){
 need(mode==='metadata-only','CANDIDATE_EXPLICIT_METADATA_MODE_REQUIRED');need(path.isAbsolute(outFile??'')&&!fs.existsSync(outFile),'CANDIDATE_FRESH_OUTPUT_REQUIRED');
 const documents=loadCurrentCandidateReviewDocuments(inputsPin),before=[inputsPin,documents.input.basis,...documents.input.reportPins,...Object.values(documents.input.images),...Object.values(documents.input.sources)];
 const result=reviewCurrentCandidateDocuments(documents);result.parentBinding=bindCurrentCandidatePlan(documents);for(const p of [documents.plan.businessResult,documents.plan.launchReceipt])need(!fs.existsSync(p),'CANDIDATE_PARENT_OUTPUT_ALREADY_CONSUMED',p);for(const p of [...before,documents.input.parentPlan,documents.plan.policy,...read(documents.input.currentBuildInputs.path).sourcePins])checkedCurrentCandidatePin(p);
 fs.writeFileSync(outFile,JSON.stringify({...result,inputs:inputsPin,basis:documents.input.basis,evidence:documents.input.reportPins},null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&key(process.argv[1])===key(fileURLToPath(import.meta.url))){try{const a=process.argv.slice(2);need(a.length===8&&JSON.stringify(a.filter((_,i)=>i%2===0))===JSON.stringify(['--mode','--inputs','--sha256','--out']),'CANDIDATE_EXACT_AUDIT_CLI');const r=auditCurrentCandidateTerminals({mode:a[1],inputsPin:{path:a[3],sha256:a[5]},outFile:a[7]});console.log(JSON.stringify({status:r.status,targetExecuted:false,reviewedMethodPins:r.reviewedMethodPins}));}catch(e){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',code:e.code,error:e.message,detail:e.detail}));process.exitCode=3;}}
