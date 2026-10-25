import fs from 'node:fs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
const ordered=values=>JSON.stringify([...values].sort());
const bools=['False','True'];
export const validationStaticIds=['validation-static-valid-split-and-nested-resource','validation-static-normalized-relative-binding',
 ...['runtime-id','missing-runtime-id','null-runtime-id','missing-host-id','empty-host-id','wrong-reference','missing-reference','null-reference','absolute-reference'].map(k=>'validation-static-reject-binding:'+k),
 ...['hash-mismatch','unsafe-member','required-file','duplicate','unexpected-file'].map(k=>'validation-static-'+k+'-remains-rejected'),
 'validation-static-required-backend',
 ...['owner','system','admins'].map(k=>'validation-acl-trusted:'+k),...['read','write','permissions'].map(k=>'validation-acl-untrusted:'+k),
 ...bools.map(k=>'validation-acl-null-dacl:'+k),'validation-acl-empty-dacl-is-distinct'];
export const validationCaseIds=[...validationStaticIds,...bools.map(b=>'validation-generation-valid:'+b),
 ...['runtime-id','wrong-reference','absolute-reference','missing-reference'].flatMap(k=>bools.map(b=>'validation-generation-reject-binding:'+k+':'+b)),
 ...['host','runtime'].flatMap(k=>bools.map(b=>'validation-generation-reject-schema:'+k+':'+b)),
 ...['generation','factory'].flatMap(k=>bools.map(b=>'validation-'+k+'-required-backend:'+b)),
 'validation-generation-cache-retains-no-per-dll-rehash',
 ...['runtime-id','wrong-reference'].flatMap(k=>bools.map(b=>'validation-factory-binding-before-assembly:'+k+':'+b)),
 ...['{','{"schemaVersion":9223372036854775807}'].map(text=>'validation-generation-damaged-record:'+text.length),
 ...bools.map(b=>'validation-native-missing-or-invalid:'+b)];
const failureSets={
 binding:[...['runtime-id','missing-runtime-id','null-runtime-id','wrong-reference','missing-reference','null-reference','absolute-reference'].map(k=>'validation-static-reject-binding:'+k),
 ...['runtime-id','wrong-reference','absolute-reference','missing-reference'].flatMap(k=>bools.map(b=>'validation-generation-reject-binding:'+k+':'+b)),
 ...['host','runtime'].map(k=>'validation-generation-reject-schema:'+k+':True'),
 ...['generation','factory'].map(k=>'validation-'+k+'-required-backend:True'),
 ...['runtime-id','wrong-reference'].flatMap(k=>bools.map(b=>'validation-factory-binding-before-assembly:'+k+':'+b))],
 'fast-path':[...['runtime-id','wrong-reference','absolute-reference','missing-reference'].map(k=>'validation-generation-reject-binding:'+k+':True'),
 ...['host','runtime'].map(k=>'validation-generation-reject-schema:'+k+':True'),...['generation','factory'].map(k=>'validation-'+k+'-required-backend:True'),...['runtime-id','wrong-reference'].map(k=>'validation-factory-binding-before-assembly:'+k+':True')],
 acl:[...['read','write','permissions'].map(k=>'validation-acl-untrusted:'+k),...bools.map(b=>'validation-acl-null-dacl:'+b)]};
