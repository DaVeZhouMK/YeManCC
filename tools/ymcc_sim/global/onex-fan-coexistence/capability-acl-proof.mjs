import fs from 'node:fs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
const sorted=values=>JSON.stringify([...values].sort());
const kinds=['private','trusted-system-admins','directory-read','directory-write','file-read','file-write','file-permissions','inherited-directory','missing-file','directory-as-token','missing-directory','anonymous-deny','directory-acl-unreadable','file-acl-unreadable'];
export const capabilityAclIds=[...['direct','factory'].flatMap(layer=>kinds.map(kind=>'capability-acl-'+layer+':'+kind)),...['null','0','2'].map(value=>'capability-acl-diagnostic-blank:'+value),...['external','prefix-lookalike'].map(kind=>'capability-acl-diagnostic-scope:'+kind),...['case','normalized'].map(kind=>'capability-acl-formal-path:'+kind)];
const pathCases=['case','normalized'].map(kind=>'capability-acl-formal-path:'+kind);
const both=values=>['direct','factory'].flatMap(layer=>values.map(kind=>'capability-acl-'+layer+':'+kind));
const failures={return:[...both(kinds.filter(kind=>!['private','trusted-system-admins','anonymous-deny'].includes(kind))),...pathCases],directory:both(['directory-read','directory-write','inherited-directory']),file:[...both(['file-read','file-write','file-permissions']),...pathCases],scope:['external','prefix-lookalike'].map(kind=>'capability-acl-diagnostic-scope:'+kind)};
export function runCapabilityAclProof({out,harness,candidate,payload,helper,hc,run}){
 const root=path.join(out,'formal-capability-acl');fs.mkdirSync(root);
 const positivePayload=path.join(root,'positive-payload');stageFaultPayload(payload,positivePayload);const positive=path.join(positivePayload,'YeManFanHost.dll'),ports=path.join(root,'ports-proof.json');
 run('dotnet',[harness,'capability-acl-ports',candidate,positive,ports],path.relative(out,path.join(root,'ports.log')),30000);
 const portsProof=read(ports);if(portsProof.status!=='PASS'||portsProof.sourceSha256!==hash(candidate)||portsProof.instrumentedSha256!==hash(positive)||portsProof.onlyOneIoGetterOperandChanged!==true||portsProof.actualWindowsAclConsumersPreserved!==true)throw Error('Formal capability ACL instrumentation differs from the one allowed folder getter');
 const executions=[];
 for(const mode of ['positive',...Object.keys(failures)]){
  const dir=path.join(root,mode);fs.mkdirSync(dir);let host=positive,mutation=null;
  if(mode!=='positive'){
   const leaf=path.join(dir,'payload');stageFaultPayload(positivePayload,leaf);host=path.join(leaf,'YeManFanHost.dll');const faultProof=path.join(dir,'fault-proof.json');
   run('dotnet',[harness,'fault-capability-acl-'+mode,positive,host,faultProof],path.relative(out,path.join(dir,'fault.log')),30000);
   mutation=read(faultProof);if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(positive)||mutation.faultSha256!==hash(host)||mutation.changedMethods?.length!==1||!mutation.changedMethods[0].includes('::ValidateFormalSessionCapabilityAcl('))throw Error('Private ACL fault changed a different consumer');
  }
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'capability-acl',host,helper,hc,resultPath],path.relative(out,path.join(dir,'run.log')),60000,mode==='positive'?0:2);const result=read(resultPath),failed=result.cases.filter(c=>c.status==='FAIL');
  if(result.candidateHostSha256!==hash(host)||sorted(result.cases.map(c=>c.id))!==sorted(capabilityAclIds)||result.nativeAttempts!==0||result.hostMainStarted!==false||result.hardwareIo!==false||result.actualFormalCapabilityAclWrapperExecuted!==true||result.actualWindowsDirectoryAndFileAclReads!==true||result.existingUserOrInstalledAclChanged!==false||result.actualInstalledAppDataTouched!==false||result.restoredFixtureAcls!==4)throw Error('Formal capability ACL execution identity, scope or exact case set differs');
  if(mode==='positive'){if(result.status!=='PASS'||failed.length)throw Error('Actual formal capability ACL consumers failed');}
  else{
   const fragment=id=>id.startsWith('capability-acl-factory:')?'Actual ACL boundary must determine the factory failure':id.startsWith('capability-acl-diagnostic-scope:')?'Fan Host 会话令牌 ACL':'Actual formal wrapper must reject';
   if(result.status!=='FAIL'||sorted(failed.map(c=>c.id))!==sorted(failures[mode])||failed.some(c=>!c.error?.includes(fragment(c.id))))throw Error('Private ACL fault missed exact business assertions: '+mode);
  }
  executions.push({mode,hostSha256:hash(host),cases:result.cases.length,checks:result.checks,failedIds:failed.map(c=>c.id),resultPath,mutation});
 }
 const proof={status:'PASS',sourceCandidateSha256:hash(candidate),instrumentedCandidateSha256:hash(positive),portsProofPath:ports,portsProof,positiveCases:capabilityAclIds.length,positiveChecks:executions[0].checks,
  privateFaults:executions.slice(1).map(row=>({mode:row.mode,count:row.failedIds.length,ids:row.failedIds})),executions,
  actualFormalCapabilityAclWrapperExecuted:true,actualWindowsAclReadFailuresExecuted:true,actualFactoryValidationBoundaryExecuted:true,onlyOneFolderGetterOperandChanged:true,existingUserOrInstalledAclChanged:false,actualInstalledAppDataTouched:false,
  scope:'Actual original wrapper and factory with one LocalApplicationData getter redirected to new Build-only real Windows ACL fixtures; native/device/installed AppData acceptance remains separate',hardwareIo:false,hostMainStarted:false};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2)+'\n');return{proof,evidence:tree(root).map(file=>({path:file,sha256:hash(file)}))};
}
