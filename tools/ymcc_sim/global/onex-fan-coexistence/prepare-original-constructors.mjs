import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {checkedPin,filePin,read,need,digest} from './default-case-contract.mjs';
import {validateOriginalConstructorRaw} from './constructor-raw-contract.mjs';
import basis from './original-constructors-basis.json' with {type:'json'};

const key=p=>path.resolve(p).toUpperCase(),same=(a,b)=>key(a)===key(b);
const inside=(r,p)=>{const q=path.relative(r,p);return !!q&&!path.isAbsolute(q)&&q!=='..'&&!q.startsWith('..'+path.sep);};
const sorted=v=>JSON.stringify([...v].sort()),equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const put=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const all=pins=>pins.forEach(checkedPin);
function unique(rows){const out=new Map();for(const r of rows){const k=key(r.path);need(!out.has(k)||out.get(k).sha256===r.sha256,'CONSTRUCTORS_CONFLICTING_PIN',r);out.set(k,r);}return [...out.values()];}
function normalized(v,root){if(typeof v==='string'&&path.isAbsolute(v)&&(same(v,root)||inside(root,v)))return '$OWNED/'+path.relative(root,v).replaceAll('\\','/');if(Array.isArray(v))return v.map(x=>normalized(x,root));if(v&&typeof v==='object')return Object.fromEntries(Object.entries(v).map(([k,x])=>[k,normalized(x,root)]));return v;}
function exactPin(p,expected,code){checkedPin(p);need(p.sha256===expected.sha256&&same(p.path,expected.path),'CONSTRUCTORS_'+code,p);return p;}
function fresh(dir){need(path.isAbsolute(dir??'')&&!fs.existsSync(dir),'CONSTRUCTORS_FRESH_ROOT_REQUIRED');fs.mkdirSync(dir,{recursive:true});need(same(fs.realpathSync(dir),dir),'CONSTRUCTORS_REDIRECTED_ROOT');}

// This profile appends exactly three framework images to an independently
// fingerprinted Default44 component. Nothing is matched or approved by filename.
const desktopPins=()=>basis.desktopFramework.pins.map(p=>({...p}));
const defaultRequest=p=>({requestedIdentity:p.identity,targetContext:'Default',targetPath:p.path});
export function validateOriginalConstructorDefaultProfile(def,ownedRoot,baseComponent){
 const extensions=desktopPins();
 need(def.id==='Default'&&def.managed.length===47,'CONSTRUCTORS_DEFAULT44_PLUS_EXACT3');
 for(const pin of extensions){
  need(def.managed.filter(p=>equal(p,pin)).length===1,'CONSTRUCTORS_DESKTOP3_EXACT_PIN',pin.identity);
  need(def.requests.filter(r=>equal(r,defaultRequest(pin))).length===1,'CONSTRUCTORS_DESKTOP3_EXACT_REQUEST',pin.identity);
 }
 const base={...def,managed:def.managed.filter(p=>!extensions.some(e=>equal(p,e))),requests:def.requests.filter(r=>!extensions.some(e=>equal(r,defaultRequest(e))))};
 need(base.managed.length===44&&digest(normalized(base,ownedRoot))===basis.defaultContextSha256,'CONSTRUCTORS_DEFAULT44');
 need(baseComponent?.schemaVersion===1&&baseComponent.kind==='original-constructors-default44-base'&&same(baseComponent.ownedRoot,ownedRoot)&&baseComponent.normalizedContextSha256===basis.defaultContextSha256&&equal(baseComponent.context,base),'CONSTRUCTORS_DEFAULT44_COMPONENT');
 return {profileId:'original89-desktop3',basePins:44,extensionPins:3,totalPins:47,baseContextSha256:basis.defaultContextSha256};
}
function verifyDesktopMetadata(input){
 const report=read(input.desktopRaw.path);
 need(report.status==='PASS_RAW_SELECTED_METHOD_READ'&&report.targetAssembliesObserved===false&&report.targetCodeExecuted===false&&report.targetConstructorsInvoked===false&&report.externalResolutionAllowed===false&&report.rows.length===3,'CONSTRUCTORS_DESKTOP3_METADATA_ONLY');
 for(const pin of desktopPins()){checkedPin(pin);const rows=report.rows.filter(r=>same(r.path,pin.path)&&r.sha256===pin.sha256&&r.identity===pin.identity);need(rows.length===1,'CONSTRUCTORS_DESKTOP3_RAW_IDENTITY');}
}

