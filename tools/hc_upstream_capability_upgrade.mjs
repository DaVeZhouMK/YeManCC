// Offline upgrade planning for the existing YMCC total library. No HC load,
// DLL replacement, hardware I/O or runtime catalog installation.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {buildPinnedUpstreamMethodConsumption} from './lib/hc_model_method_consumption.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const opt={};
for(let i=2;i<process.argv.length;i+=2) {
 const key=process.argv[i],value=process.argv[i+1];
 if(!['--source-root','--source-pin','--inventory','--out'].includes(key)||!value||opt[key])
  throw Error('Usage: --source-root <pinned HC source checkout> --source-pin <SOURCE-PIN.json> --inventory <PcCapabilityAudit.json> --out <outside repository>');
 opt[key]=path.resolve(value);
}
for(const key of ['--source-root','--source-pin','--inventory','--out'])if(!opt[key])throw Error('Missing '+key);
const sha=b=>createHash('sha256').update(b).digest('hex');
const need=(ok,reason)=>{if(!ok)throw Error(reason);};
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tracked=new Map();
function input(file){const bytes=fs.readFileSync(file),hash=sha(bytes);need(!tracked.has(file)||tracked.get(file)===hash,'SOURCE_CHANGED_DURING_UPGRADE_REVIEW: '+file);tracked.set(file,hash);return bytes;}
function relativeFile(root,name){
 need(typeof name==='string'&&name&&!name.includes('\\')&&!name.includes(':')&&
  name.split('/').every(p=>p&&p!=='.'&&p!=='..'),'SOURCE_PATH_INVALID');
 const target=path.resolve(root,...name.split('/'));need(target.startsWith(path.resolve(root)+path.sep),'SOURCE_PATH_ESCAPE');
 need(!fs.lstatSync(target).isSymbolicLink()&&fs.statSync(target).isFile(),'SOURCE_FILE_NOT_REGULAR');return target;
}
const out=opt['--out'],relative=path.relative(repo,out);
need(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative),'EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY');
need(!fs.existsSync(out)||fs.readdirSync(out).length===0,'EVIDENCE_MUST_BE_EMPTY');
const pin=JSON.parse(input(opt['--source-pin'])),inventory=JSON.parse(input(opt['--inventory']));
need(pin.repository==='Valkirie/HandheldCompanion'&&/^[a-f0-9]{40}$/.test(pin.commit)&&pin.pathPrefix==='HandheldCompanion/Devices/'&&pin.allGitBlobsMatched===true,'UPSTREAM_PIN_SCHEMA');
need(Array.isArray(pin.deviceSourceFiles)&&pin.deviceSourceFiles.length>0,'UPSTREAM_SOURCE_SET_EMPTY');
const seen=new Set(),byFile=new Map();
for(const row of pin.deviceSourceFiles){
 need(row.path.startsWith(pin.pathPrefix)&&row.path.endsWith('.cs')&&!seen.has(row.path),'UPSTREAM_SOURCE_DUPLICATE_OR_SCOPE');seen.add(row.path);
 const bytes=input(relativeFile(opt['--source-root'],row.path));
 need(sha(bytes)===row.sha256&&bytes.length===row.bytes,'UPSTREAM_SOURCE_SHA256_MISMATCH: '+row.path);
 const blob=createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
 need(blob===row.gitBlobSha,'UPSTREAM_GIT_BLOB_MISMATCH: '+row.path);byFile.set(row.path.slice(pin.pathPrefix.length),row);
}
need(inventory.schemaVersion===1&&inventory.issues?.length===0&&inventory.runtime==null,'SOURCE_ONLY_INVENTORY_REQUIRED');
need(inventory.files?.length===byFile.size&&inventory.devices?.length>0,'INVENTORY_SOURCE_SET_MISMATCH');
const inventoryFiles=new Set();
for(const row of inventory.files){need(!inventoryFiles.has(row.file)&&byFile.get(row.file)?.sha256===row.sha256,'INVENTORY_SOURCE_IDENTITY_MISMATCH: '+row.file);inventoryFiles.add(row.file);}
need(inventoryFiles.size===byFile.size,'INVENTORY_SOURCE_SET_MISMATCH');
const load=name=>JSON.parse(input(path.join(repo,name)));
const baseline=load('deps/handheldcompanion-runtime/HC-BASELINE.json');
const index=input(path.join(repo,'deps/handheldcompanion-runtime',baseline.sourceFilesIndex));
need(sha(index)===baseline.sourceFilesIndexSha256.toLowerCase(),'FROZEN_SOURCE_INDEX_CHANGED');
const before=load('capabilities/hc-device-catalog.json'),motionPolicy=load('capabilities/hc-motion-policy.json');
const scope=load('capabilities/ACTIVE-INTEGRATION-SCOPE.json'),excluded=load('capabilities/USER-SCOPE-DECISIONS.json');
const original=new Map(before.devices.map(d=>[d.name,d]));
const reviewed=new Set(motionPolicy.reviewedExtensionClasses.map(name=>name.split('.').at(-1)));
const methods=d=>new Map((d?.methods??[]).map(m=>[m.signature,m.sourceSha256]));
const declarations=d=>JSON.stringify(d?.declared??{});
const ids=new Set(),rows=[],candidateDevices=[];
for(const model of inventory.devices){
 need(!ids.has(model.name)&&typeof model.fullName==='string'&&model.fullName.startsWith('HandheldCompanion.Devices.')&&model.fullName.split('.').at(-1)===model.name&&byFile.has(model.file),'UPSTREAM_DEVICE_CLASS_INVALID');
 ids.add(model.name);const old=original.get(model.name),previousMethods=methods(old),nextMethods=methods(model);
 const methodChanges=[];
 for(const key of new Set([...previousMethods.keys(),...nextMethods.keys()]))
  if(previousMethods.get(key)!==nextMethods.get(key))methodChanges.push({signature:key,change:!previousMethods.has(key)?'added':!nextMethods.has(key)?'removed':'body-changed'});
 rows.push({deviceClass:'HandheldCompanion.Devices.'+model.name,family:model.family,source:pin.pathPrefix+model.file,sourceSha256:byFile.get(model.file)?.sha256,
  membership:old?'frozen-total-library-row':reviewed.has(model.name)?'existing-reviewed-extension':'not-yet-reviewed',
  upstreamFactoryInstantiable:model.instantiable,declaredCapabilityChanges:!old||declarations(old)!==declarations(model),methodChanges,
  consumerMigrationStatus:'REVIEW_EXISTING_CONSUMER_AND_CURRENT_RECEIPT',hardwareAccepted:false});
 candidateDevices.push({...old,deviceId:'hc.device.'+model.name,name:model.name,family:model.family,sourceType:model.fullName,
  consumerClass:'HandheldCompanion.Devices.'+model.name,runtimeType:old?.runtimeType??null,factoryInstantiable:model.instantiable,
  aliases:[...new Set([...(old?.aliases??[]),model.fullName,'HandheldCompanion.Devices.'+model.name,'hc.device.'+model.name])].sort(),
  sourcePath:pin.pathPrefix+model.file,sourceSha256:byFile.get(model.file)?.sha256,declarationSha256:model.sourceSha256,
  inheritance:model.chain,declared:model.declared,operations:model.operations,parameters:model.properties,propertyMembers:model.propertyMembers??[],methods:model.methods,oemChords:model.oemChords,
  evidenceLevel:'UNVERIFIED_UPSTREAM_SOURCE_CANDIDATE_NOT_RUNTIME_ADMISSION'});
}
const removed=before.devices.filter(d=>!ids.has(d.name)).map(d=>d.sourceType);
const candidate={...before,sourceBaseline:'HC-UPSTREAM-SOURCE-'+pin.commit,sourceProjectVersion:'source-commit-'+pin.commit.slice(0,8),
 sourceFilesIndexSha256:sha(input(opt['--source-pin'])),sourceFiles:inventory.files,devices:candidateDevices,
 scope:{...before.scope,sourceDeviceClasses:inventory.devices.length,factoryInstantiableClasses:inventory.devices.filter(d=>d.instantiable).length},
 upgradeCandidate:{verified:false,runtimeAdmission:false,upstreamCommit:pin.commit,frozenRuntimeUnchanged:true}};
