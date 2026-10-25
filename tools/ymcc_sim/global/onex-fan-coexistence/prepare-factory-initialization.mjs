import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {checkedPin,filePin,read,need,digest} from './default-case-contract.mjs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import basis from './factory-initialization-basis.json' with {type:'json'};

const key=p=>path.resolve(p).toUpperCase(),same=(a,b)=>key(a)===key(b);
const inside=(r,p)=>{const v=path.relative(r,p);return !!v&&!path.isAbsolute(v)&&v!=='..'&&!v.startsWith('..'+path.sep);};
const put=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const all=pins=>pins.forEach(checkedPin);
const unique=rows=>{const m=new Map();for(const p of rows){const k=key(p.path);need(!m.has(k)||m.get(k).sha256===p.sha256,'FACTORY_METADATA_CONFLICT',p.path);m.set(k,p);}return [...m.values()];};
const fresh=p=>{need(path.isAbsolute(p??'')&&!fs.existsSync(p),'FACTORY_FRESH_DIRECTORY_REQUIRED',p);fs.mkdirSync(p,{recursive:true});need(same(fs.realpathSync(p),p),'FACTORY_REDIRECTED_OUTPUT');};
const ids=rows=>JSON.stringify([...rows].sort());
function normalizeOwned(v,root){if(typeof v==='string'&&path.isAbsolute(v)&&(same(v,root)||inside(root,v)))return '$OWNED/'+path.relative(root,v).replaceAll('\\','/');if(Array.isArray(v))return v.map(x=>normalizeOwned(x,root));if(v&&typeof v==='object')return Object.fromEntries(Object.entries(v).map(([k,x])=>[k,normalizeOwned(x,root)]));return v;}
export function factoryInitializationContract(mode='positive'){
 need(['positive','readiness','continuation'].includes(mode),'FACTORY_UNKNOWN_MODE');
 return {mode,budgetMs:30000,parentBudgetMs:360000,expectedExitCode:mode==='positive'?0:2,caseIds:[...basis.caseIds],positiveMinimumChecks:157,failureFragments:structuredClone(basis.failureFragments[mode]??{}),nativeAttempts:0,hostMainStarted:false,hardwareIo:false,productionValidationExecuted:false};
}
// Token RIDs and byte offsets may change after Cecil writes. Branch targets and
// EH ranges are instruction indices; typed member operands retain their scope.
export function canonicalFactoryMethod(m){
 need(typeof m.method==='string'&&Array.isArray(m.instructions),'FACTORY_RAW_METHOD_REQUIRED');
 const ix=new Map(m.instructions.map((i,n)=>[i.offset,n]));const at=o=>o==null?null:(need(ix.has(o)||o===m.bodySize,'FACTORY_INVALID_IL_OFFSET',o),o===m.bodySize?m.instructions.length:ix.get(o));
 const operand=i=>/^(br|beq|bne|bge|bgt|ble|blt|leave|switch)/.test(i.opcode)?i.operand.replace(/IL_([0-9a-f]+)/gi,(_,v)=>'instruction:'+at(parseInt(v,16))):i.operand;
 return {method:m.method,isStatic:m.isStatic,isConstructor:m.isConstructor,isPInvoke:m.isPInvoke,hasBody:m.hasBody,pinvoke:m.pinvoke,locals:m.locals,
 instructions:m.instructions.map(i=>({opcode:i.opcode.replace(/\.s$/,''),operand:operand(i)})),exceptions:m.exceptions?.map(e=>({...e,tryStart:at(e.tryStart),tryEnd:at(e.tryEnd),handlerStart:at(e.handlerStart),handlerEnd:at(e.handlerEnd),filterStart:at(e.filterStart)}))};
}
function indexImage(report,pin){
 need(report.status==='PASS_RAW_SELECTED_METHOD_READ'&&report.targetAssembliesObserved===false&&report.targetCodeExecuted===false&&report.targetConstructorsInvoked===false&&report.externalResolutionAllowed===false,'FACTORY_RAW_METADATA_ONLY_REQUIRED');
 const matches=report.rows.filter(r=>same(r.path,pin.path)&&r.sha256===pin.sha256);need(matches.length===1,'FACTORY_RAW_IMAGE_PIN_REQUIRED');const image=matches[0];need(image.identity===basis.identities.host&&image.moduleInitializers?.length===0,'FACTORY_RAW_HOST_IDENTITY');
 const methods=image.selections.flatMap(s=>s.methods),map=new Map();for(const m of methods){need(!map.has(m.method),'FACTORY_DUPLICATE_FULL_SIGNATURE',m.method);map.set(m.method,m);}return {image,map};
}
export function validateFactoryRawDifference({sourceReport,sourcePin,generatedReport,generatedPin,proof}){
 const before=indexImage(sourceReport,sourcePin),after=indexImage(generatedReport,generatedPin);
 need(sourcePin.sha256===basis.images.host,'FACTORY_CURRENT_SOURCE_REQUIRED');
 need(proof.status==='PASS'&&proof.sourceSha256===sourcePin.sha256&&proof.instrumentedSha256===generatedPin.sha256&&proof.productionValidationExecuted===false&&proof.hardwareIo===false,'FACTORY_PORTS_PROOF_REQUIRED');
 need(proof.changedMethods?.length===5&&ids(proof.changedMethods)===ids(basis.leaves.map(l=>l.signature)),'FACTORY_EXACT_FIVE_CHANGED_METHODS');
 need(ids(before.map.keys())===ids(after.map.keys())&&ids(before.map.keys())===ids(basis.selectedMethods.map(m=>m.signature)),'FACTORY_SELECTED_SIGNATURE_SET_CHANGED');
 for(const selected of basis.selectedMethods){const b=before.map.get(selected.signature),a=after.map.get(selected.signature);need(digest(canonicalFactoryMethod(b))===selected.canonicalSha256,'FACTORY_ORIGINAL_METHOD_CHANGED',selected.signature);const leaf=basis.leaves.find(l=>l.signature===selected.signature);
  if(!leaf){need(digest(canonicalFactoryMethod(a))===selected.canonicalSha256,'FACTORY_ORIGINAL_CONTROL_FLOW_CHANGED',selected.signature);continue;}
  need(a.isStatic===false&&a.hasBody===true&&a.isPInvoke===false&&a.locals?.length===0&&a.exceptions?.length===0,'FACTORY_PORT_BODY_SHAPE',a.method);
  need(JSON.stringify(a.instructions.map(i=>({opcode:i.opcode,operand:i.operand})))===JSON.stringify(leaf.instructions),'FACTORY_TYPED_PORT_OPERANDS_CHANGED',a.method);
 }
 need(JSON.stringify(before.image.selections.map(s=>({type:s.type,fields:s.fields,baseType:s.baseType})))===JSON.stringify(after.image.selections.map(s=>({type:s.type,fields:s.fields,baseType:s.baseType}))),'FACTORY_FIELDS_OR_BASE_CHANGED');
 return {status:'PASS_EXACT_FIVE_FACTORY_LEAVES',changedFullSignatures:proof.changedMethods,selectedUnchanged:basis.selectedMethods.length-5,tokenRidsUsedForMatching:false,allOtherMethodPreservationBasis:'original pinned InstrumentFactoryLeaves/FactoryChangedMethods proof (attributes, locals, EH and instructions)',targetExecuted:false};
}
function sourceInputs(input){
 need(input.kind==='onex-factory-initialization-prepare'&&input.schemaVersion===1,'FACTORY_INPUT_SCHEMA');
 all([input.basePlan,input.currentSourceBuild,input.cleanRaw,input.rawReader,input.frameworkMetadata,input.basePreload,...Object.values(input.sources)]);
 need(input.frameworkMetadata.sha256===basis.frameworkMetadataSha256&&input.basePreload.sha256===basis.basePreloadSha256,'FACTORY_FRAMEWORK_METADATA_PIN');
 need(input.currentSourceBuild.sha256===basis.currentSourceBuildSha256&&input.rawReader.sha256===basis.rawReaderSha256,'FACTORY_BUILD_OR_READER_PIN');
 for(const [name,sha]of Object.entries(basis.sources))need(input.sources[name]?.sha256===sha,'FACTORY_SOURCE_CHANGED',name);
 const base=read(input.basePlan.path);all([base.policy,base.dotnet,...base.outputs,base.host,base.backend,base.light,base.rawHc]);
 for(const role of ['host','backend','producer','launcher','rawHc','light'])need(base[role].sha256===basis.images[role],'FACTORY_BASE_IMAGE_CHANGED',role);
 const bp=read(base.policy.path);all([...bp.metadata,...bp.contexts.flatMap(c=>c.managed)]);const def=bp.contexts.find(c=>c.id==='Default');
 need(base.outputs.length===37&&def?.managed.length===44&&def.kind==='default','FACTORY_ORIGINAL37_DEFAULT44');
 need(digest(normalizeOwned(def,base.ownedRoot))===basis.defaultContextSha256,'FACTORY_DEFAULT_POLICY_DRIFT');
 const actual=base.outputs.map(p=>({relative:path.relative(base.ownedRoot,p.path).replaceAll('\\','/'),sha256:p.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative));
 need(JSON.stringify(actual)===JSON.stringify(basis.original37),'FACTORY_ORIGINAL37_BYTES_OR_LAYOUT_CHANGED');
 need(def.managed.filter(p=>p.identity.startsWith('HandheldCompanion,')).length===1&&def.managed.find(p=>p.identity.startsWith('HandheldCompanion,')).sha256===basis.images.synthetic,'FACTORY_SYNTHETIC_ONLY');
 need(!def.managed.some(p=>/^(YeManFanHost|YeManFanBackend|WinRT.Runtime|Microsoft.Windows.SDK.NET),/.test(p.identity)||p.sha256===basis.images.rawHc),'FACTORY_DEFAULT_TARGET_FORBIDDEN');
 const raw=read(input.cleanRaw.path),clean=indexImage(raw,base.host);need(ids(clean.map.keys())===ids(basis.selectedMethods.map(m=>m.signature)),'FACTORY_CLEAN_SELECTED_METHODS');for(const m of basis.selectedMethods)need(digest(canonicalFactoryMethod(clean.map.get(m.signature)))===m.canonicalSha256,'FACTORY_CLEAN_METHOD_CHANGED',m.signature);
 return {base,bp,def};
}
export function validateFactoryGenerationPlan(plan,policy,argv,{requireFresh=true}={}){
 need(plan.kind==='onex-factory-leaf-ports-generation'&&plan.status==='PREPARED_NOT_EXECUTED'&&plan.budgetMs===30000&&plan.replayBudgetMs===30000&&plan.parentBudgetMs===360000,'FACTORY_PLAN_CONTRACT');
 need(plan.args.length===4&&plan.args[0]==='factory-leaf-ports'&&same(plan.args[1],plan.sourceHost.path)&&same(plan.args[2],plan.generatedHost)&&same(plan.args[3],plan.portsProof),'FACTORY_EXACT_GENERATION_ARGV');
 for(const f of [plan.generatedHost,plan.portsProof,plan.launchReceipt,plan.metadataValidateReceipt])need(inside(plan.ownedRoot,f)&&(!requireFresh||!fs.existsSync(f)),'FACTORY_FRESH_GENERATION_OUTPUT_REQUIRED',f);
 need(policy.contexts.length===1&&policy.contexts[0].id==='Default'&&policy.contexts[0].managed.length===44,'FACTORY_GENERATION_DEFAULT44_ONLY');
 need(!policy.contexts[0].managed.some(p=>/^(YeManFanHost|YeManFanBackend|WinRT.Runtime|Microsoft.Windows.SDK.NET),/.test(p.identity)||p.sha256===basis.images.rawHc),'FACTORY_GENERATOR_TARGET_LOAD_FORBIDDEN');
 need(policy.metadata.some(p=>same(p.path,plan.sourceHost.path)&&p.sha256===basis.images.host),'FACTORY_CLEAN_HOST_METADATA_REQUIRED');
 need(JSON.stringify(argv.args)===JSON.stringify(plan.args)&&argv.argvSha256===digest(plan.args)&&argv.kind==='Harness','FACTORY_ARGV_METADATA_CHANGED');
 need(JSON.stringify(plan.dotnetArguments)===JSON.stringify([plan.launcher.path,'run',plan.policy.path,plan.policy.sha256,plan.launchReceipt,...plan.args]),'FACTORY_MTA_VECTOR_CHANGED');return true;
}
export function prepareFactoryInitialization({mode,inputsPin,outDir}){
 need(mode==='prepare-only','FACTORY_EXPLICIT_PREPARE_ONLY');checkedPin(inputsPin);const input=read(inputsPin.path),{base,bp,def}=sourceInputs(input);fresh(outDir);const owned=path.join(outDir,'owned-stage');fs.mkdirSync(owned);
 const outputs=[],mapping=new Map();for(const row of base.outputs){need(inside(base.ownedRoot,row.path),'FACTORY_OUTPUT_OUTSIDE_BASE');const target=path.join(owned,path.relative(base.ownedRoot,row.path));fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.path,target,fs.constants.COPYFILE_EXCL);const p={...filePin(target),source:row.path};need(p.sha256===row.sha256,'FACTORY_COPY_CHANGED');outputs.push(p);mapping.set(key(row.path),target);}
 const relocate=p=>mapping.get(key(p))??p,producer={...base.producer,path:relocate(base.producer.path)},launcher={...base.launcher,path:relocate(base.launcher.path)};
 const leafRoot=path.join(owned,'factory-initialization','positive','leaf-payload'),sourcePayload=path.dirname(base.host.path);const staging=stageFaultPayload(sourcePayload,leafRoot),supportCopies=fs.readdirSync(leafRoot).map(name=>{const p=path.join(leafRoot,name);need(fs.lstatSync(p).isFile(),'FACTORY_FLAT_PAYLOAD_REQUIRED');return {...filePin(p),source:path.join(sourcePayload,name)};});
 need(supportCopies.some(p=>path.basename(p.path)==='YeManFanBackend.dll'&&p.sha256===basis.images.backend),'FACTORY_BACKEND_SUPPORT_REQUIRED');
 const lightRoot=path.join(owned,'fixed-light-e546');fs.mkdirSync(lightRoot);const light={...base.light,path:path.join(lightRoot,'YeManLightSetter.dll')};fs.copyFileSync(base.light.path,light.path,fs.constants.COPYFILE_EXCL);checkedPin(light);
 const generatedHost=staging.mutatorOutput,portsProof=path.join(owned,'factory-ports-proof.json'),args=['factory-leaf-ports',base.host.path,generatedHost,portsProof],argvPath=path.join(outDir,'generation-argv.json');put(argvPath,{schemaVersion:1,kind:'Harness',producer:producer.path,args,argvSha256:digest(args),executed:false});
 const context=structuredClone(def);for(const p of context.managed)p.path=relocate(p.path);for(const r of context.requests)r.targetPath=relocate(r.targetPath);
 const policy={...structuredClone(bp),entry:producer.path,ownedRoot:owned,contexts:[context]};policy.metadata=unique([...bp.metadata.filter(p=>!same(p.path,base.argumentMetadata.path)).map(p=>({...p,path:relocate(p.path)})),filePin(argvPath),inputsPin,input.cleanRaw,input.currentSourceBuild,input.frameworkMetadata,input.basePreload,...Object.values(input.sources),base.host,base.backend,base.rawHc,light,...supportCopies]);
 const policyPath=path.join(outDir,'generation-policy.json');put(policyPath,policy);
 const plan={schemaVersion:1,kind:'onex-factory-leaf-ports-generation',status:'PREPARED_NOT_EXECUTED',ownedRoot:owned,producer,launcher,dotnet:base.dotnet,sourceHost:base.host,sourceBackend:base.backend,light,rawHc:base.rawHc,outputs,supportCopies,staging,generatedHost,portsProof,policy:filePin(policyPath),argumentMetadata:filePin(argvPath),args,argsSha256:digest(args),budgetMs:30000,replayBudgetMs:30000,parentBudgetMs:360000,launchReceipt:path.join(owned,'generation-launch-receipt.json'),metadataValidateReceipt:path.join(owned,'generation-metadata-validate.json'),prepareInputs:inputsPin,cleanRaw:input.cleanRaw,rawReader:input.rawReader,frameworkMetadata:input.frameworkMetadata,basePreload:input.basePreload,contract:factoryInitializationContract(),laterContracts:['readiness','continuation'].map(factoryInitializationContract),producerExecuted:false,targetExecuted:false,executionApprovalGranted:false,fullHostApproved:false};
 plan.dotnetArguments=[launcher.path,'run',policyPath,plan.policy.sha256,plan.launchReceipt,...args];plan.metadataValidateArguments=[launcher.path,'validate',policyPath,plan.policy.sha256,plan.metadataValidateReceipt];
 validateFactoryGenerationPlan(plan,policy,read(argvPath));all([...policy.metadata,...context.managed,...outputs]);sourceInputs(input);put(path.join(outDir,'FACTORY-INITIALIZATION-GENERATION-PLAN.json'),plan);return plan;
}
function generatedInputs({generationPin,proofPin,generatedPin}){
 all([generationPin,proofPin,generatedPin]);const generation=read(generationPin.path),proof=read(proofPin.path);all([generation.prepareInputs,generation.cleanRaw,generation.rawReader,generation.policy,generation.argumentMetadata,...generation.outputs,...generation.supportCopies]);const {base,bp}=sourceInputs(read(generation.prepareInputs.path));
 for(const role of ['producer','launcher','light','rawHc'])need(generation[role].sha256===basis.images[role],'FACTORY_GENERATION_ENTRY_DRIFT',role);
 need(same(generation.sourceHost.path,base.host.path)&&generation.sourceHost.sha256===base.host.sha256&&generation.frameworkMetadata.sha256===basis.frameworkMetadataSha256&&generation.basePreload.sha256===basis.basePreloadSha256,'FACTORY_GENERATION_SOURCE_BINDING');
 const sourceRows=new Map(base.outputs.map(p=>[key(p.path),p]));need(generation.outputs.length===37&&generation.outputs.every(p=>sourceRows.get(key(p.source))?.sha256===p.sha256&&inside(generation.ownedRoot,p.path)&&path.relative(generation.ownedRoot,p.path)===path.relative(base.ownedRoot,p.source)),'FACTORY_GENERATION_OUTPUT_LAYOUT');
 const gp=read(generation.policy.path);need(digest(normalizeOwned(gp.contexts[0],generation.ownedRoot))===basis.defaultContextSha256&&same(gp.entry,generation.producer.path)&&same(gp.ownedRoot,generation.ownedRoot),'FACTORY_GENERATION_POLICY_REBIND');
 validateFactoryGenerationPlan(generation,gp,read(generation.argumentMetadata.path),{requireFresh:false});
 need(generation.kind==='onex-factory-leaf-ports-generation'&&same(proofPin.path,generation.portsProof)&&same(generatedPin.path,generation.generatedHost),'FACTORY_EXACT_GENERATED_PATHS');need(proof.instrumentedSha256===generatedPin.sha256&&proof.sourceSha256===basis.images.host,'FACTORY_GENERATED_SHA');return {generation,proof};
}
// This creates only a metadata-reader request. The caller separately invokes
// the already reviewed fixed reader; this module never spawns a process.
export function prepareGeneratedFactoryRawRequest({mode,generationPin,proofPin,generatedPin,outDir}){
 need(mode==='prepare-generated-raw-request','FACTORY_EXACT_RAW_REQUEST_MODE');const {generation}=generatedInputs({generationPin,proofPin,generatedPin});need(inside(generation.ownedRoot,outDir),'FACTORY_RAW_REQUEST_OUTSIDE_STAGE');fresh(outDir);const source=read(generation.cleanRaw.path).rows.find(r=>r.sha256===basis.images.host),selectors=source.selections.map(s=>({type:s.type,methods:[...new Set(s.methods.map(m=>m.method.slice(m.method.indexOf('::')+2,m.method.indexOf('('))))]}));
 const contractPath=path.join(outDir,'generated-factory-raw-inputs.json'),resultPath=path.join(outDir,'generated-factory-raw-result.json');put(contractPath,{schemaVersion:1,targets:[{...generatedPin,assemblyName:'YeManFanHost',selectors}]});
 const request={status:'METADATA_REQUEST_PREPARED_NOT_EXECUTED',reader:generation.rawReader,program:generation.dotnet,args:[generation.rawReader.path,'read-exact-raw-methods',contractPath,resultPath],contract:filePin(contractPath),resultPath,generation:generationPin,generated:generatedPin,producerExecuted:false,targetExecuted:false};put(path.join(outDir,'GENERATED-RAW-REQUEST.json'),request);return request;
}
function generationReceiptCheck(g,r){
 need(r.exitCode===0&&r.failure===null&&Array.isArray(r.actualLoadedManaged)&&r.actualLoadedManaged.length>0,'FACTORY_GENERATION_RECEIPT_REQUIRED');
 need([g.producer,g.launcher].every(e=>r.actualLoadedManaged.some(p=>same(p.path,e.path)&&p.sha256===e.sha256)),'FACTORY_GENERATION_INFRASTRUCTURE_BINDING');
 const policy=read(g.policy.path);all([g.frameworkMetadata,g.basePreload]);const framework=read(g.frameworkMetadata.path),preload=read(g.basePreload.path);const extras=[{...g.launcher,identity:'MTA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'},...basis.baseClr.map(e=>{const found=[...framework.rows,...preload.actualLoadedManagedAfter].find(r=>r.identity===e.identity&&r.sha256===e.sha256&&same(r.path,e.path));need(found,'FACTORY_EXACT_FRAMEWORK_ROW_REQUIRED',e);return e;})];all(extras);
 for(const p of r.actualLoadedManaged)need(!p.dynamic&&[...policy.contexts[0].managed,...extras].some(e=>same(e.path,p.path)&&e.sha256===p.sha256&&e.identity===p.identity),'FACTORY_GENERATION_UNKNOWN_MANAGED',p);
 for(const p of r.actualLoadedManaged)need(!/^(YeManFanHost|YeManFanBackend|WinRT.Runtime|Microsoft.Windows.SDK.NET),/.test(p.identity)&&!(p.identity.startsWith('HandheldCompanion,')&&p.sha256!==basis.images.synthetic),'FACTORY_GENERATION_LOADED_TARGET');
}
export function prepareFactoryInitializationReplay({mode,generationPin,proofPin,generatedPin,rawRequestPin,rawReportPin,generationReceiptPin,outDir}){
 need(mode==='accept-generated-metadata-only','FACTORY_EXACT_ACCEPT_MODE');const {generation:g,proof}=generatedInputs({generationPin,proofPin,generatedPin});all([rawRequestPin,rawReportPin,generationReceiptPin]);const request=read(rawRequestPin.path);checkedPin(request.contract);const requestContract=read(request.contract.path);
 need(request.reader.sha256===basis.rawReaderSha256&&same(rawReportPin.path,request.resultPath)&&request.generated.sha256===generatedPin.sha256&&same(request.generated.path,generatedPin.path)&&request.generation.sha256===generationPin.sha256&&same(request.generation.path,generationPin.path)&&JSON.stringify(request.args)===JSON.stringify([g.rawReader.path,'read-exact-raw-methods',request.contract.path,request.resultPath]),'FACTORY_RAW_REQUEST_BINDING');
 need(requestContract.targets.length===1&&requestContract.targets[0].sha256===generatedPin.sha256&&same(requestContract.targets[0].path,generatedPin.path),'FACTORY_RAW_TARGET_BINDING');
 need(same(generationReceiptPin.path,g.launchReceipt),'FACTORY_GENERATION_RECEIPT_PATH');generationReceiptCheck(g,read(generationReceiptPin.path));
 const difference=validateFactoryRawDifference({sourceReport:read(g.cleanRaw.path),sourcePin:g.sourceHost,generatedReport:read(rawReportPin.path),generatedPin,proof});
 need(inside(g.ownedRoot,outDir),'FACTORY_REPLAY_OUTSIDE_STAGE');fresh(outDir);const policy=read(g.policy.path),def=policy.contexts[0],producerPin=def.managed.find(p=>same(p.path,g.producer.path)),host={...generatedPin,identity:basis.identities.host,role:'target'},backend={...g.supportCopies.find(p=>path.basename(p.path)==='YeManFanBackend.dll'),identity:basis.identities.backend,role:'target'};
 const custom=images=>{const id='Harness.ProductContext|'+key(path.dirname(images[0].path));return {id,kind:'custom',registrationType:'Harness+ProductContext',registrationAssembly:{...producerPin},managed:images,requests:[...images.map(p=>({requestedIdentity:p.identity,targetContext:id,targetPath:p.path})),...def.requests]};};policy.contexts=[def,custom([host,backend]),custom([{...g.light,role:'target'}])];
 const businessResult=path.join(outDir,'factory-init-result.json'),args=['factory-init',host.path,g.light.path,g.rawHc.path,businessResult],argvPath=path.join(outDir,'replay-argv.json');put(argvPath,{schemaVersion:1,kind:'Harness',producer:g.producer.path,args,argvSha256:digest(args),executed:false});
 policy.metadata=unique([...policy.metadata.filter(p=>!same(p.path,g.argumentMetadata.path)),filePin(argvPath),generationPin,proofPin,generatedPin,rawRequestPin,rawReportPin,generationReceiptPin]);const policyPath=path.join(outDir,'replay-policy.json');put(policyPath,policy);
 const plan={schemaVersion:1,kind:'onex-factory-initialization-positive-replay',status:'PREPARED_NOT_EXECUTED',ownedRoot:g.ownedRoot,producer:g.producer,launcher:g.launcher,dotnet:g.dotnet,host,backend,light:g.light,rawHc:g.rawHc,outputs:g.outputs,policy:filePin(policyPath),argumentMetadata:filePin(argvPath),args,argsSha256:digest(args),businessResult,launchReceipt:path.join(outDir,'replay-launch-receipt.json'),metadataValidateReceipt:path.join(outDir,'replay-metadata-validate.json'),budgetMs:30000,expectedExitCode:0,contract:factoryInitializationContract(),difference,generation:generationPin,producerExecuted:false,targetExecuted:false,executionApprovalGranted:false,fullHostApproved:false};
 plan.dotnetArguments=[g.launcher.path,'run',policyPath,plan.policy.sha256,plan.launchReceipt,...args];plan.metadataValidateArguments=[g.launcher.path,'validate',policyPath,plan.policy.sha256,plan.metadataValidateReceipt];all([...policy.metadata,...policy.contexts.flatMap(c=>c.managed)]);put(path.join(outDir,'FACTORY-INITIALIZATION-REPLAY-PLAN.json'),plan);return plan;
}
export function assessFactoryInitializationResult(result,{mode='positive',generatedHostSha256}={}){
 const c=factoryInitializationContract(mode);need(result.candidateHostSha256===generatedHostSha256&&/^[a-f0-9]{64}$/.test(generatedHostSha256??'')&&result.nativeAttempts===0&&result.hostMainStarted===false&&result.hardwareIo===false,'FACTORY_RESULT_IDENTITY_OR_IO');
 const rows=result.cases;need(Array.isArray(rows)&&rows.length===24&&new Set(rows.map(r=>r.id)).size===24&&ids(rows.map(r=>r.id))===ids(c.caseIds)&&rows.every(r=>['PASS','FAIL'].includes(r.status)),'FACTORY_EXACT_SOURCE_CASE_SET');const failed=rows.filter(r=>r.status==='FAIL');
 if(mode==='positive')need(result.status==='PASS'&&failed.length===0&&result.checks>=157,'FACTORY_ORIGINAL_POSITIVE_GATE');else need(result.status==='FAIL'&&ids(failed.map(r=>r.id))===ids(Object.keys(c.failureFragments))&&failed.every(r=>r.error?.includes(c.failureFragments[r.id])),'FACTORY_ORIGINAL_NEGATIVE_GATE');
 return {status:'PASS_ORIGINAL_FACTORY_CONTRACT',mode,cases:24,checks:result.checks,failedIds:failed.map(r=>r.id),businessExecutedByThisModule:false};
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){try{const a=process.argv.slice(2);need(a.length===8&&JSON.stringify(a.filter((_,i)=>i%2===0))===JSON.stringify(['--mode','--inputs','--sha256','--out-dir']),'FACTORY_EXACT_PREPARE_CLI');console.log(JSON.stringify(prepareFactoryInitialization({mode:a[1],inputsPin:{path:a[3],sha256:a[5]},outDir:a[7]})));}catch(e){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',code:e.code,error:e.message,detail:e.detail}));process.exitCode=3;}}
