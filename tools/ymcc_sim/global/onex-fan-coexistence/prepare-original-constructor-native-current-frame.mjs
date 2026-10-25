// Source-only native-fault consumer preparation. No compiler, spawn, reflection or target loader.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {checkedPin,filePin,read,need,digest} from './default-case-contract.mjs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import {prepareNativeFaultReplay,validateNativeFaultGenerationPlan,validateNativeFaultGenerationReceipt,assessNativeFaultResult} from './prepare-original-constructor-native-fault.mjs';
import {validateNativeFaultDifference,verifyNativeFaultGeneratorCil,nativeConstructorFaultContract} from './original-constructor-native-fault-contract.mjs';
import {validatePositiveCurrentFrame,projectCurrentFramePolicy,samePath,within,exactPin} from './original-constructor-native-current-frame-contract.mjs';
import basis from './original-constructor-native-fault-basis.json' with {type:'json'};
const all=pins=>pins.forEach(checkedPin),put=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function fresh(p){need(path.isAbsolute(p??'')&&!fs.existsSync(p),'CURRENT_FRAME_FRESH_ROOT');for(let at=path.dirname(p);;){need(fs.existsSync(at)&&!fs.lstatSync(at).isSymbolicLink(),'CURRENT_FRAME_NO_REPARSE');const next=path.dirname(at);if(next===at)break;at=next;}fs.mkdirSync(p);need(samePath(fs.realpathSync(p),p),'CURRENT_FRAME_REDIRECTED_ROOT');}
export function nativeFaultConsumerPrepareForProfile(profile='schema2-original47'){
 if(profile==='schema2-original47')return prepareNativeFaultReplay;
 need(profile==='validated-current-frame-schema4-5','CURRENT_FRAME_EXPLICIT_PROFILE');return prepareNativeFaultCurrentFrame;
}
export {assessNativeFaultResult};
export function readExplicitPositiveCurrentFrame({positivePlanPin,positiveResultPin,provenancePin}){
 all([positivePlanPin,positiveResultPin,provenancePin]);const plan=read(positivePlanPin.path),result=read(positiveResultPin.path),provenance=read(provenancePin.path);
 need(provenance.evidenceKind==='ACTUAL_PARENT_POSITIVE','CURRENT_FRAME_MODEL_CANNOT_ADMIT');const pins=provenance.pins;
 const required=['positivePlan','positiveResult','policy','originalPlan','derivedDefault44','authorityReview','process','launch','metadataProcess','metadataReceipt','preload','preloadProcess','preloadReceipt'];for(const name of required){need(pins[name],'CURRENT_FRAME_MISSING_PROVENANCE_'+name);checkedPin(pins[name]);}
 need(exactPin(pins.positivePlan,positivePlanPin)&&exactPin(pins.positiveResult,positiveResultPin),'CURRENT_FRAME_EXPLICIT_INPUT_PINS');
 const f={positivePlanPin,positiveResultPin,provenancePin,plan,result,provenance,policy:read(pins.policy.path),originalPlan:read(pins.originalPlan.path),derivedDefault44:read(pins.derivedDefault44.path)};
 f.originalPolicy=read(checkedPin(f.originalPlan.policy).path);for(const name of ['process','launch','metadataProcess','metadataReceipt','preload','preloadProcess'])f[name]=read(pins[name].path);
 f.assessment=validatePositiveCurrentFrame(f); // This must succeed before any copy or output directory creation.
 const review=read(pins.authorityReview.path),reviewPins=[];function collect(v){if(v&&typeof v==='object'){if(v.path&&v.sha256)reviewPins.push(v);for(const x of Object.values(v))collect(x);}}collect(review);
 for(const source of Object.values(provenance.authorityOutputs)){checkedPin(source);need(reviewPins.some(p=>exactPin(p,source)),'CURRENT_FRAME_AUTHORITY_COMPILED_SOURCE_PROVENANCE');}
 all([plan.dotnet,plan.launcher,plan.producer,plan.runtimePins,plan.argumentMetadata,...plan.outputs,...provenance.outputOrigins.map(x=>x.source),...f.policy.metadata,...f.policy.contexts.flatMap(c=>c.managed),...f.policy.exactAliases.map(a=>a.provider)]);
 const runtime=read(plan.runtimePins.path);all(Object.entries(runtime).map(([path,sha256])=>({path,sha256})));
 const argv=read(plan.argumentMetadata.path);need(argv.kind==='Harness'&&samePath(argv.producer,plan.producer.path)&&JSON.stringify(argv.args)===JSON.stringify(plan.args)&&argv.argvSha256===digest(plan.args),'CURRENT_FRAME_CURRENT_ARGUMENT_METADATA');return f;
}
function validateGeneratedNativeFault(i){
 const names=['generationPin','proofPin','generatedPin','rawRequestPin','rawReportPin','generationReceiptPin'];all(names.map(k=>i[k]));
 const g=read(i.generationPin.path),proof=read(i.proofPin.path),old=read(checkedPin(g.originalPlan).path);
 need(g.originalPlan.sha256===basis.references.originalPlan.sha256&&exactPin(g.sourceHost,old.host)&&g.sourceHost.sha256===basis.images.host&&g.hc.sha256===basis.images.hc&&exactPin(g.cleanRaw,basis.references.currentRaw)&&exactPin(g.sourceBackend,old.backend)&&exactPin(g.dotnet,old.dotnet)&&samePath(g.runtimeRoot,old.runtimeRoot)&&samePath(g.sharedRoot,old.sharedRoot),'CURRENT_FRAME_ORIGINAL_GENERATOR_INPUTS');
 all([g.policy,g.argumentMetadata,g.sourceHost,g.sourceBackend,g.producer,g.launcher,g.dotnet,g.cleanRaw,...g.copies,...g.supportCopies]);
 validateNativeFaultGenerationPlan(g,read(g.policy.path),read(g.argumentMetadata.path),{requireFresh:false});
 const copied=g.copies.map(x=>({relative:path.relative(g.ownedRoot,x.path).replaceAll('\\','/'),sha256:x.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative));need(JSON.stringify(copied)===JSON.stringify(basis.original37)&&g.copies.every(p=>within(g.ownedRoot,p.path)),'CURRENT_FRAME_GENERATOR_COMPLETE37');
 need(samePath(i.generatedPin.path,g.generatedHost)&&samePath(i.proofPin.path,g.proofPath)&&samePath(i.generationReceiptPin.path,g.launchReceipt),'CURRENT_FRAME_GENERATED_EXACT_PATHS');
 validateNativeFaultGenerationReceipt(g,read(i.generationReceiptPin.path));
 const cleanRaw=read(g.cleanRaw.path),authority=cleanRaw.rows.find(x=>x.sha256===basis.images.authority);verifyNativeFaultGeneratorCil(cleanRaw,{producer:old.producer,authority:{path:authority.path,sha256:authority.sha256}});
 const request=read(i.rawRequestPin.path);all([request.contract,request.reader,request.program]);
 need(exactPin(request.generation,i.generationPin)&&exactPin(request.generated,i.generatedPin)&&exactPin(request.proof,i.proofPin)&&exactPin(request.reader,basis.references.rawReader)&&exactPin(request.program,g.dotnet)&&samePath(request.resultPath,i.rawReportPin.path)&&JSON.stringify(request.args)===JSON.stringify([request.reader.path,'read-exact-raw-methods',request.contract.path,request.resultPath]),'CURRENT_FRAME_MUTANT_RAW_REQUEST');
 const rc=read(request.contract.path);need(rc.targets.length===1&&exactPin(rc.targets[0],i.generatedPin)&&JSON.stringify(rc.targets[0].selectors)===JSON.stringify(basis.selectors),'CURRENT_FRAME_MUTANT_RAW26');
 const difference=validateNativeFaultDifference({sourceReport:cleanRaw,sourcePin:g.sourceHost,generatedReport:read(i.rawReportPin.path),generatedPin:i.generatedPin,proof});
 need(g.supportCopies.length===13&&new Set(g.supportCopies.map(p=>path.resolve(p.source).toUpperCase())).size===13&&new Set(g.supportCopies.map(p=>path.resolve(p.path).toUpperCase())).size===13,'CURRENT_FRAME_EXACT13_ADJACENCIES');const cleanDir=path.dirname(g.sourceHost.path);for(const p of g.supportCopies){need(within(cleanDir,p.source)&&within(g.ownedRoot,p.path)&&path.basename(p.path)!=='YeManFanHost.dll','CURRENT_FRAME_OWNED_ADJACENCY');checkedPin({path:p.source,sha256:p.sha256});}return {g,difference};
}
export function prepareNativeFaultCurrentFrame(input){
 need(input.profile==='validated-current-frame-schema4-5'&&input.mode==='prepare-only','CURRENT_FRAME_PREPARE_ONLY');
 const f=readExplicitPositiveCurrentFrame(input),{g,difference}=validateGeneratedNativeFault(input);fresh(input.outDir);const owned=path.join(input.outDir,'owned-stage');fs.mkdirSync(owned);
 const copies=[],mapping=[];for(const source of f.plan.outputs){need(within(f.plan.ownedRoot,source.path),'CURRENT_FRAME_OUTPUT_IN_ROOT');const dest=path.join(owned,path.relative(f.plan.ownedRoot,source.path));fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source.path,dest,fs.constants.COPYFILE_EXCL);const after=filePin(dest);need(after.sha256===source.sha256,'CURRENT_FRAME_COPY37_EXACT');copies.push({...after,source});mapping.push({before:source,after});}
 const payload=path.join(owned,'native-fault-payload'),staging=stageFaultPayload(path.dirname(g.sourceHost.path),payload),adjacencies=[];
 for(const source of g.supportCopies){const dest=path.join(payload,path.relative(path.dirname(g.sourceHost.path),source.source)),after=filePin(dest);need(after.sha256===source.sha256,'CURRENT_FRAME_COPY13_EXACT');adjacencies.push({...after,source:{path:source.source,sha256:source.sha256}});mapping.push({before:{path:source.source,sha256:source.sha256},after});}
 const stagedFiles=[];function enumerate(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);need(!fs.lstatSync(p).isSymbolicLink(),'CURRENT_FRAME_NO_PAYLOAD_REPARSE');if(e.isDirectory())enumerate(p);else stagedFiles.push(p);}}enumerate(payload);need(stagedFiles.length===13,'CURRENT_FRAME_NO_EXTRA_ADJACENCIES');
 fs.copyFileSync(input.generatedPin.path,staging.mutatorOutput,fs.constants.COPYFILE_EXCL);const host=filePin(staging.mutatorOutput);need(host.sha256===input.generatedPin.sha256,'CURRENT_FRAME_FAULT_HOST_COPY');mapping.push({before:g.sourceHost,after:host});need(new Set(mapping.map(m=>path.resolve(m.before.path).toUpperCase())).size===mapping.length,'CURRENT_FRAME_UNIQUE_MIGRATION');
 const projected=projectCurrentFramePolicy({policy:f.policy,plan:f.plan,newRoot:owned,mapping}),args=projected.args,producer=filePin(mapping.find(m=>samePath(m.before.path,f.plan.producer.path)).after.path),launcher=filePin(mapping.find(m=>samePath(m.before.path,f.plan.launcher.path)).after.path);
 const argvPath=path.join(input.outDir,'native-fault-current-frame-argv.json');put(argvPath,{schemaVersion:1,kind:'Harness',producer:producer.path,args,argvSha256:digest(args),executed:false});
 const p=projected.policy,argvIndex=p.metadata.findIndex(m=>exactPin(m,f.plan.argumentMetadata));need(argvIndex>=0,'CURRENT_FRAME_ORIGINAL_ARGV_METADATA');const sourceArgMetadata=p.metadata[argvIndex];p.metadata[argvIndex]=filePin(argvPath);
 const helper=filePin(fileURLToPath(import.meta.url)),contract=filePin(fileURLToPath(new URL('./original-constructor-native-current-frame-contract.mjs',import.meta.url))),extras=[input.positivePlanPin,input.positiveResultPin,input.provenancePin,input.generationPin,input.proofPin,input.generatedPin,input.rawRequestPin,input.rawReportPin,input.generationReceiptPin,helper,contract];for(const e of extras){const old=p.metadata.find(x=>samePath(x.path,e.path));need(!old||old.sha256===e.sha256,'CURRENT_FRAME_METADATA_CONFLICT');if(!old)p.metadata.push(e);}all([...p.metadata,...p.contexts.flatMap(c=>c.managed)]);
 const policyPath=path.join(input.outDir,'native-fault-current-frame-policy.json');put(policyPath,p);const launchReceipt=path.join(owned,'native-fault-current-frame-launch.json'),metadataValidateReceipt=path.join(owned,'native-fault-current-frame-metadata.json');
 const plan={schemaVersion:1,kind:'original89-native-fault-validated-current-frame',status:'PREPARED_NOT_EXECUTED',sourceProfile:input.profile,authoritySchema:p.schemaVersion,ownedRoot:owned,sourcePositivePlan:input.positivePlanPin,sourcePositiveResult:input.positiveResultPin,sourceProvenance:input.provenancePin,producer,launcher,dotnet:f.plan.dotnet,copies,adjacencies,host,hc:g.hc,args,argsSha256:digest(args),policy:filePin(policyPath),argumentMetadata:filePin(argvPath),businessResult:args[6],settingsRoot:path.join(path.dirname(args[6]),'constructor-owned-settings'),launchReceipt,metadataValidateReceipt,budgetMs:60000,parentBudgetMs:360000,expectedExitCode:2,contract:nativeConstructorFaultContract(),difference,migration:{...projected,policy:undefined,sourcePolicy:f.plan.policy,sourceArgMetadata,appendedMetadata:extras},referenceInputs:[input.positivePlanPin,input.positiveResultPin,input.provenancePin],actualNativeFaultMask:null,producerExecuted:false,targetExecuted:false,executionApprovalGranted:false};
 plan.dotnetArguments=[launcher.path,'run',policyPath,plan.policy.sha256,launchReceipt,...args];plan.metadataValidateArguments=[launcher.path,'validate',policyPath,plan.policy.sha256,metadataValidateReceipt];put(path.join(input.outDir,'NATIVE-FAULT-CURRENT-FRAME-PLAN.json'),plan);return plan;
}