// Independent source contract: inventory order plus the two literal tuple domains
// in the pinned original Consumer. No case names or expectations come from a run.
export function originalConstructorContract(){
 const scenarios=basis.inventoryRows.filter(r=>!r.inactive).map(r=>({type:r.type,id:r.type,biosMajor:1,biosMinor:1,processor:'AMD Ryzen 9 395',flags:null,clock:null}));
 for(const [model,major,minor,flags] of [['OneXPlayerX1AMD',1,2,56],['OneXPlayerX1AMD',1,3,504],['OneXPlayerX1AMD',2,0,56],['OneXPlayerX1Intel',0,66,56],['OneXPlayerX1Intel',0,67,504]]){
  const type='HandheldCompanion.Devices.'+model;scenarios.push({type,id:type+'@firmware-'+major+'.'+minor,biosMajor:major,biosMinor:minor,processor:'AMD Ryzen 9 395',flags,clock:null});
 }
 for(const [processor,clock] of [['AMD Ryzen AI Max 385',5000],['AMD Ryzen AI Max 395',5100]]){const type='HandheldCompanion.Devices.GPDWin5';scenarios.push({type,id:type+'@cpu-'+clock,biosMajor:1,biosMinor:1,processor,flags:4,clock});}
 const ids=scenarios.map(s=>'original-constructor:'+s.id),nativeIds=['valid','missing','invalid'].map(k=>'native-admission:'+k);
 const failures={capabilities:['ROGAlly','ROGAllyX','XboxROGAlly','XboxROGAllyX'].map(n=>'original-constructor:HandheldCompanion.Devices.'+n),hotkeys:[...ids],dispose:[...ids],native:nativeIds.slice(1)};
 return {mode:'original-constructors',selection:'all',caseIds:[...ids,...nativeIds],scenarios,caseCount:89,inventoryCount:80,activeConstructors:79,inactiveConstructors:1,constructorExecutions:86,nativeDeclarations:21,budgetMs:60000,parentBudgetMs:360000,expectedExitCode:0,minimumChecks:null,reservedHotkeyFlags:[...basis.reservedHotkeys],negativeRoles:Object.entries(failures).map(([mode,failedIds])=>({mode,failedIds,expectedExitCode:2,budgetMs:60000,errorFragment:{capabilities:'capability bits must satisfy',hotkeys:'four reserved default hotkey flags',dispose:'Dispose releases its owner handle',native:'Expected GamepadMotion.dll'}[mode],nativeMathFaultCleanup:mode==='dispose'?86:null})),targetExecutionApproved:false};
}

// Verbatim pure PE/ABI audit from the pinned original constructor-proof source.
function nativeInterface(file,inventory){
 const bytes=fs.readFileSync(file),pe=bytes.readUInt32LE(0x3c);if(bytes.toString('ascii',pe,pe+4)!=='PE\0\0'||bytes.readUInt16LE(pe+4)!==0x8664)throw Error('Pinned motion library must be an x64 PE');
 const optional=pe+24,sections=bytes.readUInt16LE(pe+6),size=bytes.readUInt16LE(pe+20);if(bytes.readUInt16LE(optional)!==0x20b)throw Error('PE32+ required');
 const spans=Array.from({length:sections},(_,i)=>{const at=optional+size+i*40;return{size:Math.max(bytes.readUInt32LE(at+8),bytes.readUInt32LE(at+16)),rva:bytes.readUInt32LE(at+12),raw:bytes.readUInt32LE(at+20)};});
 const offset=rva=>{const span=spans.find(s=>rva>=s.rva&&rva<s.rva+s.size);if(!span)throw Error('PE RVA outside a section');return span.raw+rva-span.rva;};
 const string=rva=>{const at=offset(rva),end=bytes.indexOf(0,at);if(end<0||end-at>4096)throw Error('Invalid PE string');return bytes.toString('ascii',at,end);};
 const rva=bytes.readUInt32LE(optional+112),length=bytes.readUInt32LE(optional+116),at=offset(rva),count=bytes.readUInt32LE(at+24),functions=offset(bytes.readUInt32LE(at+28)),names=offset(bytes.readUInt32LE(at+32)),ordinals=offset(bytes.readUInt32LE(at+36));
 const exports=Array.from({length:count},(_,i)=>{const name=string(bytes.readUInt32LE(names+i*4)),entry=bytes.readUInt32LE(functions+bytes.readUInt16LE(ordinals+i*2)*4);if(entry>=rva&&entry<rva+length)throw Error('Native math exports cannot forward outside the pinned file');return name;});
 const declared=inventory.nativeGamepadMotionDeclarations.map(m=>m.entryPoint);if(declared.length!==21||declared.some(n=>!exports.includes(n)))throw Error('Original managed motion ABI is missing a native export');
 // The original library is a software math dependency. Refuse any newly
 // introduced native module in its import table before executing it.
 const imports=[];const importRva=bytes.readUInt32LE(optional+120);if(importRva)for(let p=offset(importRva);bytes.readUInt32LE(p+12);p+=20){imports.push(string(bytes.readUInt32LE(p+12)));}
 const allowed=['VCRUNTIME140.dll','api-ms-win-crt-math-l1-1-0.dll','api-ms-win-crt-heap-l1-1-0.dll','api-ms-win-crt-runtime-l1-1-0.dll','KERNEL32.dll'];
 if(sorted(imports.map(n=>n.toLowerCase()))!==sorted(allowed.map(n=>n.toLowerCase())))throw Error('Pinned native math import set changed');
 return{status:'PASS',nativeSha256:filePin(file).sha256,machine:'x64',declaredCount:declared.length,exports,imports,forwardedExports:false,declaredMethods:inventory.nativeGamepadMotionDeclarations,hardwareIo:false};
}


