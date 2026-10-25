import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

export const reviewedAuthoritySha256='64ad04c2085e208d6e27aaf6f4c15d2a5ad35b58a4076cec73e3b6b407c1900b';
const DOTNET='ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135';
const CSC='2c196b47e0ade88940af111beaad6a6ee5fa0df6152a847e7d3c81059ec15848';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:hash(file)});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const write=(file,data)=>fs.writeFileSync(file,JSON.stringify(data,null,2)+'\n');
const same=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const fail=(code,detail)=>{throw Object.assign(new Error(code),{code,detail});};
function need(value,code,detail){if(!value)fail(code,detail);}
function checked(p){need(p&&path.isAbsolute(p.path)&&hash(p.path)===p.sha256,'PRELOAD_METADATA_PIN_CHANGED',p);return p;}
function runtime(p){checked(p);const r=read(p.path).runtimeOptions;need(r.tfm==='net10.0'&&r.rollForward==='Disable'&&JSON.stringify(r.frameworks)===JSON.stringify([{name:'Microsoft.NETCore.App',version:'10.0.11'},{name:'Microsoft.WindowsDesktop.App',version:'10.0.11'}]),'PRELOAD_METADATA_RUNTIME_ORDER');}
function cleanEnv(){const env={...process.env};for(const n of Object.keys(env))if(/^(DOTNET_|CORECLR_|COR_|COMPLUS_)/i.test(n))delete env[n];return {...env,DOTNET_ROOT:'C:/Program Files/dotnet',DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_EnableDiagnostics:'0'};}
export function assertPreloadMetadataArgv(argv,step){
 need(Array.isArray(argv)&&argv.length===7&&argv[1]==='check-default-metadata','PRELOAD_METADATA_ONLY_EXACT_MODE_AND_ARGC');
 const exact=[step.tool.path,'check-default-metadata',step.policy.path,step.policy.sha256,step.authority.path,step.authority.sha256,step.receipt];
 need(JSON.stringify(argv)===JSON.stringify(exact),'PRELOAD_METADATA_EXACT_INPUT_VECTOR');return argv;
}
export function createDefaultPreloadMetadataStep({outDir,policyPin,authorityPin,runtimeconfigPin,dotnetPin,cscPin}){
 for(const p of [policyPin,authorityPin,runtimeconfigPin,dotnetPin,cscPin])checked(p);
 need(dotnetPin.sha256===DOTNET&&cscPin.sha256===CSC,'PRELOAD_METADATA_COMPILER_NOT_REVIEWED');runtime(runtimeconfigPin);
 const out=path.resolve(outDir);need(!fs.existsSync(out),'PRELOAD_METADATA_FRESH_STEP_OUTPUT_REQUIRED');
 need(!same(out,path.dirname(policyPin.path))&&!same(out,path.dirname(authorityPin.path)),'PRELOAD_METADATA_OUTPUT_OVERLAPS_INPUT');
 fs.mkdirSync(out,{recursive:true});const tool=path.join(out,'DefaultPreloadMetadata.dll'),source=fileURLToPath(new URL('./load-authority/DefaultPreloadMetadata.cs',import.meta.url));
 const refs='C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/10.0.11/ref/net10.0',rsp=path.join(out,'compile.rsp');
 fs.writeFileSync(rsp,['/nologo','/target:exe','/nullable:enable','/debug:portable','/pdb:"'+tool.replace(/\.dll$/,'.pdb')+'"','/out:"'+tool+'"',...fs.readdirSync(refs).filter(n=>n.endsWith('.dll')).map(n=>'/reference:"'+path.join(refs,n)+'"'),'"'+source+'"'].join('\n'));
 const start=Date.now(),child=spawnSync(dotnetPin.path,[cscPin.path,'@'+rsp],{cwd:out,env:cleanEnv(),shell:false,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:4*1024*1024});
 fs.writeFileSync(path.join(out,'compile.log'),(child.stdout??'')+'\n'+(child.stderr??''));
 const command={program:dotnetPin,args:[cscPin.path,'@'+rsp],exitCode:child.status,error:child.error?.message??null,signal:child.signal,naturalExit:child.status!==null&&!child.error&&!child.signal,durationMs:Date.now()-start};write(path.join(out,'compile-receipt.json'),command);
 need(child.status===0&&command.naturalExit,'PRELOAD_METADATA_SELF_COMPILATION_FAILED',command);
 const cfg=tool.replace(/\.dll$/,'.runtimeconfig.json');fs.copyFileSync(runtimeconfigPin.path,cfg);need(hash(cfg)===runtimeconfigPin.sha256,'PRELOAD_METADATA_RUNTIME_COPY_CHANGED');
 const step={schemaVersion:1,status:'PREPARED_METADATA_STEP_NOT_EXECUTED',mode:'check-default-metadata',tool:pin(tool),toolPdb:pin(tool.replace(/\.dll$/,'.pdb')),source:pin(source),runtimeconfig:pin(cfg),sourceRuntimeconfig:runtimeconfigPin,policy:policyPin,authority:authorityPin,dotnet:dotnetPin,csc:cscPin,compileReceipt:pin(path.join(out,'compile-receipt.json')),receipt:path.join(out,'preload-metadata-receipt.json'),authorityMatchesReviewedBuild:authorityPin.sha256===reviewedAuthoritySha256,executionRequiresExplicitMetadataAuthorization:true,sourceApprovalGranted:false,producerExecutionApprovalGranted:false,fullHostApproved:false};
 step.argv=[tool,'check-default-metadata',policyPin.path,policyPin.sha256,authorityPin.path,authorityPin.sha256,step.receipt];assertPreloadMetadataArgv(step.argv,step);
 const file=path.join(out,'preload-metadata-step.json');write(file,step);return pin(file);
}
export function executeDefaultPreloadMetadataStep({stepPin,toolAuditPin}){
 checked(stepPin);checked(toolAuditPin);const step=read(stepPin.path),audit=read(toolAuditPin.path);
 need(audit.status==='PASS_OWN_PRELOAD_METADATA_TOOL'&&audit.tool.sha256===step.tool.sha256&&audit.source.sha256===step.source.sha256,'PRELOAD_METADATA_TOOL_AUDIT_REQUIRED');
 for(const p of [step.tool,step.toolPdb,step.source,step.runtimeconfig,step.policy,step.authority,step.dotnet])checked(p);
 runtime(step.runtimeconfig);need(step.authority.sha256===reviewedAuthoritySha256&&step.authorityMatchesReviewedBuild===true,'PRELOAD_METADATA_AUTHORITY_BUILD_REVIEW_REQUIRED');
 need(step.dotnet.sha256===DOTNET,'PRELOAD_METADATA_DOTNET_NOT_REVIEWED');assertPreloadMetadataArgv(step.argv,step);need(!fs.existsSync(step.receipt),'PRELOAD_METADATA_FRESH_RECEIPT_REQUIRED');
 const start=Date.now(),child=spawnSync(step.dotnet.path,step.argv,{cwd:path.dirname(stepPin.path),env:cleanEnv(),shell:false,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:4*1024*1024});
 const command={program:step.dotnet,argv:step.argv,step:stepPin,toolAudit:toolAuditPin,exitCode:child.status,error:child.error?.message??null,signal:child.signal,naturalExit:child.status!==null&&!child.error&&!child.signal,durationMs:Date.now()-start,stdout:child.stdout,stderr:child.stderr};
 const commandFile=path.join(path.dirname(stepPin.path),'execute-receipt.json');write(commandFile,command);
 need(command.naturalExit&&[0,3].includes(child.status),'PRELOAD_METADATA_TOOL_PROCESS_FAILED',command);
 const report=read(step.receipt);need(report.exitCode===child.status&&report.targetAssembliesObserved===false&&report.loadPinExecuted===false&&report.attachExecuted===false&&report.producerExecuted===false&&report.observerErrors.length===0,'PRELOAD_METADATA_UNEXPECTED_EXECUTION',report);
 const allowed=[step.tool.path,step.authority.path].map(p=>path.resolve(p).toLowerCase()),roots=['C:/Program Files/dotnet/shared/Microsoft.NETCore.App/10.0.11/','C:/Program Files/dotnet/shared/Microsoft.WindowsDesktop.App/10.0.11/'].map(p=>path.resolve(p).toLowerCase()+path.sep);
 const before=new Map(report.actualLoadedManagedBefore.map(r=>[r.identity+'|'+r.path,r]));
 for(const row of [...report.actualLoadedManagedBefore,...report.actualLoadedManagedAfter,...report.assemblyLoadEvents]){
  need(!row.target&&!row.dynamic&&typeof row.path==='string','PRELOAD_METADATA_TARGET_OR_DYNAMIC_ASSEMBLY',row);
  const name=path.resolve(row.path).toLowerCase();need(allowed.includes(name)||roots.some(root=>name.startsWith(root)),'PRELOAD_METADATA_UNAPPROVED_ACTUAL_LOADED_PATH',row);checked(row);
  const prior=before.get(row.identity+'|'+row.path);if(prior)need(prior.sha256===row.sha256,'PRELOAD_METADATA_LOADED_SHA_CHANGED',row);
 }
 for(const p of [step.tool,step.source,step.policy,step.authority,step.runtimeconfig])checked(p);
 return {status:report.status,receipt:pin(step.receipt),command:pin(commandFile),targetAssembliesObserved:0,loadPinExecuted:false,attachExecuted:false,producerExecuted:false};
}
