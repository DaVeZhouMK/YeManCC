import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

export const OLD_WRITE_GATE=Object.freeze({mode:'old-write',cases:6,goldenWrites:9,nativeAttempts:0,budgetMs:45000,status:'DEFECT_REPRODUCED_IN_MEMORY'});
export const OLD_WRITE_IDENTITIES=Object.freeze({hostSha256:'a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539',syntheticHcSha256:'6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6',rawHcSha256:'08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'});
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const same=(a,b)=>key(a)===key(b);
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const fail=(code,detail)=>{throw Object.assign(new Error(code),{code,detail,oldWritePrepareBlocked:true});};
const need=(ok,code,detail)=>{if(!ok)fail(code,detail);};
export function checkedOldWritePin(pin){need(pin&&path.isAbsolute(pin.path??'')&&/^[a-f0-9]{64}$/.test(pin.sha256??''),'OLD_WRITE_PIN_SHAPE',pin);need(fs.existsSync(pin.path)&&fs.statSync(pin.path).isFile(),'OLD_WRITE_PIN_MISSING',pin);need(same(fs.realpathSync(pin.path),pin.path)&&!fs.lstatSync(pin.path).isSymbolicLink(),'OLD_WRITE_PIN_REDIRECTED',pin);need(sha(pin.path)===pin.sha256,'OLD_WRITE_PIN_CHANGED',pin);return pin;}
// Independent prepare-only module. No child_process or managed target loader is
// imported. The remaining functions derive exact policy/argv and audit a finite
// selected MethodDef/reflection boundary from pinned existing raw PE reports.
import {fileURLToPath} from 'node:url';
import {auditOldWriteMetadata} from './old-write-metadata-audit.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const put=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const pin=p=>({path:p,sha256:sha(p)});
const digest=v=>createHash('sha256').update(JSON.stringify(v)).digest('hex');
export function validateOldWritePreparationInputs(input,base,policy,inventory,fixture){
  need(input.schemaVersion===1&&input.kind==='onex-old-write-prepare','OLD_WRITE_INPUT_SCHEMA');
  need(input.mode==='prepare-only','OLD_WRITE_ONLY_EXPLICIT_PREPARE_ALLOWED');
  need(JSON.stringify(input.gate)===JSON.stringify(OLD_WRITE_GATE),'OLD_WRITE_ORIGINAL_GATE_CHANGED');
  need(base.originalBusinessBudgetMs===45000&&base.status==='ARGUMENT_BOUND_PREPARED_NOT_EXECUTED','OLD_WRITE_PREPARED_BASE_REQUIRED');
  need(base.outputs.length===37&&inventory.outputs.length===37,'OLD_WRITE_EXACT37_BUILD_OUTPUTS_REQUIRED');
  const seen=new Set();
  for(const row of base.outputs){need(!seen.has(key(row.path)),'OLD_WRITE_DUPLICATE_BUILD_PIN');seen.add(key(row.path));need(inventory.outputs.some(p=>same(p.path,row.original)&&p.sha256===row.sha256),'OLD_WRITE_ORIGINAL_BUILD_PIN_MISMATCH',row);}
  need(base.launcher.sha256==='55bbc787f00ccc7b3c3855023f2f96df6423baf0481883ec40a732f7b5583a0e'&&base.producer.sha256==='fe96c6a564e74a1bafcfe4161f1f23f4d84ecaab26b1e510fe332217c6dd4846','OLD_WRITE_EXACT_REVIEWED_COMPILED_ENTRY_REQUIRED');
  need(same(policy.entry,base.producer.path)&&same(policy.ownedRoot,base.ownedRoot),'OLD_WRITE_BASE_POLICY_LOCATION_MISMATCH');
  need(policy.contexts.length===2&&policy.contexts[0].id==='Default'&&policy.contexts[0].kind==='default'&&policy.contexts[0].managed.length===44,'OLD_WRITE_BASE_CONTEXT_CONTRACT');
  need(fixture.id==='host-a678-write-failure'&&fixture.files.length===2,'OLD_WRITE_FIXED_HOST_PROVENANCE_REQUIRED');
  need(fixture.files.some(p=>p.path==='YeManFanHost.dll'&&p.sha256===OLD_WRITE_IDENTITIES.hostSha256)&&fixture.files.some(p=>p.path==='YeManFanBackend.dll'&&p.sha256==='6d2bcbd48498b7826db64d77b6d5d9d3f45bb91917d816b97c7a830a1eaa84db'),'OLD_WRITE_FIXED_HOST_BYTES_REQUIRED');
  const hc=policy.contexts[0].managed.filter(p=>p.identity.startsWith('HandheldCompanion,'));
  need(hc.length===1&&hc[0].sha256===OLD_WRITE_IDENTITIES.syntheticHcSha256,'OLD_WRITE_SYNTHETIC_HC_REQUIRED');
  need(policy.metadata.some(p=>same(p.path,base.firstOldArgs[3])&&p.sha256===OLD_WRITE_IDENTITIES.rawHcSha256),'OLD_WRITE_RAW_HC_METADATA_ONLY_PIN');
  need(Array.isArray(input.rawReports)&&input.rawReports.length<=10&&input.rawReports.length>=2,'OLD_WRITE_RAW_REPORT_PINS_REQUIRED');
}
function relocatePolicy(basePolicy,base,ownedRoot,outputs,argumentMetadataPin,customImages){
  const outputMap=new Map(outputs.map(p=>[key(p.source),p.path]));
  const relocate=p=>outputMap.get(key(p))??p;
  const q=structuredClone(basePolicy);q.ownedRoot=ownedRoot;q.entry=relocate(q.entry);
  const def=q.contexts[0];
  for(const p of def.managed)p.path=relocate(p.path);
  for(const request of def.requests)request.targetPath=relocate(request.targetPath);
  const contextId='OldWriteProbe.Context|'+path.dirname(customImages[0].path).toUpperCase();
  const old=q.contexts[1],targetRequests=customImages.map(p=>({requestedIdentity:p.identity,targetContext:contextId,targetPath:p.path}));
  const defaultRequests=old.requests.filter(r=>r.targetContext==='Default').map(r=>({...r,targetPath:relocate(r.targetPath)}));
  q.contexts[1]={id:contextId,kind:'custom',registrationType:'OldWriteProbe+Context',registrationAssembly:{...def.managed.find(p=>same(p.path,q.entry))},managed:customImages.map(p=>({...p,role:'target'})),requests:[...targetRequests,...defaultRequests]};
  q.metadata=q.metadata.map(p=>same(p.path,base.argumentMetadata.path)?argumentMetadataPin:{...p,path:relocate(p.path)});
  return q;
}
export function validateOldWriteDerivedPlan(plan,policy,argvMetadata){
  need(plan.status==='PREPARED_NOT_EXECUTED'&&plan.producerExecuted===false&&plan.businessPassed===false,'OLD_WRITE_PREPARE_CANNOT_CLAIM_PASS');
  need(JSON.stringify(plan.gate)===JSON.stringify(OLD_WRITE_GATE),'OLD_WRITE_ORIGINAL_GATE_CHANGED');
  need(plan.args.length===4&&plan.args[0]==='old-write','OLD_WRITE_EXACT_MODE_AND_ARGC');
  need(plan.host.sha256===OLD_WRITE_IDENTITIES.hostSha256&&plan.rawHc.sha256===OLD_WRITE_IDENTITIES.rawHcSha256,'OLD_WRITE_ARGUMENT_PIN_IDENTITY');
  need(same(plan.args[1],plan.host.path)&&same(plan.args[2],plan.rawHc.path)&&same(plan.args[3],plan.businessResult),'OLD_WRITE_EXACT_TARGET_ARGUMENTS');
  const expected=[plan.launcher.path,'run',plan.policy.path,plan.policy.sha256,plan.launchReceipt,...plan.args];
  need(JSON.stringify(plan.dotnetArguments)===JSON.stringify(expected),'OLD_WRITE_EXACT_LAUNCH_ARGV');
  need(JSON.stringify(plan.metadataValidateArguments)==JSON.stringify([plan.launcher.path,'validate',plan.policy.path,plan.policy.sha256,plan.metadataValidateReceipt]),'OLD_WRITE_EXACT_VALIDATE_ARGV');
  need(argvMetadata.kind==='Harness'&&JSON.stringify(argvMetadata.args)===JSON.stringify(plan.args)&&argvMetadata.argvSha256===digest(plan.args),'OLD_WRITE_ARGUMENT_METADATA_MISMATCH');
  need(same(policy.entry,plan.producer.path),'OLD_WRITE_DERIVED_ENTRY');
  const context=policy.contexts[1],expectedId='OldWriteProbe.Context|'+path.dirname(plan.host.path).toUpperCase();
  need(context.id===expectedId&&context.registrationType==='OldWriteProbe+Context'&&context.kind==='custom','OLD_WRITE_CONTEXT_OWNERSHIP_CHANGED');
  need(context.managed.length===2&&context.managed.some(p=>same(p.path,plan.host.path)&&p.sha256===plan.host.sha256),'OLD_WRITE_CUSTOM_PIN_SET');
  const hc=policy.contexts.flatMap(c=>c.managed).filter(p=>p.identity.startsWith('HandheldCompanion,'));
  need(hc.length===1&&hc[0].sha256===OLD_WRITE_IDENTITIES.syntheticHcSha256,'OLD_WRITE_SYNTHETIC_HC_REQUIRED');
  need(policy.contexts.flatMap(c=>c.managed).every(p=>p.sha256!==OLD_WRITE_IDENTITIES.rawHcSha256),'OLD_WRITE_RAW_HC_RUNTIME_FORBIDDEN');
  for(const c of policy.contexts)for(const r of c.requests){const target=policy.contexts.find(t=>t.id===r.targetContext);need(target&&target.managed.some(p=>same(p.path,r.targetPath)&&p.identity===r.requestedIdentity),'OLD_WRITE_UNPINNED_REQUEST',r);}
  for(const p of [plan.businessResult,plan.launchReceipt,plan.metadataValidateReceipt])need(inside(plan.ownedRoot,p)&&!policy.metadata.some(row=>same(row.path,p)),'OLD_WRITE_FRESH_OUTPUT_SCOPE');
  return true;
}
export function prepareOldWriteStage({mode,inputsPin,outDir}){
  need(mode==='prepare-only','OLD_WRITE_ONLY_EXPLICIT_PREPARE_ALLOWED');
  const checked=new Map(),check=p=>{checkedOldWritePin(p);const old=checked.get(key(p.path));need(!old||old.sha256===p.sha256,'OLD_WRITE_CONFLICTING_PIN',p);checked.set(key(p.path),{path:p.path,sha256:p.sha256});return p;},json=p=>read(check(p).path);
  const input=json(inputsPin),base=json(input.baseInvocation),basePolicy=json(base.policy),inventory=json(base.originalInventory),fixture=json(input.hostProvenance),framework=json(input.frameworkMetadata);
  validateOldWritePreparationInputs(input,base,basePolicy,inventory,fixture);
  need(framework.status==='PASS'&&framework.targetRuntimeLoaded===false,'OLD_WRITE_EXISTING_FRAMEWORK_METADATA_REQUIRED');
  for(const p of base.outputs)check(p);
  for(const p of [...basePolicy.metadata,...basePolicy.contexts.flatMap(c=>c.managed),input.dotnet,input.frameworkMetadata,...input.sourcePins])check(p);
  need(input.dotnet.sha256==='ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135','OLD_WRITE_EXACT_DOTNET_REQUIRED');
  for(const name of ['OldWriteProbe.cs','OldWriteLoadContexts.cs','Harness.cs']){
    const current=input.sourcePins.find(p=>path.basename(p.path)===name),captured=inventory.sourceCopies.find(p=>path.basename(p.path)===name);
    need(current&&captured&&current.sha256===captured.copySha256&&captured.sha256===captured.copySha256,'OLD_WRITE_COMPILED_SOURCE_MISMATCH',{name});check({path:captured.copy,sha256:captured.copySha256});
  }
  const reports=input.rawReports.map(json),analysis=auditOldWriteMetadata(reports);
  const imageBySha=new Map(analysis.images.map(i=>[i.sha256,i]));
  const hostRoot=path.dirname(input.hostProvenance.path),hostImages=fixture.files.map(p=>{const image=imageBySha.get(p.sha256);need(image&&same(image.path,path.join(hostRoot,p.path)),'OLD_WRITE_EXACT_HOST_IMAGE_METADATA_MISSING',p);return {path:path.join(hostRoot,p.path),sha256:p.sha256,identity:image.identity};});
  for(const p of hostImages)check(p);
  const provenancePin=basePolicy.metadata.find(p=>path.basename(p.path)==='source-input-provenance.json');need(provenancePin,'OLD_WRITE_SOURCE_PROVENANCE_REQUIRED');
  const provenance=json(provenancePin),fixed=json(provenance.fixedRunBindings),protectedRoots=[fixed.sourceRoot,fixed.snapshotRoot];
  need(path.isAbsolute(outDir??''),'OLD_WRITE_ABSOLUTE_NEW_OUTPUT_REQUIRED');const out=path.resolve(outDir);
  need(protectedRoots.every(root=>path.isAbsolute(root)&&!inside(root,out)&&!inside(out,root)),'OLD_WRITE_OUTPUT_OVERLAPS_PROTECTED_SOURCE');
  need(!fs.existsSync(out),'OLD_WRITE_FRESH_OUTPUT_REQUIRED',{path:out});need(same(fs.realpathSync(path.dirname(out)),path.dirname(out)),'OLD_WRITE_REDIRECTED_OUTPUT_PARENT');
  need([...checked.values()].every(p=>!inside(out,p.path)),'OLD_WRITE_OUTPUT_CONTAINS_INPUT');
  fs.mkdirSync(out);const owned=path.join(out,'owned-stage');fs.mkdirSync(owned);
  const outputs=base.outputs.map(row=>{
    const relative=path.relative(base.ownedRoot,row.path);need(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative),'OLD_WRITE_BUILD_OUTPUT_ESCAPE',row);
    const target=path.join(owned,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.path,target,fs.constants.COPYFILE_EXCL);need(sha(target)===row.sha256,'OLD_WRITE_COPIED_OUTPUT_CHANGED');
    return {source:row.path,original:row.original,path:target,sha256:row.sha256};
  });
  const mapped=p=>{const row=outputs.find(r=>same(r.source,p.path));need(row&&row.sha256===p.sha256,'OLD_WRITE_MISSING_EXACT_BUILD_OUTPUT',p);return {path:row.path,sha256:row.sha256};};
  const producer=mapped(base.producer),launcher=mapped(base.launcher),host=hostImages.find(p=>p.sha256===OLD_WRITE_IDENTITIES.hostSha256),rawHc=basePolicy.metadata.find(p=>p.sha256===OLD_WRITE_IDENTITIES.rawHcSha256);
  const args=['old-write',host.path,rawHc.path,path.join(owned,'old-write-result.json')],argPath=path.join(out,'old-write-argv.json'),argumentMetadata={schemaVersion:1,kind:'Harness',producer:producer.path,args,argvSha256:digest(args),executed:false,expectedBusinessExit:0,output:args[3]};put(argPath,argumentMetadata);
  const policy=relocatePolicy(basePolicy,base,owned,outputs,pin(argPath),hostImages),policyPath=path.join(out,'old-write-policy.candidate.json');
  // Preserve exact original metadata pins and add finite preparation provenance.
  policy.metadata.push(inputsPin,input.hostProvenance,input.frameworkMetadata,...input.sourcePins,...input.rawReports);
  const unique=new Map();for(const p of policy.metadata){const previous=unique.get(key(p.path));need(!previous||previous.sha256===p.sha256,'OLD_WRITE_METADATA_PIN_CONFLICT');unique.set(key(p.path),p);}policy.metadata=[...unique.values()];
  put(policyPath,policy);
  const plan={schemaVersion:1,kind:'onex-old-write-prepared-invocation',status:'PREPARED_NOT_EXECUTED',gate:OLD_WRITE_GATE,ownedRoot:owned,producer,launcher,host,rawHc,
    policy:pin(policyPath),argumentMetadata:pin(argPath),args,argsSha256:digest(args),launchReceipt:path.join(owned,'old-write-launch-receipt.json'),businessResult:args[3],metadataValidateReceipt:path.join(owned,'old-write-metadata-validate.json'),
    dotnet:input.dotnet,outputs,baseInvocation:input.baseInvocation,originalInventory:base.originalInventory,hostProvenance:input.hostProvenance,frameworkMetadata:input.frameworkMetadata,
    producerExecuted:false,businessPassed:false,sourceApprovalGranted:false,executionApprovalGranted:false,fullHostApproved:false};
  plan.dotnetArguments=[launcher.path,'run',policyPath,plan.policy.sha256,plan.launchReceipt,...args];
  plan.metadataValidateArguments=[launcher.path,'validate',policyPath,plan.policy.sha256,plan.metadataValidateReceipt];
  validateOldWriteDerivedPlan(plan,policy,argumentMetadata);
  for(const p of [...policy.metadata,...policy.contexts.flatMap(c=>c.managed),...outputs])checkedOldWritePin(p);
  for(const p of checked.values())checkedOldWritePin(p);
  for(const p of [plan.launchReceipt,plan.businessResult,plan.metadataValidateReceipt])need(!fs.existsSync(p),'OLD_WRITE_PREPARATION_WROTE_TARGET_OUTPUT');
  const ownedIdentities=outputs.flatMap(row=>inventory.identities.filter(i=>same(i.path,row.original)&&i.sha256===row.sha256).map(i=>({...i,path:row.path,originalPath:i.path,identityBasis:'Exact-byte copy of pinned original compiler metadata'})));
  put(path.join(out,'owned-output-manifest.json'),{schemaVersion:1,status:'EXACT_COPIES_METADATA_NOT_RUNTIME_LOAD',outputs,identities:ownedIdentities,originalInventory:base.originalInventory,sharedAuthoritySha256:inventory.sharedAuthoritySha256,compiledAgain:false,targetRuntimeLoaded:false});
  need(new Set([...checked.values(),...outputs].map(p=>key(p.path))).size<=3072,'OLD_WRITE_ARTIFACT_BUDGET_EXCEEDED');
  put(path.join(out,'OLD-WRITE-METADATA-ANALYSIS.json'),analysis);
  put(path.join(out,'OLD-WRITE-PREPARED-PLAN.json'),plan);
  put(path.join(out,'INPUTS-VERIFIED.json'),{schemaVersion:1,status:'EXACT_INPUTS_AND_COPIES_VERIFIED',pins:[...checked.values()],targetRuntimeLoaded:false,businessExecuted:false});
  const summary={schemaVersion:1,status:'PREPARED_NOT_EXECUTED',passed:false,plan:pin(path.join(out,'OLD-WRITE-PREPARED-PLAN.json')),policy:plan.policy,argumentMetadata:plan.argumentMetadata,
    outputs:outputs.length,defaultPins:policy.contexts[0].managed.length,customPins:policy.contexts[1].managed.length,sourceMapping:'Current three entry sources equal the immutable compiler source-view copies',
    ownedOutputManifest:pin(path.join(out,'owned-output-manifest.json')),metadataAnalysis:pin(path.join(out,'OLD-WRITE-METADATA-ANALYSIS.json')),uniqueMethods:analysis.uniqueMethods,directMethodDefFrontier:analysis.frontierCount,missingReflectionBindings:analysis.missingReflectionBindings,
    producerExecuted:false,targetRuntimeLoaded:false,metadataValidateExecuted:false,unresolvedTerminalReview:true,businessExecutionApproved:false,fullHostApproved:false,wholeGoalComplete:false};
  put(path.join(out,'SUMMARY.json'),summary);return summary;
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){
  try{const args=process.argv.slice(2);need(args.length===8&&JSON.stringify(args.filter((_,i)=>i%2===0))===JSON.stringify(['--mode','--inputs','--sha256','--out-dir']),'OLD_WRITE_EXACT_CLI_ARGUMENTS');const result=prepareOldWriteStage({mode:args[1],inputsPin:{path:args[3],sha256:args[5]},outDir:args[7]});console.log(JSON.stringify(result));}
  catch(e){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',code:e.code,error:e.message,detail:e.detail,producerExecuted:false}));process.exitCode=3;}
}