const methodPolicy=load('capabilities/hc-model-method-policy.json');
const methodReview=buildPinnedUpstreamMethodConsumption({catalog:candidate,inventory,policy:methodPolicy,pin,sourcePinSha256:sha(input(opt['--source-pin']))});
for(const group of [...methodPolicy.groups,...methodPolicy.upstreamGroups])for(const ref of group.consumerReferences){
 const target=path.resolve(repo,ref.file),relative=path.relative(repo,target);
 need(relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'UPSTREAM_METHOD_REFERENCE_PATH');
 need(input(target).toString('utf8').includes(ref.anchor),'UPSTREAM_METHOD_REFERENCE_ANCHOR: '+ref.file+': '+ref.anchor);
}
input(fileURLToPath(import.meta.url));input(path.join(repo,'tools/lib/hc_model_method_consumption.mjs'));
const report={schemaVersion:1,purpose:'Source upgrade review for the single YMCC capability library',upstreamCommit:pin.commit,latestReleaseAtCapture:pin.latestRelease,
 sourceDeviceFiles:byFile.size,sourceDeviceClasses:rows.length,sourceFactoryClasses:inventory.devices.filter(d=>d.instantiable).length,
 existingFrozenClasses:original.size,existingReviewedExtensions:rows.filter(d=>d.membership==='existing-reviewed-extension').length,
 notYetReviewedClasses:rows.filter(d=>d.membership==='not-yet-reviewed'),removedFrozenClasses:removed,devices:rows,
 scopeAuthority:scope.authority,userExclusions:excluded,sourceInputs:[...tracked].map(([file,sha256])=>({file,sha256})),
 runtimeCatalog:'capabilities/hc-device-catalog.json and its existing generated domain bindings',
 methodReview:{file:'method-consumption-review.json',sha256:sha(Buffer.from(JSON.stringify(methodReview,null,2)+'\n')),classes:methodReview.classCount,methods:methodReview.declaredMethodCount,methodSpecificExecution:'not-inferred-from-domain-link',runtimeAvailabilityAuthority:false},
 candidateIsOfflineReviewOnly:true,soleRuntimeEntryUnchanged:true,newDlls:[],hcAssemblyLoaded:false,hardwareIo:false,goalComplete:false};
for(const [file,hash]of tracked)need(sha(fs.readFileSync(file))===hash,'SOURCE_CHANGED_DURING_UPGRADE_REVIEW');
fs.mkdirSync(out,{recursive:true});
for(const [name,value]of [['candidate-catalog.json',candidate],['method-consumption-review.json',methodReview],['result.json',report]])fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
console.log(JSON.stringify({status:'SOURCE_REVIEW_PREPARED',sourceDeviceFiles:byFile.size,sourceDeviceClasses:rows.length,reviewedExtensions:report.existingReviewedExtensions,newUnreviewed:report.notYetReviewedClasses.length,candidateIsOfflineReviewOnly:true,hcAssemblyLoaded:false,hardwareIo:false}));