function failureFragment(id){return id.startsWith('validation-factory')?'Actual rejection must come from the binding/validation boundary':id.startsWith('validation-acl-untrusted')?'Untrusted capability access is rejected':id.startsWith('validation-acl-null')?'A NULL/absent DACL cannot be accepted':'Actual deployment consumer must reject';}
export function prepareGenuineValidation({root,payload,hc}){
 const dir=path.join(root,'genuine');fs.mkdirSync(dir);const power=path.join(dir,'PowerControl'),host=path.join(power,'fan-host');fs.cpSync(payload,host,{recursive:true});
 const runtimeSource=path.dirname(hc),runtimeManifest=path.join(runtimeSource,'HandheldCompanion.runtime.json'),runtimeDocument=read(runtimeManifest);
 const runtime=path.join(power,'handheldcompanion-runtime',path.basename(runtimeSource));fs.mkdirSync(runtime,{recursive:true});
 const sourcePins=[];
 function copyFile(source,target){const pin=hash(source);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target);if(hash(source)!==pin||hash(target)!==pin)throw Error('Genuine validation input changed during copy');sourcePins.push({path:source,sha256:pin});}
 function listed(root,relative){if(typeof relative!=='string'||path.isAbsolute(relative)||relative.split(/[\\/]/).some(segment=>segment==='..'))throw Error('Unsafe genuine runtime member');const file=path.resolve(root,relative);if(!file.toLowerCase().startsWith((path.resolve(root)+path.sep).toLowerCase())||fs.lstatSync(file).isSymbolicLink()||!fs.lstatSync(file).isFile())throw Error('Genuine runtime member must be a regular bound file');return file;}
 for(const entry of runtimeDocument.files){const source=listed(runtimeSource,entry.path);if(hash(source)!==entry.sha256.toLowerCase())throw Error('Declared genuine runtime hash mismatch: '+entry.path);copyFile(source,path.join(runtime,entry.path));}
 copyFile(runtimeManifest,path.join(runtime,'HandheldCompanion.runtime.json'));
 const sharedSource=path.resolve(runtimeSource,'../..','ymcc-shared-runtime'),shared=path.join(power,'ymcc-shared-runtime');
 for(const entry of runtimeDocument.sharedDependencies?.files??[]){const source=listed(sharedSource,entry.path);if(hash(source)!==entry.sha256.toLowerCase())throw Error('Declared genuine shared hash mismatch: '+entry.path);copyFile(source,path.join(shared,entry.path));}
 const info={hostRoot:host,runtimeRoot:runtime,recordPath:path.join(dir,'records','YeManFanHost.deployment.json')};const configuration=path.join(dir,'configuration.json');fs.writeFileSync(configuration,JSON.stringify(info,null,2));
 return{dir,configuration,sourcePins};
}
export function runValidationProof({out,harness,candidate,payload,helper,hc,run}){
 const root=path.join(out,'deployment-validation');fs.mkdirSync(root);const rows=[];
 const rawResult=path.join(root,'unmodified-static.json');run('dotnet',[harness,'validation-static',candidate,helper,hc,rawResult],path.relative(out,path.join(root,'unmodified-static.log')),30000);
 const raw=read(rawResult);if(raw.status!=='PASS'||raw.nativeAttempts!==0||raw.hostMainStarted!==false||ordered(raw.cases.map(c=>c.id))!==ordered(validationStaticIds))throw Error('Unmodified actual static validator/ACL consumers failed');
 let positiveHost;
 for(const mode of ['positive',...Object.keys(failureSets)]){
  const dir=path.join(root,mode);fs.mkdirSync(dir);const leaf=path.join(dir,'leaf-payload');stageFaultPayload(payload,leaf);const relative=name=>path.relative(out,path.join(dir,name));let input=candidate,mutation=null;
  if(mode!=='positive'){
   input=path.join(dir,'faulted-host.dll');const proof=path.join(dir,'fault-proof.json');run('dotnet',[harness,'fault-validation-'+mode,candidate,input,proof],relative('fault-generation.log'),30000);
   mutation=read(proof);const method=mode==='binding'?'::ValidateRuntimeManifestBinding(':mode==='fast-path'?'::ValidateDeploymentGenerationForHcLoad(':'::IsPrivateCapabilityAcl(';
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(candidate)||mutation.faultSha256!==hash(input)||mutation.changedMethods.length!==1||!mutation.changedMethods[0].includes(method))throw Error('Validation fault must change exactly its one named method');
  }
  const host=path.join(leaf,'YeManFanHost.dll'),portsPath=path.join(dir,'ports-proof.json');run('dotnet',[harness,'validation-path-ports',input,host,portsPath],relative('ports.log'),30000);
  const ports=read(portsPath);if(ports.status!=='PASS'||ports.originalValidationInstructionsRetained!==true||ports.sourceSha256!==hash(input)||ports.instrumentedSha256!==hash(host)||ports.changedMethods.length!==2||ports.changedMethods.some(name=>!name.includes('::ValidateDeploymentGenerationForHcLoad(')&&!name.includes('::ValidateSplitPayloadManifest(')))throw Error('Only two base-directory getter operands may change');
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'validation-init',host,helper,hc,resultPath],relative('run.log'),30000,mode==='positive'?0:2);
  const result=read(resultPath),failures=result.cases.filter(c=>c.status==='FAIL');
  if(result.nativeAttempts!==0||result.hostMainStarted!==false||result.hardwareIo!==false||result.candidateHostSha256!==hash(host)||ordered(result.cases.map(c=>c.id))!==ordered(validationCaseIds))throw Error('Actual validation result identity or exact case set is incomplete');
  if(mode==='positive'){if(result.status!=='PASS'||failures.length)throw Error('Actual validation consumers failed');positiveHost=host;}
  else if(result.status!=='FAIL'||ordered(failures.map(c=>c.id))!==ordered(failureSets[mode])||failures.some(c=>!c.error?.includes(failureFragment(c.id))))throw Error('Validation private fault missed its expected business failures: '+mode);
  rows.push({mode,mutation,ports,resultPath,portsProofPath:portsPath,cases:result.cases.length,checks:result.checks,failedCases:failures.map(c=>c.id)});
 }
 const genuine=prepareGenuineValidation({root,payload,hc});const genuineResult=path.join(genuine.dir,'result.json');run('dotnet',[harness,'validation-real',positiveHost,helper,hc,genuineResult,genuine.configuration],path.relative(out,path.join(genuine.dir,'run.log')),30000);
 const real=read(genuineResult);if(real.status!=='PASS'||real.nativeAttempts!==0||real.hostMainStarted!==false||real.cases.length!==4||real.deviceInitializationExecuted!==false)throw Error('Genuine current Host/runtime/shared validation failed');
 const proof={status:'PASS',sourceCandidateSha256:hash(candidate),unmodifiedStaticCases:raw.cases.length,positiveCases:53,positiveChecks:rows[0].checks,caseIds:validationCaseIds,
  privateFaults:rows.slice(1).map(row=>({mode:row.mode,count:row.failedCases.length,ids:row.failedCases})),executions:rows,genuine:{resultPath:genuineResult,cases:4,configuration:genuine.configuration,sourcePins:genuine.sourcePins},
  actualManifestGenerationAndFactoryRefusalExecuted:true,onlyTwoIoGetterOperandsChanged:true,privateAclPredicateExecuted:true,formalAppDataAclWrapperExecuted:false,
  nativeLoaderRefusalOnly:true,originalDeviceAndSensorAlgorithmsExecuted:false,hostMainStarted:false,hardwareIo:false};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2));return{proof,evidence:tree(root).map(file=>({path:file,sha256:hash(file)})),external:genuine.sourcePins};
}
