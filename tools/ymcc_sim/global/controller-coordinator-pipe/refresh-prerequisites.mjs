// Reproducible offline prerequisite work. Source identity and owned paths are
// per-run inputs; no snapshot name or old artifact directory is baked into code.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const moduleDir=path.dirname(fileURLToPath(import.meta.url));
const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const LIFECYCLE='tools/ymcc_sim/global/controller-coordinator-lifecycle';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const need=(v,m)=>{if(!v)throw Error('PREREQUISITE_REFRESH_'+m)};
const within=(a,b)=>{const r=path.relative(path.resolve(a),path.resolve(b));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep))};
const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n')};
const safeRelative=s=>typeof s==='string'&&s&&!path.isAbsolute(s)&&!s.includes('\\')&&!s.includes(':')&&!s.split('/').some(x=>!x||x==='.'||x==='..');
export function refreshPrerequisites({repoRoot,outRoot,inputBindings,safetyReview,previousSourceManifest,stage}){
 need(['prepare','build','bind','verify'].includes(stage),'EXPLICIT_STAGE_REQUIRED');
 for(const [k,v]of Object.entries({repoRoot,outRoot,inputBindings}))need(typeof v==='string'&&path.isAbsolute(v),'ABSOLUTE_'+k);
 repoRoot=fs.realpathSync(repoRoot);outRoot=path.resolve(outRoot);inputBindings=path.resolve(inputBindings);
 need(!within(repoRoot,outRoot)&&!within(outRoot,repoRoot),'INPUT_OUTPUT_OVERLAP');
 const binding=read(inputBindings);
 need(binding.creationStatus==='PASS'&&binding.captureComplete===true&&binding.snapshotIntegrityVerified===true,'UNVERIFIED_SOURCE_BINDING');
 need(hash(binding.manifest)===binding.manifestSha256,'SOURCE_MANIFEST_CHANGED');
 const sourceManifest=read(binding.manifest),members=new Map(sourceManifest.files.map(r=>[r.path,r]));
 need(members.size===sourceManifest.files.length&&members.get('native/main.cpp')?.sha256===binding.productMainSha256,'SOURCE_MEMBERSHIP');
 need(hash(path.join(repoRoot,'native/main.cpp'))===binding.productMainSha256,'UNKNOWN_SELECTED_MAIN');
 const repo=path.join(outRoot,'overlay/repo'),build=path.join(outRoot,'Build/Validation/prerequisites');
 const layoutFile=path.join(repo,MODULE,'r3-prerequisite-root.json');
 const env={...process.env,TEMP:path.join(outRoot,'environment/temp'),TMP:path.join(outRoot,'environment/temp'),DOTNET_CLI_HOME:path.join(outRoot,'environment/dotnet-home'),NUGET_PACKAGES:path.join(outRoot,'environment/nuget'),DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'};
 function run(name,script,args,timeoutMs){
  const startedAt=new Date().toISOString();const p=spawnSync(process.execPath,[script,...args],{cwd:outRoot,env,encoding:'utf8',windowsHide:true,timeout:timeoutMs,maxBuffer:8*1024*1024});
  fs.writeFileSync(path.join(outRoot,name+'.stdout.log'),p.stdout??'');fs.writeFileSync(path.join(outRoot,name+'.stderr.log'),p.stderr??'');
  const row={name,script,scriptSha256:hash(script),args,startedAt,endedAt:new Date().toISOString(),exitCode:p.status,signal:p.signal,timeoutMs:timeoutMs??null,error:p.error?.message??null};
  const ledgerFile=path.join(outRoot,'refresh-process-ledger.json'),ledger=fs.existsSync(ledgerFile)?read(ledgerFile):[];ledger.push(row);write(ledgerFile,ledger);return row;
 }
 if(stage==='prepare'){
  need(!fs.existsSync(outRoot),'FRESH_OUTPUT_REQUIRED');fs.mkdirSync(repo,{recursive:true});
  // Copy the existing capture manifest's exact member list, not a new inventory.
  for(const r of sourceManifest.files){
   need(safeRelative(r.path),'SOURCE_PATH');const source=path.join(repoRoot,r.path),dest=path.join(repo,r.path);
   need(within(repoRoot,fs.realpathSync(source))&&!fs.lstatSync(source).isSymbolicLink(),'SOURCE_REDIRECTION:'+r.path);
   need(hash(source)===r.sha256,'UNKNOWN_SELECTED_SOURCE:'+r.path);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source,dest);
  }
  const patches=['adapter.mjs','bind-r3-fixtures.mjs','r3-managed-builder.mjs','r3-source-equivalence.mjs','r3-source-equivalence.test.mjs','r3-contract.mjs','bind-current-prerequisites.mjs','refresh-prerequisites.mjs'];
  const patchLedger=[];for(const name of patches){const source=path.join(moduleDir,name),dest=path.join(repo,MODULE,name);const before=fs.existsSync(dest)?hash(dest):null;fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source,dest);patchLedger.push({path:MODULE+'/'+name,beforeSha256:before,afterSha256:hash(dest)});}
  const base=read(path.join(moduleDir,'r3-prerequisite-root.json'));
  const sourceDelta=previousSourceManifest?read(previousSourceManifest).records.map(r=>({path:r.path,beforeSha256:r.sha256,afterSha256:members.get(r.path)?.sha256??null})).filter(r=>r.beforeSha256!==r.afterSha256):null;
  write(path.join(outRoot,'SOURCE-DELTA.json'),{previousSourceManifest:previousSourceManifest??null,changes:sourceDelta,meaning:'Comparison against previous actual compiler source ledger; rebuild and original assertions remain required.'});
  const expectedManagedSources=base.expectedManagedSources.map(r=>{need(members.has(r.relative),'MANAGED_MEMBER_MISSING');return{relative:r.relative,sha256:members.get(r.relative).sha256}});
  fs.mkdirSync(build,{recursive:true});
  write(layoutFile,{schemaVersion:1,sourceVersion:path.basename(binding.snapshotRoot),sourceRoot:path.resolve(binding.snapshotRoot),selectedRepoRoot:repoRoot,productMainSha256:binding.productMainSha256,workspaceRoot:outRoot,authorizedBuildRoot:build,expectedManagedSources,fixedRunInputBinding:{path:inputBindings,sha256:hash(inputBindings)},sourcePolicy:'Regenerated per-run exact source profile; fresh owned outputs only. Unknown source and unreviewed execution remain rejected.'});
  write(path.join(outRoot,'PREPARE.json'),{status:'PREPARED_NOT_AUTHORIZED_TO_EXECUTE',repoRoot,stagedRepoRoot:repo,sourceBinding:{path:inputBindings,sha256:hash(inputBindings)},patches:patchLedger,reviewRequired:{sourceRoot:repoRoot,bindingSha256:hash(inputBindings),nativeBuilderSha256:hash(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs')),managedBuilderSha256:hash(path.join(repo,MODULE,'r3-managed-builder.mjs')),status:'REVIEWED_BEFORE_EXECUTION',isolated:true,realSdkLoaded:false,hardwareIo:false,normalProductMainExecuted:false,osSleep:false}});
  return {status:'PREPARED',repo,build};
 }
 need(fs.existsSync(layoutFile),'PREPARE_REQUIRED');const profile=read(layoutFile);
 need(path.resolve(profile.selectedRepoRoot)===repoRoot&&path.resolve(profile.workspaceRoot)===outRoot&&profile.fixedRunInputBinding.sha256===hash(inputBindings),'BATCH_ARGUMENT_CHANGED');
 need(typeof safetyReview==='string'&&path.isAbsolute(safetyReview),'SAFETY_REVIEW_REQUIRED');
 const review=read(safetyReview),required=read(path.join(outRoot,'PREPARE.json')).reviewRequired;
 for(const [k,v]of Object.entries(required))need(review[k]===v,'ISOLATION_NOT_PROVEN:'+k);
 // These are source-specific human/agent review facts, not an auto-generated
 // permission grant. Changed original builders or safety ends need new review.
 need(review.nativeBuilderSha256===hash(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'))&&review.managedBuilderSha256===hash(path.join(repo,MODULE,'r3-managed-builder.mjs')),'REVIEWED_BUILDER_CHANGED');
 for(const dir of [env.TEMP,env.DOTNET_CLI_HOME,env.NUGET_PACKAGES])fs.mkdirSync(dir,{recursive:true});
 if(stage==='build'){
  const managed=path.join(build,'managed'),managedRow=run('managed-build',path.join(repo,MODULE,'r3-managed-builder.mjs'),['--out',managed]);
  need(managedRow.exitCode===0,'MANAGED_COMPILE_FAILED');
  const dll=path.join(managed,'bin/Release/net10.0/Fixture.dll');
  const defs=[['positive',null,true],['cancellation','rollback-cancellation-guards',false],['psOwnership','rollback-ps-endpoint-ownership',false],['psBinding','rollback-ps-binding-revision',false],['admission','bypass-admission',false],['inheritance','rollback-recovery-inheritance',false],['switch-return','rollback-switch-return-rearm',false],['switch-owner','rollback-switch-fault-owner',false]];
  for(const [name,mutation,all]of defs){const dir=path.join(build,name),args=['--out',dir];if(mutation)args.push('--mutation',mutation);if(all||['cancellation','psOwnership','psBinding'].includes(name))args.push('--managed-dll',dll);if(all)args.push('--suite','all');run('build-'+name,path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'),args);need(fs.existsSync(path.join(dir,'RESULT.json')),'COMPILE_OR_EXECUTION_INCOMPLETE:'+name);}
  return {status:'EXECUTED_RAW_RESULTS_PRESERVED',build};
 }
 if(stage==='bind'){
  const r=run('bind-current',path.join(repo,MODULE,'bind-current-prerequisites.mjs'),[]);need(r.exitCode===0,'BIND_FAILED');return{status:'BOUND_NOT_APPROVED'};
 }
 const results=[];for(const key of ['positive','cancellation','psOwnership','psBinding','lifecycle']){
  const target=path.join(outRoot,'leaf-evidence',key);fs.mkdirSync(path.dirname(target),{recursive:true});
  const script=path.join(repo,key==='lifecycle'?LIFECYCLE:MODULE,'run.mjs');const args=key==='lifecycle'?['--evidence-dir',target]:['--case',key,'--evidence-dir',target];
  const process=run('leaf-'+key,script,args,key==='lifecycle'?90000:120000),resultFile=path.join(target,'result.json');
  results.push({key,...process,result:fs.existsSync(resultFile)?{path:resultFile,sha256:hash(resultFile),status:read(resultFile).status}:null});
 }
 write(path.join(outRoot,'LEAF-VERIFICATION.json'),{status:results.every(r=>r.exitCode===0&&r.result?.status==='PASS')?'PASS':'FAIL',results,unknownSourceOrSemanticFailurePreserved:true,publicFrontdoorAndAuditPerformed:false});return{status:results.every(r=>r.exitCode===0)?'PASS':'FAIL'};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2),o={};need(args.length%2===0,'ARGUMENT_PAIRS');for(let i=0;i<args.length;i+=2){need(['--repo-root','--out-root','--input-bindings','--safety-review','--previous-source-manifest','--stage'].includes(args[i])&&!Object.hasOwn(o,args[i]),'ARGUMENT');o[args[i]]=args[i+1];}const r=refreshPrerequisites({repoRoot:o['--repo-root'],outRoot:o['--out-root'],inputBindings:o['--input-bindings'],safetyReview:o['--safety-review'],previousSourceManifest:o['--previous-source-manifest'],stage:o['--stage']});console.log(JSON.stringify(r));if(r.status==='FAIL')process.exitCode=1;}catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