export function validateOriginalConstructorNativeInterface({nativePin,inventory}){checkedPin(nativePin);need(nativePin.sha256===basis.images.nativeMath,'CONSTRUCTORS_NATIVE_MATH_BYTES');need(equal(inventory.nativeGamepadMotionDeclarations,basis.nativeDeclarations),'CONSTRUCTORS_TYPED_NATIVE_DECLARATIONS');return nativeInterface(nativePin.path,inventory);}

export function verifyOriginalConstructorManifests(runtimeRoot,sharedRoot){
 const pins={},members=[];
 for(const [root,name] of [[runtimeRoot,'HandheldCompanion.runtime.json'],[sharedRoot,'manifest.json']]){
  const manifest=path.join(root,name);checkedPin(filePin(manifest));pins[manifest]=filePin(manifest).sha256;members.push(filePin(manifest));
  const rows=read(manifest).files;need(Array.isArray(rows)&&rows.length>0,'CONSTRUCTORS_MANIFEST_ROWS');
  const seen=new Set();for(const row of rows){const file=path.resolve(root,row.packagedPath||row.path);need(inside(root,file)&&!seen.has(key(file)),'CONSTRUCTORS_MANIFEST_PATH_OR_DUPLICATE',file);seen.add(key(file));checkedPin({path:file,sha256:row.sha256});pins[file]=row.sha256;members.push({path:file,sha256:row.sha256});}
 }
 return {pins,members};
}
function verifyBuild(pin){
 exactPin(pin,basis.references.sourceBuild,'BUILD_PROVENANCE');const p=read(pin.path);all([p.inputBefore,p.inputAfter,p.compilerInputAudit,p.harness37]);need(p.sourceBound===true&&p.sourceFileCount===55&&p.host.sha256===basis.images.host&&p.backend.sha256===basis.images.backend,'CONSTRUCTORS_CURRENT55_BUILD');
 const before=read(p.inputBefore.path),after=read(p.inputAfter.path),audit=read(p.compilerInputAudit.path);need(after.sourceSetUnchanged===true&&after.currentGSourceStillMatchesCopiedBuild===true&&after.GWritten===false&&after.sourceCopies.length===55&&audit.uniqueCompilerInputs===267,'CONSTRUCTORS_SOURCE_COMPILER_GRAPH');
 need(equal(before.pins,after.pins),'CONSTRUCTORS_BUILD_INPUT_BEFORE_AFTER');
 for(const row of after.sourceCopies){checkedPin(row);checkedPin({path:row.copy,sha256:row.copySha256});need(row.sha256===row.copySha256,'CONSTRUCTORS_CURRENT_COPY_DRIFT');}
 for(const project of audit.projects){all([project.before,project.after,project.commandLineArguments,...project.pins]);need(project.before.sha256===project.after.sha256,'CONSTRUCTORS_COMPILER_INPUT_DRIFT');}
 return {sourceCount:55,uniqueCompilerInputs:267,before:p.inputBefore,after:p.inputAfter,audit:p.compilerInputAudit};
}
function preflight(input){
 need(input.schemaVersion===1&&input.kind==='original-constructors-prepare-inputs','CONSTRUCTORS_INPUT_SCHEMA');
 for(const [name,p] of Object.entries(basis.references)){need(input[name],'CONSTRUCTORS_REFERENCE_MISSING',name);exactPin(input[name],p,'REFERENCE_'+name);}
 for(const [name,p] of Object.entries(basis.sources)){need(input.sources?.[name],'CONSTRUCTORS_SOURCE_MISSING',name);exactPin(input.sources[name],p,'SOURCE_'+name);}
 for(const row of basis.compiledSources){all([row.current,row.compiledSource]);need(row.current.sha256===row.compiledSource.sha256,'CONSTRUCTORS_COMPILED_SOURCE_DRIFT');}
 verifyDesktopMetadata(input);
 const b=read(input.basePlan.path);all([...b.outputs,b.producer,b.launcher,b.dotnet,b.sourceHost,b.sourceBackend,b.frameworkMetadata,b.basePreload,b.policy]);
 const bp=read(b.policy.path),def=bp.contexts[0];need(bp.contexts.length===1&&def.id==='Default'&&def.managed.length===44&&digest(normalized(def,b.ownedRoot))===basis.defaultContextSha256,'CONSTRUCTORS_DEFAULT44_SOURCE');
 const actual=b.outputs.map(r=>({relative:path.relative(b.ownedRoot,r.path).replaceAll('\\','/'),sha256:r.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative));need(equal(actual,basis.original37),'CONSTRUCTORS_ORIGINAL37');
 need(b.producer.sha256===basis.images.producer&&b.launcher.sha256===basis.images.launcher,'CONSTRUCTORS_ORIGINAL_TOOL_BYTES');
 need(input.generatedHc.sha256===basis.images.generatedHc&&input.sourceHc.sha256===basis.images.rawHc&&input.host.sha256===basis.images.host&&input.backend.sha256===basis.images.backend,'CONSTRUCTORS_FIXED_IMAGES');all([input.host,input.backend,input.dotnet]);
 need(same(path.dirname(input.sourceHc.path),input.runtimeRoot),'CONSTRUCTORS_RUNTIME_ROOT');
 const inv=read(input.inventory.path);need(inv.status==='PASS_INVENTORY_ONLY'&&inv.hcSha256===basis.images.rawHc&&inv.factoryConstructors===80&&equal(inv.rows.map(v=>({type:v.type,inactive:v.inactive,originalConstructor:v.originalConstructor})),basis.inventoryRows)&&inv.constructorsExecuted===false&&inv.nativeCodeExecuted===false,'CONSTRUCTORS_INVENTORY80_79');
 need(equal(inv.nativeGamepadMotionDeclarations,basis.nativeDeclarations),'CONSTRUCTORS_TYPED_NATIVE_DECLARATIONS');
 const raw=read(input.raw.path),difference=validateOriginalConstructorRaw({sourceReport:raw,sourcePin:input.sourceHc,generatedReport:raw,generatedPin:input.generatedHc,proof:read(input.portsProof.path)});
 const manifest=verifyOriginalConstructorManifests(input.runtimeRoot,input.sharedRoot);need(manifest.pins[input.sourceHc.path]===basis.images.rawHc,'CONSTRUCTORS_RAW_HC_MANIFEST');
 const nativeMath=path.join(input.runtimeRoot,'GamepadMotion.dll');need(filePin(nativeMath).sha256===basis.images.nativeMath,'CONSTRUCTORS_NATIVE_MATH_BYTES');const native=validateOriginalConstructorNativeInterface({nativePin:filePin(nativeMath),inventory:inv});
 const depRaw=read(input.dependencyRaw.path);need(depRaw.status==='PASS_RAW_SELECTED_METHOD_READ'&&depRaw.targetAssembliesObserved===false&&depRaw.targetCodeExecuted===false&&depRaw.externalResolutionAllowed===false,'CONSTRUCTORS_DEPENDENCY_METADATA_ONLY');
 for(const d of basis.dependencies){checkedPin(d);const rows=depRaw.rows.filter(v=>same(v.path,d.path)&&v.sha256===d.sha256);need(rows.length===1&&rows[0].identity===d.identity,'CONSTRUCTORS_EXACT_DEPENDENCY_IDENTITY',d);}
 const passive=read(input.passiveProjection.path);all([...passive.inputReports,...passive.sources]);need(passive.actualMetadataProof?.status==='PASS_EXACT12_PASSIVE_MANAGED_PROJECTION_REGISTRATION'&&passive.actualMetadataProof.methods===12&&passive.actualMetadataProof.sdkFeatureInitializationApproved===false&&passive.targetExecutions===0,'CONSTRUCTORS_PASSIVE_PROJECTION_PROOF');
 const old=read(input.oldResult.path);need(old.status==='PASS'&&old.hcSha256===basis.images.generatedHc&&old.cases.length===89,'CONSTRUCTORS_HISTORICAL_BINDING_BASIS');
 return {base:b,policy:bp,def,manifest,native,difference,provenance:verifyBuild(input.sourceBuild)};
}

export function prepareOriginalConstructors({mode,inputsPin,outDir}){
 need(mode==='prepare-only','CONSTRUCTORS_EXPLICIT_PREPARE_ONLY');checkedPin(inputsPin);const input=read(inputsPin.path),v=preflight(input);fresh(outDir);
 const owned=path.join(outDir,'owned-stage');fs.mkdirSync(owned);const outputs=[],mapping=new Map();
 for(const row of v.base.outputs){need(inside(v.base.ownedRoot,row.path),'CONSTRUCTORS_OUTPUT_ROOT');const p=path.join(owned,path.relative(v.base.ownedRoot,row.path));fs.mkdirSync(path.dirname(p),{recursive:true});fs.copyFileSync(row.path,p,fs.constants.COPYFILE_EXCL);const copy={...filePin(p),source:row.path};need(copy.sha256===row.sha256,'CONSTRUCTORS_COPY_DRIFT');outputs.push(copy);mapping.set(key(row.path),p);}
 const relocate=p=>mapping.get(key(p))??p,producer={...v.base.producer,path:relocate(v.base.producer.path)},launcher={...v.base.launcher,path:relocate(v.base.launcher.path)},def=structuredClone(v.def);
 for(const p of def.managed)p.path=relocate(p.path);for(const r of def.requests)r.targetPath=relocate(r.targetPath);
 const baseComponent={schemaVersion:1,kind:'original-constructors-default44-base',ownedRoot:owned,normalizedContextSha256:basis.defaultContextSha256,context:structuredClone(def)};
 const defaultBasePath=path.join(outDir,'original-constructors-default44-base.json');put(defaultBasePath,baseComponent);const defaultBase=filePin(defaultBasePath);
 def.managed.push(...desktopPins());def.requests.push(...desktopPins().map(defaultRequest));
 const defaultProfile=validateOriginalConstructorDefaultProfile(def,owned,baseComponent);
 const realHc={...input.generatedHc,identity:basis.identity,role:'target'},deps=basis.dependencies.map(({path,sha256,identity,role})=>({path,sha256,identity,role}));
 const requests=(id,pins)=>pins.map(p=>({requestedIdentity:p.identity,targetContext:id,targetPath:p.path}));
 const constructorIdentities=new Set([realHc,...deps].map(p=>p.identity));
 const constructor={id:'ConstructorContext',kind:'custom',registrationType:'Harness+ConstructorContext',registrationAssembly:{...producer,identity:def.managed.find(p=>same(p.path,producer.path)).identity},managed:[realHc,...deps],requests:[...requests('ConstructorContext',[realHc,...deps]),...requests('Default',def.managed.filter(p=>!constructorIdentities.has(p.identity)))]};
 const productId='Harness.ProductContext|'+key(path.dirname(input.host.path)),product={id:productId,kind:'custom',registrationType:'Harness+ProductContext',registrationAssembly:constructor.registrationAssembly,managed:[{...input.host,role:'target'},{...input.backend,role:'target'}],requests:[...requests(productId,[input.host,input.backend]),...requests('Default',def.managed)]};
 // Results and all side-effect roots start absent. The original consumer owns
 // creating Build/settings/logs/cache/calibration and native-admission children.
 const buildRoot=path.join(owned,'Build','original-constructors'),businessResult=path.join(buildRoot,'result.json'),settingsRoot=path.join(buildRoot,'constructor-owned-settings');
 const runtimePins={...v.manifest.pins,[input.generatedHc.path]:input.generatedHc.sha256},runtimePinsPath=path.join(outDir,'runtime-pins.json');put(runtimePinsPath,runtimePins);
 const args=['original-constructors',realHc.path,input.runtimeRoot,input.sharedRoot,input.inventory.path,runtimePinsPath,businessResult,'all',input.catalog.path,input.host.path],argvPath=path.join(outDir,'original-constructors-argv.json');put(argvPath,{schemaVersion:1,kind:'Harness',producer:producer.path,args,argvSha256:digest(args),executed:false});
 const prepareTool=filePin(fileURLToPath(import.meta.url)),prepareBasis=filePin(fileURLToPath(new URL('./original-constructors-basis.json',import.meta.url)));
 const policy={schemaVersion:2,ownedRoot:owned,entry:producer.path,contexts:[def,constructor,product],metadata:unique([prepareTool,prepareBasis,defaultBase,...v.policy.metadata.filter(p=>!same(p.path,v.base.argumentMetadata.path)).map(p=>({...p,path:relocate(p.path)})),inputsPin,...Object.values(basis.references),...Object.values(input.sources),...v.manifest.members,filePin(runtimePinsPath),filePin(argvPath),input.host,input.backend])};
 const policyPath=path.join(outDir,'original-constructors-policy.json');put(policyPath,policy);
 const unresolved=[{id:'CURRENT_CONSTRUCTOR_DEPENDENCY_REQUEST_CLOSURE',reason:'Original Default44 base retained exactly, with only three pinned Desktop framework extensions (total47). Historical eight custom bindings constrain candidates, but do not prove every current framework request or cache/TPA alias. Unknown requests still reject.'},{id:'CURRENT89_RUNTIME_RESULT_REQUIRED',reason:'Old89 uses identical HC/math but a different Host/loader. Current actual89, OS/native-math and manager/cleanup receipts are parent work.'}];
 const plan={schemaVersion:1,kind:'onex-original-constructors-positive',status:'PREPARED_METADATA_ONLY_BUSINESS_BLOCKED',prepareTool,prepareBasis,defaultBase,defaultProfile,prepareInputs:inputsPin,ownedRoot:owned,producer,launcher,dotnet:input.dotnet,outputs,host:input.host,backend:input.backend,generatedHc:realHc,rawHc:input.sourceHc,runtimeRoot:input.runtimeRoot,sharedRoot:input.sharedRoot,frameworkMetadata:input.frameworkMetadata,basePreload:input.basePreload,policy:filePin(policyPath),argumentMetadata:filePin(argvPath),runtimePins:filePin(runtimePinsPath),args,argsSha256:digest(args),businessResult,buildRoot,settingsRoot,freshPaths:[buildRoot,settingsRoot,path.join(settingsRoot,'Calibration'),path.join(buildRoot,'native-admission')],launchReceipt:path.join(outDir,'original-constructors-launch.json'),metadataValidateReceipt:path.join(outDir,'original-constructors-metadata-validate.json'),budgetMs:60000,parentBudgetMs:360000,expectedExitCode:0,contract:originalConstructorContract(),provenance:v.provenance,passiveProjection:{proof:input.passiveProjection,status:'PASS_EXACT12_PASSIVE_MANAGED_PROJECTION_REGISTRATION',sdkFeatureInitializationApproved:false,wholeConstructorConsumerApproved:false},rawDifference:v.difference,nativeInterface:v.native,unresolved,metadataCandidatePinsAreNotRuntimeApproval:true,producerExecuted:false,targetExecuted:false,nativeMathExecuted:false,executionApprovalGranted:false,fullHostApproved:false};
 plan.dotnetArguments=[launcher.path,'run',policyPath,plan.policy.sha256,plan.launchReceipt,...args];plan.metadataValidateArguments=[launcher.path,'validate',policyPath,plan.policy.sha256,plan.metadataValidateReceipt];
 validateOriginalConstructorPlan(plan);all(policy.metadata);all(policy.contexts.flatMap(c=>c.managed));all(outputs);put(path.join(outDir,'ORIGINAL-CONSTRUCTORS-PREPARED-PLAN.json'),plan);return plan;
}

export function validateOriginalConstructorPlan(plan,{requireFresh=true}={}){
 need(plan.schemaVersion===1&&plan.kind==='onex-original-constructors-positive'&&plan.status==='PREPARED_METADATA_ONLY_BUSINESS_BLOCKED'&&plan.executionApprovalGranted===false,'CONSTRUCTORS_PLAN_KIND');
 all([plan.prepareTool,plan.prepareBasis,plan.defaultBase,plan.policy,plan.argumentMetadata,plan.runtimePins,plan.prepareInputs,plan.generatedHc,plan.host,plan.backend,...plan.outputs]);
 need(plan.budgetMs===60000&&plan.parentBudgetMs===360000&&plan.expectedExitCode===0,'CONSTRUCTORS_ORIGINAL_BUDGET');need(equal(plan.contract,originalConstructorContract()),'CONSTRUCTORS_SOURCE89_CONTRACT');
 const input=read(plan.prepareInputs.path);for(const [name,pin] of Object.entries(basis.references))exactPin(input[name],pin,'REFERENCE_'+name);for(const [name,pin] of Object.entries(basis.sources))exactPin(input.sources?.[name],pin,'SOURCE_'+name);
 need(same(plan.generatedHc.path,input.generatedHc.path)&&same(plan.host.path,input.host.path)&&same(plan.backend.path,input.backend.path)&&same(plan.runtimeRoot,input.runtimeRoot)&&same(plan.sharedRoot,input.sharedRoot),'CONSTRUCTORS_PLAN_INPUT_BINDING');
 const p=read(plan.policy.path);need(p.schemaVersion===2,'CONSTRUCTORS_AUTHORITY_SCHEMA2');const a=read(plan.argumentMetadata.path),expected=['original-constructors',plan.generatedHc.path,plan.runtimeRoot,plan.sharedRoot,input.inventory.path,plan.runtimePins.path,plan.businessResult,'all',input.catalog.path,plan.host.path];
 need(equal(plan.args,expected)&&equal(a.args,expected)&&a.argvSha256===digest(expected)&&a.kind==='Harness'&&same(a.producer,plan.producer.path)&&plan.argsSha256===digest(expected),'CONSTRUCTORS_EXACT_ARGV');
 need(equal(plan.dotnetArguments,[plan.launcher.path,'run',plan.policy.path,plan.policy.sha256,plan.launchReceipt,...expected])&&equal(plan.metadataValidateArguments,[plan.launcher.path,'validate',plan.policy.path,plan.policy.sha256,plan.metadataValidateReceipt]),'CONSTRUCTORS_EXACT_MTA_VECTORS');
 need(p.contexts.length===3&&same(p.entry,plan.producer.path)&&same(p.ownedRoot,plan.ownedRoot),'CONSTRUCTORS_POLICY_ROOT');const [def,ctx,product]=p.contexts;
 const defaultProfile=validateOriginalConstructorDefaultProfile(def,plan.ownedRoot,read(plan.defaultBase.path));need(equal(plan.defaultProfile,defaultProfile),'CONSTRUCTORS_DESKTOP3_PLAN_PROFILE');
 const expectedReal={...input.generatedHc,identity:basis.identity,role:'target'},deps=basis.dependencies.map(({path,sha256,identity,role})=>({path,sha256,identity,role}));need(ctx.id==='ConstructorContext'&&ctx.kind==='custom'&&ctx.registrationType==='Harness+ConstructorContext'&&same(ctx.registrationAssembly.path,plan.producer.path)&&ctx.registrationAssembly.sha256===basis.images.producer&&equal(ctx.managed,[expectedReal,...deps]),'CONSTRUCTORS_EXACT_CUSTOM_BINDINGS');
 const req=(id,rows)=>rows.map(r=>({requestedIdentity:r.identity,targetContext:id,targetPath:r.path}));need(equal(ctx.requests,[...req(ctx.id,ctx.managed),...req('Default',def.managed.filter(p=>!new Set(ctx.managed.map(r=>r.identity)).has(p.identity)))]),'CONSTRUCTORS_EXACT_CUSTOM_REQUESTS');
 need(product.id==='Harness.ProductContext|'+key(path.dirname(plan.host.path))&&product.registrationType==='Harness+ProductContext'&&equal(product.registrationAssembly,ctx.registrationAssembly)&&equal(product.managed,[{...input.host,role:'target'},{...input.backend,role:'target'}])&&equal(product.requests,[...req(product.id,[input.host,input.backend]),...req('Default',def.managed)]),'CONSTRUCTORS_PRODUCT_CONTEXT');
 need(def.managed.find(r=>r.identity.startsWith('HandheldCompanion,'))?.sha256===basis.images.syntheticHc&&ctx.managed[0].sha256===basis.images.generatedHc&&ctx.managed[0].identity===basis.identity&&p.contexts.flatMap(c=>c.managed).every(r=>r.sha256!==basis.images.rawHc),'CONSTRUCTORS_NO_HC_SIMPLE_NAME_MERGE');
 const layouts=plan.outputs.map(r=>({relative:path.relative(plan.ownedRoot,r.path).replaceAll('\\','/'),sha256:r.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative));need(equal(layouts,basis.original37)&&plan.outputs.every(r=>inside(plan.ownedRoot,r.path)),'CONSTRUCTORS_PLAN_ORIGINAL37_LAYOUT');
 need(plan.outputs.length===37&&plan.producer.sha256===basis.images.producer&&plan.launcher.sha256===basis.images.launcher&&plan.host.sha256===basis.images.host&&plan.backend.sha256===basis.images.backend,'CONSTRUCTORS_FIXED_PLAN_IMAGES');
 need(inside(plan.ownedRoot,plan.businessResult)&&inside(plan.buildRoot,plan.businessResult)&&path.basename(plan.buildRoot)==='original-constructors'&&plan.buildRoot.split(path.sep).some(s=>s.toLowerCase()==='build'),'CONSTRUCTORS_OWNED_BUILD_RESULT');
 const expectedBuild=path.join(plan.ownedRoot,'Build','original-constructors'),expectedSettings=path.join(expectedBuild,'constructor-owned-settings');need(same(plan.buildRoot,expectedBuild)&&same(plan.settingsRoot,expectedSettings)&&same(plan.businessResult,path.join(expectedBuild,'result.json'))&&equal(plan.freshPaths,[expectedBuild,expectedSettings,path.join(expectedSettings,'Calibration'),path.join(expectedBuild,'native-admission')]),'CONSTRUCTORS_EXACT_FRESH_PATHS');
 const requiredMetadata=[plan.prepareTool,plan.prepareBasis,plan.defaultBase,plan.prepareInputs,plan.argumentMetadata,plan.runtimePins,...Object.values(basis.references),...Object.values(input.sources),plan.host,plan.backend];for(const pin of requiredMetadata)need(p.metadata.some(v=>same(v.path,pin.path)&&v.sha256===pin.sha256),'CONSTRUCTORS_REQUIRED_METADATA_PIN',pin);all(p.metadata);all(p.contexts.flatMap(c=>c.managed));
 for(const f of [plan.businessResult,plan.launchReceipt,plan.metadataValidateReceipt,...plan.freshPaths]){need(inside(plan.ownedRoot,f)||inside(path.dirname(plan.policy.path),f),'CONSTRUCTORS_OUTPUT_ESCAPE',f);if(requireFresh)need(!fs.existsSync(f),'CONSTRUCTORS_ALREADY_CONSUMED_OUTPUT',f);}
 return {status:'PASS_PREPARE_METADATA_CONTRACT_ONLY',caseCount:89,defaultPins:47,defaultBasePins:44,defaultExtensionPins:3,customConstructorPins:ctx.managed.length,customProductPins:2,targetExecuted:false,businessApproved:false};
}

export function assessOriginalConstructorResult(result,{hcSha256,hostSha256,settingsRoot,mode='positive'}={}){
 const c=originalConstructorContract(),r=result;need(/^[a-f0-9]{64}$/.test(hcSha256??'')&&/^[a-f0-9]{64}$/.test(hostSha256??'')&&r.hcSha256===hcSha256&&r.candidateHostSha256===hostSha256,'CONSTRUCTORS_RESULT_IMAGE');
 need(r.rejectedNativeRequests===0&&r.originalDeviceConstructorBodiesExecuted===true&&r.actualHostNativeAdmissionExecuted===true&&r.nativeMathCodeExecuted===true&&r.managerStartCalled===false&&r.actualDeviceOpenBodyExecuted===false&&r.hostMainStarted===false&&r.hardwareIo===false,'CONSTRUCTORS_ORIGINAL_EXECUTION_SCOPE');
 need(Array.isArray(r.cases)&&r.cases.length===89&&new Set(r.cases.map(v=>v.id)).size===89&&sorted(r.cases.map(v=>v.id))===sorted(c.caseIds)&&r.cases.every(v=>['PASS','FAIL'].includes(v.status)),'CONSTRUCTORS_EXACT89_CASE_SET');
 if(settingsRoot!==undefined)need(same(settingsRoot,r.settingsRoot),'CONSTRUCTORS_RESULT_SETTINGS_ROOT');const failed=r.cases.filter(v=>v.status==='FAIL');
 if(mode==='positive'){
  need(r.status==='PASS'&&failed.length===0&&r.nativeMathFaultCleanup===0,'CONSTRUCTORS_ORIGINAL_POSITIVE');need(equal(r.cases.map(v=>v.id),c.caseIds),'CONSTRUCTORS_SOURCE_CASE_ORDER');
 }else{const neg=c.negativeRoles.find(v=>v.mode===mode);need(neg,'CONSTRUCTORS_UNKNOWN_FAULT_ROLE');need(r.status==='FAIL'&&sorted(failed.map(v=>v.id))===sorted(neg.failedIds)&&failed.every(v=>v.error?.includes(neg.errorFragment)),'CONSTRUCTORS_ORIGINAL_NEGATIVE');if(mode==='dispose')need(r.nativeMathFaultCleanup===86,'CONSTRUCTORS_DISPOSE_FAULT_CLEANUP');}
 const mask=names=>names.reduce((n,name)=>(need(Object.hasOwn(basis.capabilityBits,name),'CONSTRUCTORS_UNKNOWN_CAPABILITY'),n|basis.capabilityBits[name]),0);
 for(const s of c.scenarios){const row=r.cases.find(v=>v.id==='original-constructor:'+s.id);if(row.status!=='PASS')continue;const v=row.snapshot,bound=basis.catalogBounds.find(v=>v.type===s.type);need(v&&v.type===s.type&&v.biosMajor===s.biosMajor&&v.biosMinor===s.biosMinor&&v.processor===s.processor,'CONSTRUCTORS_TYPED_SCENARIO');need(Number.isInteger(v.capabilities)&&v.capabilities>=0&&v.capabilities<=65535&&(v.capabilities&mask(bound.minimum))===mask(bound.minimum)&&(v.capabilities&~mask(bound.possible))===0,'CONSTRUCTORS_TYPED_CAPABILITIES');need(s.flags===null||v.capabilities===s.flags,'CONSTRUCTORS_TYPED_FIRMWARE');need(s.clock===null||v.cpu===s.clock,'CONSTRUCTORS_TYPED_CPU');need(Number.isInteger(v.profileCount)&&v.profileCount>=1&&Number.isInteger(v.hotkeyCount)&&v.hotkeyCount>=4&&Array.isArray(v.ports),'CONSTRUCTORS_TYPED_SNAPSHOT');}
 return {status:'PASS_ORIGINAL89_RESULT_CONTRACT',mode,cases:89,checks:r.checks,failedIds:failed.map(v=>v.id),assessmentOnly:true,checkCountThresholdInvented:false,runtimeApprovalGranted:false};
}

if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){try{const a=process.argv.slice(2);need(a.length===8&&equal(a.filter((_,i)=>i%2===0),['--mode','--inputs','--sha256','--out-dir']),'CONSTRUCTORS_EXACT_PREPARE_CLI');console.log(JSON.stringify(prepareOriginalConstructors({mode:a[1],inputsPin:{path:a[3],sha256:a[5]},outDir:a[7]})));}catch(e){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',code:e.code,error:e.message,detail:e.detail}));process.exitCode=3;}}
