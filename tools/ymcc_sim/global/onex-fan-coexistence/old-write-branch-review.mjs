import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const BASIS_FILE=fileURLToPath(new URL('./old-write-branch-basis.json',import.meta.url));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const textHash=s=>createHash('sha256').update(s).digest('hex');
const key=p=>path.resolve(p).toLowerCase(),same=(a,b)=>key(a)===key(b);
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
const fail=(code,detail)=>{throw Object.assign(new Error(code),{code,detail,oldWriteBranchBlocked:true});};
const need=(value,code,detail)=>{if(!value)fail(code,detail);};
export function checkedBranchPin(pin){need(pin&&path.isAbsolute(pin.path??'')&&/^[a-f0-9]{64}$/.test(pin.sha256??''),'OLD_WRITE_BRANCH_PIN_SHAPE',pin);need(fs.existsSync(pin.path)&&fs.statSync(pin.path).isFile(),'OLD_WRITE_BRANCH_PIN_MISSING',pin);need(same(fs.realpathSync(pin.path),pin.path)&&!fs.lstatSync(pin.path).isSymbolicLink(),'OLD_WRITE_BRANCH_REDIRECTED_PIN',pin);need(hash(pin.path)===pin.sha256,'OLD_WRITE_BRANCH_PIN_CHANGED',pin);return pin;}
const fullName=m=>m.method;
export function reviewOldWriteBranchDocuments({basis,reports,sources,plan,policy,priorAnalysis,priorNextReview}){
  need(reports.length<=10&&reports.length>=3,'OLD_WRITE_BRANCH_REPORT_BUDGET');
  const methods=new Map(),types=new Map();
  for(const report of reports){need(report.status==='PASS_RAW_SELECTED_METHOD_READ'&&report.failure===null&&report.targetAssembliesObserved===false&&report.targetCodeExecuted===false&&report.targetConstructorsInvoked===false&&report.externalResolutionAllowed===false,'OLD_WRITE_BRANCH_NOT_RAW_METADATA_ONLY');for(const image of report.rows){need(image.moduleInitializers.length===0,'OLD_WRITE_BRANCH_MODULE_INITIALIZER');for(const selection of image.selections){types.set(image.sha256+'|'+selection.type,selection);for(const method of selection.methods){const id=image.sha256+'|'+method.token,previous=methods.get(id);need(!previous||previous.method.instructionListingSha256===method.instructionListingSha256,'OLD_WRITE_BRANCH_CONFLICTING_METHOD');methods.set(id,{image:{path:image.path,sha256:image.sha256,identity:image.identity},type:selection.type,method});}}}}
  need(methods.size<=400,'OLD_WRITE_BRANCH_METHOD_BUDGET');
  for(const row of basis.reviewedBodies){const actual=methods.get(row.imageSha256+'|'+row.token);need(actual&&actual.method.method===row.method&&actual.method.instructionListingSha256===row.instructionListingSha256,'OLD_WRITE_BRANCH_UNREVIEWED_BODY',row);}
  for(const expected of basis.sources){need(sources[expected.name]&&textHash(sources[expected.name])===expected.sha256,'OLD_WRITE_BRANCH_UNREVIEWED_SOURCE',expected);}
  const host=basis.images.host,producer=basis.images.producer,synthetic=basis.images.synthetic;
  const get=(image,fragment)=>{const rows=[...methods.values()].filter(row=>row.image.sha256===image&&fullName(row.method).includes(fragment));need(rows.length===1,'OLD_WRITE_BRANCH_UNIQUE_METHOD_REQUIRED',{fragment,count:rows.length});return rows[0];};
  const witness=row=>({image:row.image,token:row.method.token,method:row.method.method,instructionListingSha256:row.method.instructionListingSha256});
  const at=(row,offset,opcode,contains)=>{const i=row.method.instructions.find(i=>i.offset===offset);need(i&&i.opcode===opcode&&(!contains||i.operand.includes(contains)),'OLD_WRITE_BRANCH_IL_GUARD_CHANGED',{method:row.method.method,offset,opcode,contains,actual:i});return i;};
  const stores=row=>row.method.instructions.filter(i=>i.opcode==='stfld'||i.opcode==='stsfld');
  const noStore=(row,names)=>need(stores(row).every(i=>names.every(name=>!i.operand.includes('::'+name+' '))),'OLD_WRITE_BRANCH_UNEXPECTED_RESOURCE_ASSIGNMENT',{method:row.method.method,fields:names});
  const source=sources['OldWriteProbe.cs'];
  need(plan.kind==='onex-old-write-prepared-invocation'&&plan.status==='PREPARED_NOT_EXECUTED'&&plan.producerExecuted===false&&plan.businessPassed===false,'OLD_WRITE_BRANCH_EXACT_PREPARE_REQUIRED');
  for(const [k,v]of Object.entries(basis.gate))need(plan.gate[k]===v,'OLD_WRITE_BRANCH_GATE_CHANGED',{field:k});
  need(plan.host.sha256===host&&plan.producer.sha256===producer,'OLD_WRITE_BRANCH_DIFFERENT_HOST_OR_PRODUCER');
  need(plan.args.length===4&&plan.args[0]==='old-write'&&same(plan.args[1],plan.host.path)&&same(plan.args[2],plan.rawHc.path)&&same(plan.args[3],plan.businessResult),'OLD_WRITE_BRANCH_ARGV_CHANGED');
  need(JSON.stringify(plan.dotnetArguments)===JSON.stringify([plan.launcher.path,'run',plan.policy.path,plan.policy.sha256,plan.launchReceipt,...plan.args]),'OLD_WRITE_BRANCH_LAUNCH_VECTOR_CHANGED');
  need(plan.launcher.sha256==='55bbc787f00ccc7b3c3855023f2f96df6423baf0481883ec40a732f7b5583a0e'&&plan.dotnet.sha256==='ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135','OLD_WRITE_BRANCH_UNREVIEWED_LAUNCHER');
  need(plan.rawHc.sha256==='08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'&&policy.metadata.some(p=>same(p.path,plan.rawHc.path)&&p.sha256===plan.rawHc.sha256),'OLD_WRITE_BRANCH_RAW_HC_METADATA_CHANGED');
  const norm=p=>{const r=path.relative(plan.ownedRoot,p);return !path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep)?'$OWNED/'+r.replaceAll('\\','/').toLowerCase():key(p);};
  const normalized=structuredClone(policy.contexts[0]);for(const p of normalized.managed)p.path=norm(p.path);for(const r of normalized.requests)r.targetPath=norm(r.targetPath);
  need(JSON.stringify(normalized)===JSON.stringify(basis.normalizedDefaultContext),'OLD_WRITE_BRANCH_DEFAULT_POLICY_CHANGED');
  const targets=policy.contexts[1].managed.map(p=>({path:key(p.path),sha256:p.sha256,identity:p.identity,role:p.role}));need(JSON.stringify(targets)===JSON.stringify(basis.customTargets),'OLD_WRITE_BRANCH_CUSTOM_TARGETS_CHANGED');
  need(policy.contexts.length===2&&policy.contexts[0].id==='Default'&&policy.contexts[0].managed.length===44&&policy.contexts[1].managed.length===2,'OLD_WRITE_BRANCH_POLICY_SCOPE_CHANGED');
  need(same(policy.entry,plan.producer.path)&&policy.contexts[1].id==='OldWriteProbe.Context|'+path.dirname(plan.host.path).toUpperCase()&&policy.contexts[1].registrationType==='OldWriteProbe+Context','OLD_WRITE_BRANCH_CONTEXT_CHANGED');
  const hc=policy.contexts.flatMap(c=>c.managed).filter(p=>p.identity.startsWith('HandheldCompanion,'));need(hc.length===1&&hc[0].sha256===synthetic,'OLD_WRITE_BRANCH_NON_SYNTHETIC_HC');
  need(policy.contexts.flatMap(c=>c.managed).every(p=>p.sha256!==plan.rawHc.sha256),'OLD_WRITE_BRANCH_RAW_HC_RUNTIME');
  need(priorAnalysis.fixedBranchProof.proven===true&&priorAnalysis.oneXLifecycleDirectFrontier.length===0&&priorAnalysis.syntheticDirectFrontier.length===0,'OLD_WRITE_BRANCH_PRIOR_SCOPE_NOT_PROVEN');
  const nineteen=basis.requiredNineteen.map(expected=>{const row=methods.get(host+'|'+expected.token);need(row&&row.method.method===expected.target.replace(/ \[scope=.*$/,''),'OLD_WRITE_BRANCH_MISSING_NINETEEN',expected);return witness(row);});
  need(nineteen.length===19,'OLD_WRITE_BRANCH_NINETEEN_REQUIRED');
  need(priorNextReview.remainingConservativeFrontier.actualBranchOrDiagnosticHelperNeedsParentClassification.length===19,'OLD_WRITE_BRANCH_INPUT_FRONTIER_CHANGED');

  // Uninitialized HostOptions is deliberate: the HostOptions constructor is not
  // invoked, all backing references are null, and the actual backend ctor sets
  // useBackendFanWriter=false at the reviewed FanProvider branch.
  const backend=get(producer,'OldWriteProbe::Backend('),run=get(producer,'OldWriteProbe::Run('),ctor=get(host,'RealHcBackend::.ctor(');
  need(backend.method.calls.some(c=>c.target.includes('RuntimeHelpers::GetUninitializedObject')),'OLD_WRITE_BRANCH_OPTIONS_CTOR_ROUTE_CHANGED');
  need(source.includes('Set(b,"device",device);Set(b,"assembly",typeof(IDevice).Assembly)')&&source.includes('b.GetType().GetProperty("Diagnostic")!.SetValue(b,new Action<string,object?>'),'OLD_WRITE_BRANCH_BACKEND_ASSIGNMENT_CHANGED');
  const deviceCtors=run.method.calls.filter(c=>c.opcode==='newobj'&&c.target.includes('HandheldCompanion.Devices.'));
  need(deviceCtors.length===3&&deviceCtors.every(c=>c.target.includes('HandheldCompanion.Devices.OneXPlayerX1AMD::.ctor')),'OLD_WRITE_BRANCH_DIFFERENT_ACTUAL_FACTORY');
  for(const f of ['routeOverrideForSelfTest','hcBoundaryForSelfTest','<DiagnosticLazy>k__BackingField','temperatureWatchdog'])noStore(ctor,[f]);
  need(!source.includes('DiagnosticLazy')&&!source.includes('hcBoundaryForSelfTest')&&!source.includes('routeOverrideForSelfTest'),'OLD_WRITE_BRANCH_DEFAULT_STATE_CHANGED');
  at(ctor,0x18d,'brfalse.s','IL_01b6');at(ctor,0x1b6,'ldc.i4.0');at(ctor,0x1d9,'stfld','::useBackendFanWriter');
  const factory=get(host,'RealHcBackend::get_FactoryType('),route=get(host,'RealHcBackend::get_Route('),build=get(host,'RealHcBackend::BuildFanRoutes('),add=get(host,'RealHcBackend::<BuildFanRoutes>g__Add');
  at(factory,1,'ldfld','::device');at(factory,0xc,'call','GetType');at(factory,0x11,'callvirt','get_FullName');at(route,1,'ldfld','::routeOverrideForSelfTest');at(route,0xb,'call','get_FactoryType');at(route,0x10,'call','ResolveRoute');
  at(build,17,'ldc.i4.0');at(build,23,'ldc.i4.1');at(build,360,'ldstr','OneXPlayerX1AMD');at(build,404,'call','<BuildFanRoutes>g__Add');
  const restore=types.get(host+'|YeManFanHost.FanRestoreStrategy'),kind=types.get(host+'|YeManFanHost.FanRouteKind');
  need(restore?.fields.some(f=>f.name==='GenericEcControl'&&f.constant===1)&&kind?.fields.some(f=>f.name==='GenericDuty'&&f.constant===0),'OLD_WRITE_BRANCH_ROUTE_ENUM_CHANGED');
  at(add,18,'ldstr','HandheldCompanion.Devices.');at(add,37,'newobj','FanRoute::.ctor');
  const backendWriter=get(host,'RealHcBackend::get_UsesBackendFanWriter(');at(backendWriter,1,'ldfld','::useBackendFanWriter');at(backendWriter,6,'brfalse.s','IL_001e');at(backendWriter,0x1e,'ldc.i4.0');
  const routeFact={id:'actual-factory-route',factoryType:'HandheldCompanion.Devices.OneXPlayerX1AMD',basis:'Three actual newobj sites in OldWriteProbe.Run, injected device field, GetType.FullName, exact dictionary Add record and enum constants; not a class-name heuristic',route:{kind:'GenericDuty',value:0,restore:'GenericEcControl',restoreValue:1},defaults:{FanProvider:null,useBackendFanWriter:false,routeOverrideForSelfTest:null,hcBoundaryForSelfTest:null},witnesses:[backend,run,ctor,factory,route,build,add,backendWriter].map(witness)};

  const profile=get(host,'RealHcBackend::ApplyPowerProfile('),beginMsi=get(host,'RealHcBackend::BeginMsiReleaseOperation('),settleMsi=get(host,'RealHcBackend::SettleMsiCloseRelease(');
  at(profile,0x54,'ldc.i4.8');at(profile,0x57,'brfalse.s','IL_00c0');
  for(const row of [beginMsi,settleMsi]){at(row,0x12,'ldc.i4.8');at(row,0x18,'brfalse.s','IL_001b');at(row,0x1a,'ret');}
  const exclusionFact={id:'MSI-ASUS-branch-exclusion',restoreStrategy:1,msiRestoreStrategy:8,ApplyPowerProfile:{guardOffset:0x57,falseTarget:0xc0,ApplyMsiCallSkipped:true},closeSettlement:{returnOffset:0x1a,MsiStructuredReleaseNotInvoked:true},ASUS:{useBackendFanWriter:false,UsesBackendFanWriter:false},scope:'Original 36 MSI + 1 ASUS conservative edges remain outside actual fixed path; not recursively read',witnesses:[profile,beginMsi,settleMsi,backendWriter].map(witness)};

  // DiagnosticLazy is null, but eager Diagnostic is supplied. Hence factories
  // DO execute synchronously and must be included in this review.
  const emit=get(host,'RealHcBackend::EmitDiagnostic('),log=get(producer,'::<Backend>b__0(');
  at(emit,8,'brfalse.s','IL_0013');at(emit,0x20,'callvirt','System.Func');at(emit,0x25,'callvirt','System.Action');
  need(log.method.calls.length===3&&log.method.calls[0].target.includes('JsonSerializer::Serialize')&&log.method.calls[1].target.includes('String::Concat')&&log.method.calls[2].target.includes('List`1<System.String>::Add'),'OLD_WRITE_BRANCH_DIAGNOSTIC_TERMINAL_CHANGED');
  const diagnosticFact={id:'eager-diagnostic',DiagnosticLazy:null,Diagnostic:'Exact owned Action serializes bounded fixed DTO then appends to own List<string>',factoryEvaluated:true,scope:'All supplied eager callback bodies and JsonDefaults initializer are pinned; no skipped lazy lambda assumption',witnesses:[emit,log,get(host,'JsonDefaults::.cctor(')].map(witness)};

  const monitorCtor=get(host,'HWiNFOTemperatureMonitor::.ctor('),dispose=get(host,'HWiNFOTemperatureMonitor::Dispose('),mapping=get(host,'HWiNFOTemperatureMonitor::InvalidateCachedMapping('),detach=get(host,'RealHcBackend::DetachCpuTemperatureLoop(');
  noStore(monitorCtor,['timer','cachedMappingView','cachedMapping']);
  const monitorSets=[...source.matchAll(/Set\(monitor,"([^"]+)"/g)].map(m=>m[1]);need(monitorSets.length===3&&monitorSets.every(n=>n==='sampleReadTick'),'OLD_WRITE_BRANCH_MONITOR_ASSIGNMENT_CHANGED');
  need(!source.includes('Invoke(monitor,')&&!source.includes('"Start"')&&!source.includes('"Poll"'),'OLD_WRITE_BRANCH_MONITOR_START_CHANGED');
  at(dispose,0x2e,'brtrue.s','IL_0033');at(dispose,0x31,'br.s','IL_0038');at(dispose,0x33,'call','Timer::Dispose');
  at(mapping,0x1b,'brtrue.s','IL_0020');at(mapping,0x1e,'br.s','IL_0025');at(mapping,0x20,'call','UnmanagedMemoryAccessor::Dispose');at(mapping,0x32,'brtrue.s','IL_0037');at(mapping,0x35,'br.s','IL_003c');at(mapping,0x37,'call','MemoryMappedFile::Dispose');
  const monitorMethods=[...methods.values()].filter(r=>r.image.sha256===host&&r.type==='YeManFanHost.HWiNFOTemperatureMonitor');
  need(monitorMethods.every(r=>(r.method.calls??[]).every(c=>!/Timer::\.ctor|MemoryMappedFile::(?:Create|Open)|MemoryMappedViewAccessor::\.ctor/.test(c.target))),'OLD_WRITE_BRANCH_MONITOR_EXTERNAL_RESOURCE_CREATION');
  const monitorFact={id:'fresh-monitor-dispose',derivedState:{timer:null,cachedMappingView:null,cachedMapping:null,backendTemperatureWatchdog:null},assignments:monitorSets,
    proof:'Actual fresh ctor allocates only dictionaries/gates/byte buffer; exact Run sets sampleReadTick three times, never Start/Poll; selected synchronous monitor methods do not create external resources. All three Dispose leaves are skipped by explicit null branches.',
    skippedExternalLeaves:[{method:dispose.method.method,guardOffset:0x2e,skipOffset:0x31,callOffset:0x33,leaf:'Timer.Dispose'},{method:mapping.method.method,guardOffset:0x1b,skipOffset:0x1e,callOffset:0x20,leaf:'UnmanagedMemoryAccessor.Dispose'},{method:mapping.method.method,guardOffset:0x32,skipOffset:0x35,callOffset:0x37,leaf:'MemoryMappedFile.Dispose'}],witnesses:[monitorCtor,dispose,mapping,detach].map(witness)};

  const confirm=get(host,'RealHcBackend::ConfirmManagerCleanupAfterClose('),should=get(host,'RealHcBackend::ShouldObserveManagerAfterClose('),observe=get(host,'RealHcBackend::ObserveManagerFactoryLifecycle('),close=get(host,'::<CloseHcDevice>b__0(');
  need(should.method.instructions.map(i=>i.opcode).join(',')==='ldarg.0,ldarg.1,or,ret','OLD_WRITE_BRANCH_MANAGER_OR_GUARD_CHANGED');
  at(close,0x9c,'ldc.i4.1');at(close,0x9d,'call','set_HcVirtualCloseReturned');at(confirm,0xe,'call','ShouldObserveManagerAfterClose');at(confirm,0x16,'call','ObserveManagerFactoryLifecycle');
  at(observe,0xc,'ldfld','::hcBoundaryForSelfTest');at(observe,0x13,'brfalse','IL_00f1');at(observe,0x114,'ldstr','Managers');at(observe,0x119,'call','GetStaticMember');at(observe,0x171,'ldstr','Status');
  const managers=get(synthetic,'ManagerFactory::get_Managers('),status=get(synthetic,'IManager::get_Status(');
  need(managers.method.calls.length===0&&managers.method.instructions.filter(i=>i.opcode==='ldsfld').length===3&&status.method.calls.length===0&&status.method.instructions[1].operand.includes('::<Status>k__BackingField'),'OLD_WRITE_BRANCH_MANAGER_GETTER_NOT_INERT');
  const managerFact={id:'close-manager-observation',actuallyExecuted:true,reason:'HcVirtualCloseReturned becomes true; ShouldObserve is OR, not a false/default short circuit',reflection:{type:'HandheldCompanion.Managers.ManagerFactory',staticMember:'Managers',rows:['SettingsManager','DeviceManager','PowerProfileManager'],statusMember:'IManager.Status'},
    terminal:'Exact synthetic array and backing-field getters; manager constructors allocate only dictionary/profile/objects. No manager Start/Stop or foreign manager binding.',selfTestBranchExcludedByNull:true,witnesses:[confirm,should,observe,close,managers,status,get(host,'RealHcBackend::GetStaticMember('),get(host,'RealHcBackend::FindStaticMemberMetadata(')].map(witness)};

  const chain=['HandheldCompanion.Devices.OneXPlayerX1AMD','HandheldCompanion.Devices.OneXPlayerX1','HandheldCompanion.Devices.OneXAOKZOE','HandheldCompanion.Devices.IDevice'];
  for(const type of chain){const t=types.get(synthetic+'|'+type);need(t?.absentRequestedMethods?.includes('TryWaitCloseCleanupCompleted'),'OLD_WRITE_BRANCH_UNREVIEWED_CLOSE_API',{type});}
  const candidateFact={id:'candidate-close-reflection',actualTypeChain:chain,TryWaitCloseCleanupCompleted:'Absent on every concrete declaring owner; candidate cleanup method is not invoked',witness:witness(get(host,'RealHcBackend::ConfirmCandidateCloseCleanup('))};
  const allNewTokens=new Set(basis.requiredNineteen.map(e=>host+'|'+e.token));
  for(const report of reports.slice(-2))for(const image of report.rows)for(const t of image.selections)for(const m of t.methods)allNewTokens.add(image.sha256+'|'+m.token);
  const missing=[];for(const id of allNewTokens){const row=methods.get(id);if(!row)continue;for(const c of row.method.calls??[])if(c.token>>>24===6&&!methods.has(row.image.sha256+'|'+c.token)){
    if(row.method.method.includes('ObserveManagerFactoryLifecycle(')&&c.offset===0x19&&c.target.includes('SelfTestHcBoundary::get_ManagerObservationAvailable'))continue;
    missing.push({from:row.method.method,offset:c.offset,...c});
  }}need(missing.length===0,'OLD_WRITE_BRANCH_OPEN_ACTUAL_LOCAL_EDGE',missing);
  const readinessFact={id:'temperature-synchronous-readiness',assignments:['sessionDeviceReference=temperatureDevice','oemBaselineCaptured=true','baselineRestoreRoute=actual device FullName','baselineStrategy=actual Route.RestoreStrategy','CaptureSessionRouteMarker before sample dispatch'],witnesses:[get(host,'RealHcBackend::IsHcSessionReadyForControl('),get(host,'RealHcBackend::EnsureHcSessionReadyForControl('),get(host,'RealHcBackend::CaptureSessionRouteMarker('),get(host,'RealHcBackend::OnHcCpuTemperatureChanged(')].map(witness),effect:'Field/epoch/reference checks and exact synthetic device IsOpen; no real curve computation, timer start or manager bootstrap'};
  return {schemaVersion:1,status:'REVIEWED_ONE_EXACT_OLD_WRITE_NO_DEVICE_IO',staticNoDeviceIoConclusion:true,oneInvocationOnly:true,originalGate:basis.gate,
    reviewedNineteen:nineteen,nineteenCount:19,uniqueMetadataMethods:methods.size,actualBranchLocalMissing:[],facts:[routeFact,exclusionFact,diagnosticFact,monitorFact,managerFact,candidateFact,readinessFact],
    priorMsiAsusScopeExpanded:false,untouchedConservativeMsiAsusEdges:37,sourceApprovalGranted:false,executionApprovalGranted:false,producerExecuted:false,fullHostApproved:false,
    allowedTerminals:['Exact pinned PE/policy/metadata reads including raw HC via Cecil only','Fresh owned JSON/log outputs','Own-thread/current-process/clock observations','Pure managed DTO/cache/collection/delegate/reflection work','Existing 6d7 synthetic OLS/EC/serial trace terminals'],
    rejectIf:['Host/producer/synthetic/source/IL bytes differ','Any actual device ctor/type/base chain or exact route assignment differs','HostOptions constructor or non-null provider/self-test/lazy diagnostic hook introduced','Monitor timer/mapping/view or backend watchdog can be non-null, or Start/Poll enters the path','Manager reflection escapes exact synthetic ManagerFactory/IManager getters','Candidate close API appears on any fixed owner','Any actual branch direct helper is missing','Unknown managed/native path or real raw HC load requested','Original 6 cases/9 goldens/native0/45s changed','Exact policy/argv/output pin or fresh output check fails'],
    pendingIndependentRunGates:['Parent selected-policy Prepare/default44/TPA validation for its actual copied invocation','Parent explicit single-business invocation authorization, fresh outputs and post-run managed/native receipt verification'],
    noIoDefinition:'No device/registry/network/foreign-process effects on this exact static branch. Allowed pinned reads and owned writes remain explicit. This is not full CLR/native/Host acceptance.'};
}

export function reviewOldWriteBranch({inputsPin,evidenceDir}){
  const checked=new Map(),check=p=>{checkedBranchPin(p);const old=checked.get(key(p.path));need(!old||old.sha256===p.sha256,'OLD_WRITE_BRANCH_CONFLICTING_PIN',p);checked.set(key(p.path),{path:p.path,sha256:p.sha256});return p;},json=p=>read(check(p).path);
  const input=json(inputsPin);need(input.schemaVersion===1&&input.kind==='onex-old-write-branch-review','OLD_WRITE_BRANCH_INPUT_SCHEMA');
  const basis=read(BASIS_FILE),plan=json(input.preparedPlan),policy=json(plan.policy),priorAnalysis=json(input.priorAnalysis),priorNextReview=json(input.priorNextReview),priorPrepare=json(input.priorPrepareInputs),seal=json(input.priorSeal),reports=input.rawReports.map(json),validation=json(input.rawReaderValidation);
  need(validation.status==='OWN_RAW_METHOD_READER_CANARY_AND_TWO_REFUSALS_PASSED'&&validation.tool.sha256==='3bd052bb0165ad8ce1765e2b9ab3504ccec443fa350f365dca1bf899b50e928e','OLD_WRITE_BRANCH_READER_NOT_REVIEWED');check(validation.tool);check(validation.source);
  need(seal.status==='SEALED_READY_D_ONLY_PREPARE_NOT_RUN','OLD_WRITE_BRANCH_PRIOR_SEAL_REQUIRED');
  const sources={};for(const p of input.sourcePins){check(p);sources[path.basename(p.path)]=fs.readFileSync(p.path,'utf8');}
  const result=reviewOldWriteBranchDocuments({basis,reports,sources,plan,policy,priorAnalysis,priorNextReview});
  for(const report of reports)for(const image of report.rows)check(image);
  for(const p of [...plan.outputs,...policy.metadata,...policy.contexts.flatMap(c=>c.managed)])check(p);
  const provenancePin=policy.metadata.find(p=>path.basename(p.path)==='source-input-provenance.json'),provenance=json(provenancePin),binding=json(provenance.fixedRunBindings);
  need(path.isAbsolute(evidenceDir??'')&&!fs.existsSync(evidenceDir),'OLD_WRITE_BRANCH_FRESH_OWNED_OUTPUT_REQUIRED');
  const roots=[binding.sourceRoot,binding.snapshotRoot,path.dirname(input.priorSeal.path)];need(roots.every(root=>!inside(root,evidenceDir)&&!inside(evidenceDir,root)),'OLD_WRITE_BRANCH_OUTPUT_OVERLAPS_SOURCE_OR_SEAL');
  need(same(fs.realpathSync(path.dirname(evidenceDir)),path.dirname(evidenceDir)),'OLD_WRITE_BRANCH_REDIRECTED_OUTPUT');
  for(const p of checked.values())checkedBranchPin(p);
  fs.mkdirSync(evidenceDir);const review={...result,atUtc:new Date().toISOString(),plan:input.preparedPlan,policy:plan.policy,argv:plan.args,dotnetArguments:plan.dotnetArguments,argumentMetadata:plan.argumentMetadata,input:inputsPin,basis:{path:BASIS_FILE,sha256:hash(BASIS_FILE)},sourcePins:input.sourcePins,checkedPins:[...checked.values()]};
  const output=path.join(evidenceDir,'OLD-WRITE-BRANCH-REVIEW.json');fs.writeFileSync(output,JSON.stringify(review,null,2)+'\n',{flag:'wx'});return {status:review.status,path:output,sha256:hash(output),nineteenCount:19,staticNoDeviceIoConclusion:true,producerExecuted:false,executionApprovalGranted:false,fullHostApproved:false};
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){try{const args=process.argv.slice(2);need(args.length===6&&JSON.stringify(args.filter((_,i)=>i%2===0))===JSON.stringify(['--inputs','--sha256','--evidence-dir']),'OLD_WRITE_BRANCH_EXACT_CLI_ARGUMENTS');console.log(JSON.stringify(reviewOldWriteBranch({inputsPin:{path:args[1],sha256:args[3]},evidenceDir:args[5]})));}catch(e){console.error(JSON.stringify({status:'BLOCKED_OLD_WRITE_BRANCH',code:e.code,error:e.message,detail:e.detail,producerExecuted:false}));process.exitCode=3;}}
