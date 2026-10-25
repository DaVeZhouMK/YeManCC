// Offline method obligations. Never imported by a product runtime or used for admission.
import {createHash} from 'node:crypto';
const digest=value=>createHash('sha256').update(value).digest('hex');
const domains=new Set(['fan','lighting','battery','motion','controller','oem-input','performance','lifecycle-and-dependencies','remaining-model-methods']);
const kinds=new Set(['domain-action','required-internal-dependency','metadata-only-by-user','required-lifecycle-dependency','mixed-settings-dependency-review','scope-review-required','conditional-installation-dependency-review','model-metadata-reference','unconnected-model-capability']);
const fail=code=>{throw Error(code);};
const need=(ok,code)=>{if(!ok)fail(code);};
const methodId=(device,method)=>device.deviceId+'::'+method.signature;
const proofFields=['name','line','signature','sourceSha256','isOverride','returnExpressions','constantBooleanReturn'];
const sameMethod=(a,b)=>proofFields.every(k=>JSON.stringify(a[k])===JSON.stringify(b[k]));
const validHash=value=>typeof value==='string'&&/^[a-f0-9]{64}$/i.test(value);
const forbiddenAuthority=value=>{
 if(!value||typeof value!=='object')return false;
 for(const [key,item]of Object.entries(value)){
  if(['runtimeAvailable','available','sourceConnected','allMethodsConsumed','goalComplete'].includes(key))return true;
  if(['runtimeAvailabilityAuthority','automaticExecution','physicalAcceptance'].includes(key)&&item!==false)return true;
  if(forbiddenAuthority(item))return true;
 }
 return false;
};
export function validateMethodPolicy(policy){
 need(policy?.schemaVersion===1&&policy.evidenceLevel==='source-classification-only','METHOD_POLICY_SCHEMA');
 need(policy.defaultForUnclassified==='reject'&&policy.runtimeAvailabilityAuthority===false&&policy.automaticExecution===false&&policy.physicalAcceptance===false,'METHOD_POLICY_AUTHORITY');
 need(!forbiddenAuthority(policy),'METHOD_POLICY_FORBIDDEN_AUTHORITY');
 need(Array.isArray(policy.groups)&&policy.groups.length>0,'METHOD_POLICY_GROUPS');
 const byName=new Map(),ids=new Set();
 for(const group of policy.groups){
  need(typeof group.id==='string'&&group.id&&!ids.has(group.id),'METHOD_POLICY_DUPLICATE_GROUP');ids.add(group.id);
  need(domains.has(group.domain)&&kinds.has(group.kind),'METHOD_POLICY_DISPOSITION:'+group.id);
  need(Array.isArray(group.affectedDomains)&&group.affectedDomains.includes(group.domain)&&group.affectedDomains.every(d=>domains.has(d)),'METHOD_POLICY_IMPACT:'+group.id);
  need(typeof group.note==='string'&&group.note.length>0&&Array.isArray(group.sideEffects),'METHOD_POLICY_REASON:'+group.id);
  need(Array.isArray(group.names)&&group.names.length>0,'METHOD_POLICY_NAMES:'+group.id);
  need(Array.isArray(group.consumerReferences)&&group.consumerReferences.length>0,'METHOD_POLICY_REFERENCES:'+group.id);
  for(const ref of group.consumerReferences)need(typeof ref.file==='string'&&ref.file&&typeof ref.anchor==='string'&&ref.anchor&&['open-obligation','consumer-contract','existing-domain-implementation'].includes(ref.role),'METHOD_POLICY_REFERENCE:'+group.id);
  for(const name of group.names){need(typeof name==='string'&&/^[A-Za-z_]\w*$/.test(name)&&!byName.has(name),'METHOD_POLICY_AMBIGUOUS_NAME:'+name);byName.set(name,group);}
 }
 return byName;
}
export function buildModelMethodConsumption({catalog,inventory,policy}){
 const byName=validateMethodPolicy(policy);
 need(catalog?.schemaVersion===1&&Array.isArray(catalog.devices)&&catalog.devices.length>0,'METHOD_CATALOG_SCHEMA');
 need(inventory&&Array.isArray(inventory.devices)&&Array.isArray(inventory.files)&&Array.isArray(inventory.issues)&&inventory.issues.length===0,'METHOD_ROSLYN_INVENTORY');
 const sourceClasses=new Map();for(const d of inventory.devices){need(!sourceClasses.has(d.name),'METHOD_ROSLYN_DUPLICATE_CLASS:'+d.name);sourceClasses.set(d.name,d);}
 const sourceFiles=new Map();for(const f of inventory.files){need(!sourceFiles.has(f.file)&&validHash(f.sha256),'METHOD_ROSLYN_DUPLICATE_FILE');sourceFiles.set(f.file,f.sha256);}
 need(Array.isArray(catalog.sourceFiles)&&catalog.sourceFiles.length===sourceFiles.size,'METHOD_SOURCE_FILE_COUNT');
 for(const f of catalog.sourceFiles)need(sourceFiles.get(f.file)===f.sha256,'METHOD_SOURCE_FILE_MISMATCH:'+f.file);
 need(sourceClasses.size===catalog.devices.length,'METHOD_CLASS_COUNT');
 const methods=[],classes=[],ids=new Set(),usedNames=new Set(),seenClasses=new Set(),groupCounts={},domainCounts={},kindCounts={};
 for(const device of catalog.devices){
  const original=sourceClasses.get(device.name);
  need(original&&!seenClasses.has(device.name),'METHOD_SOURCE_CLASS:'+device.name);seenClasses.add(device.name);
  need(original.sourceSha256===device.declarationSha256&&original.fullName===device.sourceType&&device.deviceId==='hc.device.'+device.name&&JSON.stringify(original.chain)===JSON.stringify(device.inheritance)&&original.file===device.sourcePath.replace('deps/handheldcompanion-runtime/source/Devices/','')&&original.instantiable===device.factoryInstantiable,'METHOD_CLASS_PROOF:'+device.name);
  need(sourceFiles.get(original.file)===device.sourceSha256&&Array.isArray(device.methods)&&Array.isArray(original.methods)&&device.methods.length===original.methods.length,'METHOD_DECLARATION_COUNT:'+device.name);
  const declared=[];
  for(let i=0;i<device.methods.length;i++){
   const method=device.methods[i],source=original.methods[i];
   need(sameMethod(method,source),'METHOD_ROSLYN_PROOF:'+device.name+':'+method.signature);
   need(typeof method.signature==='string'&&method.signature&&Number.isInteger(method.line)&&method.line>0&&validHash(method.sourceSha256),'METHOD_PROOF_SHAPE:'+device.name);
   const id=methodId(device,method),group=byName.get(method.name);
   need(!ids.has(id),'METHOD_DUPLICATE_ID:'+id);ids.add(id);
   need(group,'METHOD_CLASSIFICATION_MISSING:'+id);usedNames.add(method.name);
   declared.push(id);
   groupCounts[group.id]=(groupCounts[group.id]??0)+1;domainCounts[group.domain]=(domainCounts[group.domain]??0)+1;kindCounts[group.kind]=(kindCounts[group.kind]??0)+1;
   methods.push({id,deviceId:device.deviceId,deviceClass:device.consumerClass,factoryInstantiable:device.factoryInstantiable,source:{line:method.line,signature:method.signature,methodSha256:method.sourceSha256,isOverride:method.isOverride,constantBooleanReturn:method.constantBooleanReturn},group:group.id,domain:group.domain,kind:group.kind,methodSpecificExecution:'not-inferred-from-domain-link',runtimeAvailabilityAuthority:false,physicalAcceptance:false});
  }
  classes.push({deviceId:device.deviceId,deviceClass:device.consumerClass,factoryInstantiable:device.factoryInstantiable,sourceFile:device.sourcePath,sourceSha256:device.sourceSha256,inheritance:device.inheritance,declaredMethodIds:declared});
 }
 for(const name of byName.keys())need(usedNames.has(name),'METHOD_POLICY_STALE_NAME:'+name);
 // Ancestor declarations are traceability only: the syntax inventory does not establish
 // access modifiers, virtual dispatch, invocation or a consumer's live hardware admission.
 const classesByName=new Map(catalog.devices.map(d=>[d.name,d]));
 for(let i=0;i<classes.length;i++){
  const entry=classes[i],own=catalog.devices[i];entry.ancestorDeviceIds=[];
  for(const name of own.inheritance??[]){if(name===own.name)continue;const parent=classesByName.get(name);need(parent,'METHOD_ANCESTOR_MISSING:'+name);entry.ancestorDeviceIds.push(parent.deviceId);}
 }
 const registry={schemaVersion:1,scope:'Frozen Devices method declarations and their consumer/dependency review obligations',sourceBaseline:catalog.sourceBaseline,sourceFilesIndexSha256:catalog.sourceFilesIndexSha256,policySha256:digest(JSON.stringify(policy)),evidenceLevel:'fresh-Roslyn-source-classification',classCount:classes.length,factoryClassCount:classes.filter(d=>d.factoryInstantiable).length,sourceFileCount:sourceFiles.size,declaredMethodCount:methods.length,distinctMethodNames:usedNames.size,groupCounts,domainCounts,kindCounts,groups:policy.groups,classes,methods,coverageMeaning:'Every source declaration is classified; references to an existing consumer domain do not certify that this exact method has executed.',runtimeAvailabilityAuthority:false,automaticExecution:false,physicalAcceptance:false,wholeProcessCpuReduction:null,goalComplete:false};
 registry.contentSha256=digest(JSON.stringify(registry));return registry;
}
export function planModelMethodChanges({before,candidate,policy}){
 const byName=validateMethodPolicy(policy);need(Array.isArray(candidate?.devices),'METHOD_CANDIDATE_SCHEMA');
 const flatten=catalog=>{const result=new Map(),deviceIds=new Set();for(const device of catalog.devices){need(typeof device.deviceId==='string'&&Array.isArray(device.methods),'METHOD_CANDIDATE_DEVICE');need(!deviceIds.has(device.deviceId),'METHOD_CANDIDATE_DUPLICATE_DEVICE:'+device.deviceId);deviceIds.add(device.deviceId);for(const method of device.methods){need(typeof method.name==='string'&&typeof method.signature==='string'&&method.signature,'METHOD_CANDIDATE_METHOD_SHAPE');const id=methodId(device,method);need(!result.has(id),'METHOD_CANDIDATE_DUPLICATE:'+id);result.set(id,{device,method});}}return result;};
 const old=flatten(before),next=flatten(candidate),changes=[],affected=new Set();
 for(const id of new Set([...old.keys(),...next.keys()])){
  const a=old.get(id),b=next.get(id);if(a&&b&&sameMethod(a.method,b.method)&&a.device.sourceSha256===b.device.sourceSha256)continue;
  const row=b??a,group=byName.get(row.method.name);changes.push({id,change:!a?'added-method':!b?'removed-method':'changed-method-or-source',domain:group?.domain??null,reviewRequired:true,classificationKnown:!!group,candidateUnverified:true});if(group)for(const domain of group.affectedDomains)affected.add(domain);else affected.add('remaining-model-methods');
 }
 return{scope:'method declaration changes only; run the general capability maintenance planner for constructor flags, parameters, aliases, inheritance, runtime and payload changes',candidateUnverified:true,changes,affectedDomains:[...affected].sort(),unclassifiedMethods:changes.filter(c=>!c.classificationKnown).map(c=>c.id),automaticExecution:false,automaticBinaryReplacement:false,runtimeAvailabilityAuthority:false};
}

// Lightweight freshness check for the existing maintenance planner. Its proof is
// the current catalog snapshot; the standalone CLI supplies fresh Roslyn evidence.
export function checkStoredModelMethodRegistry({catalog,policy,registry}){
 try{
  const inventory={files:catalog.sourceFiles,issues:[],devices:catalog.devices.map(d=>({name:d.name,fullName:d.sourceType,file:d.sourcePath.replace('deps/handheldcompanion-runtime/source/Devices/',''),sourceSha256:d.declarationSha256,instantiable:d.factoryInstantiable,chain:d.inheritance,methods:d.methods}))};
  const expected=buildModelMethodConsumption({catalog,inventory,policy});
  return{errors:JSON.stringify(registry)===JSON.stringify(expected)?[]:['METHOD_GENERATED_REGISTRY_STALE'],methodCount:expected.declaredMethodCount,freshSourceReparse:false,runtimeAvailabilityAuthority:false};
 }catch(error){return{errors:[String(error.message)],freshSourceReparse:false,runtimeAvailabilityAuthority:false};}
}
