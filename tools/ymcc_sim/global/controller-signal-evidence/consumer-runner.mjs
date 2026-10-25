import {executeManagedTerminal,managedCurrentVersion} from './managed-terminal-receipts.mjs';
import {executePrimaryThread} from './primary-thread-receipts.mjs';
import {composeRegistrationContext, compositionContract, prepareCompositionToolchain, sealCompositionNestedClosure} from './owned-cleanup-composition-receipts.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {toolInputs, historicalInputs, sourceInputs, sensorEnsureIdentity} from './adapter.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const CONSUMER_TIMEOUT_MS=1200000;
export const JOB_TIMEOUT_MS=900000;
export const PARALLELISM=4;
export const schema=JSON.parse(fs.readFileSync(new URL('./consumer-schema.json',import.meta.url),'utf8'));
export async function inspectAll(tasks,width=PARALLELISM){
 const outcomes=[];
 for(let i=0;i<tasks.length;i+=width)outcomes.push(...await Promise.allSettled(tasks.slice(i,i+width).map(task=>Promise.resolve().then(task))));
 const failures=outcomes.flatMap((r,i)=>r.status==='rejected'?[{index:i,error:r.reason?.message??String(r.reason)}]:[]);
 if(failures.length)throw new AggregateError(failures.map(f=>new Error(f.index+': '+f.error)),'CONSUMER_BATCH_FAILED:'+JSON.stringify(failures));
 return outcomes.map(r=>r.value);
}
export function sdkBatch(tools){return '@echo off\r\ncall "'+tools.vcvars+'" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset "WindowsSDKVersion='+tools.sdkVersion+'/"\r\nset "INCLUDE='+tools.native.include.join(';')+'"\r\nset "LIB='+tools.native.lib.join(';')+'"\r\n';}
// Shared by the full queue and the explicitly scoped registration front door.
// The aggregate is a fresh canonical receipt; its six real children retain their
// original owned workspaces and exact per-build invocation/resource evidence.
export async function runManagedTerminalGroup({repo,out,sourceHashes,tools,workspace,run}){
 return executeManagedTerminal({repo,sourceHashes,tools,workspace,run,out:path.join(out,'managed-terminal'),version:managedCurrentVersion()});
}
export async function runConsumers({repo,out,env}){
 const tools=toolInputs(repo),historical=historicalInputs(repo),cleanEnv=consumerEnvironment({ambient:env,tools,out,nodeExecutable:process.execPath}),execution={schemaVersion:1,status:'RUNNING',pass:false,parallelism:PARALLELISM,jobTimeoutMs:JOB_TIMEOUT_MS,jobs:[],toolsBefore:[],toolsAfter:[]};
 const identity=()=>tools.files.map(r=>{assert.equal(hash(r.path),r.sha256,'CONSUMER_TOOL_CHANGED:'+r.path);return {...r};});
 const primaryIdentity=()=>Object.entries(tools.primary).map(([role,file])=>{
  const declared=tools.files.find(r=>path.resolve(r.path).toLowerCase()===path.resolve(file).toLowerCase());
  assert(declared&&hash(file)===declared.sha256,'CONSUMER_PRIMARY_TOOL_CHANGED:'+role);
  return {role,path:file,sha256:declared.sha256};
 });
 execution.toolsBefore=identity();execution.primaryToolsBefore=primaryIdentity();
 const sourceHashes=Object.fromEntries(sourceInputs.map(rel=>[rel,hash(path.join(repo,rel))]));
 const workspace=createOwnedWorkspace({repo,output:out,primary:tools.primary,sourceHashes});execution.workspace={root:workspace.root,identity:workspace.identity};
 const save=()=>fs.writeFileSync(path.join(out,'consumer-execution.json'),JSON.stringify(execution,null,2)+'\n');save();
 const wrapper=path.join(out,'reviewed-sdk.cmd');fs.writeFileSync(wrapper,sdkBatch(tools));execution.sdkWrapper={path:wrapper,sha256:hash(wrapper)};
 const python=tools.python;
 async function run(id,exe,args,expectedExit=0,extra={},owned=workspace.allocate(id)){
  workspace.verify(owned);
  const started=Date.now();const row={id,executable:exe,args,expectedExit,ownedWorkspace:owned,primaryToolsBefore:primaryIdentity(),startedAt:new Date().toISOString(),exitCode:null,signal:null,error:null,timedOut:false};execution.jobs.push(row);save();
  const childEnv={...cleanEnv,...extra,TEMP:owned.temp,TMP:owned.temp};
  const stdout=[],stderr=[];let bytes=0;
  await new Promise(resolve=>{const child=spawn(exe,args,{cwd:repo,env:childEnv,windowsHide:true,stdio:['ignore','pipe','pipe']});let timer;const end=()=>{clearTimeout(timer);resolve();};
   timer=setTimeout(()=>{row.timedOut=true;child.kill();},JOB_TIMEOUT_MS);
   const collect=target=>b=>{bytes+=b.length;if(bytes<=8*1024*1024)target.push(b);else{row.error='OUTPUT_LIMIT_EXCEEDED';child.kill();}};
   child.stdout.on('data',collect(stdout));child.stderr.on('data',collect(stderr));child.on('error',error=>{row.error=error.message;end();});child.on('close',(code,signal)=>{row.exitCode=code;row.signal=signal;end();});});
  workspace.verify(owned);
  row.primaryToolsAfter=primaryIdentity();
  row.elapsedMs=Date.now()-started;row.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(out,id+'-entry.log'),Buffer.concat([...stdout,...stderr]));save();
  assert(!row.timedOut&&!row.error&&!row.signal&&row.exitCode===expectedExit,'ACTUAL_CONSUMER_FAILED:'+id+':'+JSON.stringify(row));return row;
 }
 // Each child can generate deeper mutant/source paths than its base output.
 // Keep all actual builds short, not only partial-init; publish original receipt
 // bytes with their actual owned path and hash for strict validation/auditing.
 function publishReceipt(row,file,canonical){
  const relative=path.relative(row.ownedWorkspace.buildOutput,file);
  assert(relative&&!path.isAbsolute(relative)&&!relative.startsWith('..'),'CHILD_RECEIPT_ESCAPED_WORKSPACE');
  assert(!fs.existsSync(canonical),'FRESH_CANONICAL_RECEIPT_REQUIRED');
  const originalSha256=hash(file);fs.mkdirSync(path.dirname(canonical),{recursive:true});fs.copyFileSync(file,canonical);
  assert.equal(hash(canonical),originalSha256,'PUBLISHED_RECEIPT_BYTES_CHANGED');
  row.publishedReceipts??=[];row.publishedReceipts.push({actualPath:file,canonicalPath:canonical,sha256:originalSha256});save();
 }
 const jobs=[];
 for(const [script,key,folder] of [['msi_external_gamepad_decode_selftest.mjs','YMCC_MSI_DECODE_OUT','descriptor-decode'],['msi_external_gamepad_witness_selftest.mjs','YMCC_MSI_WITNESS_OUT','semantic-witness'],['input_timer_evidence_selftest.mjs','YMCC_INPUT_TIMER_EVIDENCE_OUT','timer-recorder']])jobs.push(async()=>{
  const owned=workspace.allocate(folder),name=folder==='descriptor-decode'?'source-binding.json':'RESULT.json';
  const row=await run(folder,process.execPath,[path.join(repo,'tools',script)],0,{[key]:owned.buildOutput},owned);
  publishReceipt(row,path.join(owned.buildOutput,name),path.join(out,folder,name));
 });
 for(const [script,folder] of [['msi_visibility_resume_generation_selftest.py','resume-generation'],['input_timer_evidence_adapter_selftest.py','timer-adapter'],['input_capture_worker_retirement_selftest.py','worker-retirement'],['input_capture_stop_wait_selftest.py','stop-wait'],['input_capture_startup_wait_selftest.py','startup-wait'],['input_capture_startup_lifecycle_selftest.py','startup-lifecycle'],['input_host_exit_fence_selftest.py','exit-fence'],['input_capture_stop_receipt_selftest.py','stop-receipt'],['input_capture_exit_continuation_selftest.py','exit-continuation'],['input_capture_sensor_owners_selftest.py','sensor-owners'],['ymcc_sim/gamebar_trace_owner/run.py','gamebar-owner']])jobs.push(async()=>{
  const owned=workspace.allocate(folder),args=['-B',path.join(repo,'tools',script),'--output-root',owned.buildOutput];
  if(folder==='exit-fence'&&schema.exitFenceCurrentV2)args.push('--source-contract-manifest',path.join(repo,schema.exitFenceCurrentV2.sourceContract),'--source-contract-sha256',schema.exitFenceCurrentV2.sourceContractSha256);
  const row=await run(folder,python,args,0,{},owned);
  let actual=path.join(owned.buildOutput,folder==='startup-wait'?'result.json':'RESULT.json');
  if(folder==='gamebar-owner')actual=JSON.parse(fs.readFileSync(path.join(owned.buildOutput,'LATEST.json'),'utf8')).result;
  publishReceipt(row,actual,path.join(out,folder,folder==='startup-wait'?'result.json':'RESULT.json'));
 });
 jobs.push(async()=>{
  const owned=workspace.allocate('sensor-ensure');
  fs.mkdirSync(owned.buildOutput);
  const pin=sensorEnsureIdentity(repo),manifest=JSON.parse(fs.readFileSync(pin.path,'utf8'));
  assert.equal(manifest.mainSha256,sourceHashes['native/main.cpp'],'SENSOR_ENSURE_PARENT_FINAL_FREEZE_REQUIRED');
  assert.equal(manifest.sourceBindings?.['native/main.cpp'],manifest.mainSha256,'SENSOR_ENSURE_APPROVED_MAIN_PIN_REQUIRED');
  const closure=path.join(owned.buildOutput,'runtime_sourceClosure.json');
  fs.writeFileSync(closure,JSON.stringify(sourceHashes,null,2)+'\n',{flag:'wx'});
  const row=await run('sensor-ensure',python,['-B',path.join(repo,'tools/input_capture_sensor_ensure_selftest.py'),
    '--output-root',owned.buildOutput,'--runtime_sourceClosure',closure,'--source-identity-manifest',pin.path,
    '--historical-source',historical['sensor-ensure/native/main.cpp'].path],0,{},owned);
  publishReceipt(row,path.join(owned.buildOutput,'RESULT.json'),path.join(out,'sensor-ensure','RESULT.json'));
 });
 jobs.push(async()=>{
  const owned=workspace.allocate('owned-cleanup-composition');
  const pin=sensorEnsureIdentity(repo),approved=JSON.parse(fs.readFileSync(pin.path,'utf8'));
  assert.equal(approved.mainSha256,sourceHashes['native/main.cpp'],'PIPE_COMPOSITION_PARENT_FINAL_FREEZE_REQUIRED');
  assert.equal(approved.sourceBindings?.['native/main.cpp'],approved.mainSha256,'PIPE_COMPOSITION_APPROVED_MAIN_PIN_REQUIRED');
  const {contract}=compositionContract(repo),closure=path.join(owned.directory,'runtime_sourceClosure.json');
  fs.writeFileSync(closure,JSON.stringify(sourceHashes,null,2)+'\n',{flag:'wx'});
  const toolchainView=prepareCompositionToolchain({repo,owned});
  const args=['-B',path.join(repo,contract.frontDoorIntegration.consumer),'--require-wrapper','--out',owned.buildOutput,'--toolchain',toolchainView.path];
  const nestedToolClosure={before:sealCompositionNestedClosure({repo,owned,sourceHashes,phase:'before'})};
  let row;
  try{row=await run('owned-cleanup-composition',python,args,0,{},owned);}
  finally{nestedToolClosure.after=sealCompositionNestedClosure({repo,owned,sourceHashes,phase:'after'});}
  assert.equal(hash(pin.path),pin.sha256,'PIPE_COMPOSITION_APPROVED_MANIFEST_CHANGED');
  for(const [relative,sha256] of Object.entries(sourceHashes))assert.equal(hash(path.join(repo,relative)),sha256,'PIPE_COMPOSITION_CURRENT_SOURCE_CHANGED:'+relative);
  composeRegistrationContext({repo,sourceHashes,owned,job:row,identity:pin,closure,toolchainView,nestedToolClosure});
  publishReceipt(row,path.join(owned.buildOutput,'RESULT.json'),path.join(out,'owned-cleanup-composition','RESULT.json'));
 });
 jobs.push(async()=>{
  const {job,file}=await executePrimaryThread({repo,sourceHashes,workspace,run,identity:sensorEnsureIdentity(repo),python});
  publishReceipt(job,file,path.join(out,'primary-thread','RESULT.json'));
 });
 jobs.push(async()=>{
  await runManagedTerminalGroup({repo,out,sourceHashes,tools,workspace,run});
 });
 jobs.push(async()=>{
  const owned=workspace.allocate('dsu-owner'),folder=owned.buildOutput;
  const row=await run('dsu-owner',process.execPath,[path.join(repo,'tools/fixtures/dsu_lifecycle/run.mjs'),folder],0,{},owned);
  const receipts=fs.readdirSync(folder,{withFileTypes:true}).filter(e=>e.isDirectory()&&fs.existsSync(path.join(folder,e.name,'RESULT.json')));assert.equal(receipts.length,1,'ONE_FRESH_DSU_RUN_REQUIRED');
  publishReceipt(row,path.join(folder,receipts[0].name,'RESULT.json'),path.join(out,'dsu-owner','RESULT.json'));
 });
 jobs.push(async()=>{
  const owned=workspace.allocate('x2-owner'),row=await run('x2-owner',process.execPath,[path.join(repo,'tools/hc_onex_x2_windows_selftest.mjs'),'--vcvars',wrapper,'--out',owned.buildOutput],0,{},owned);
  publishReceipt(row,path.join(owned.buildOutput,'source-bindings.json'),path.join(out,'x2-owner','RESULT.json'));
 });
 jobs.push(async()=>{
  const target=path.join(out,'partial-init');fs.mkdirSync(target);const results=[];
  // All variants run once, including the exact historical closure. Failure in
  // one variant never prevents observing outcomes of the remaining variants.
  const tasks=schema.partialVariants.map(variant=>async()=>{
   // The old global case nesting also exceeded MSVC's include-path limit
   // for long mutant names. Retain the actual build at a short owned path.
   const owned=workspace.allocate('partial-'+variant);
   const args=['-B',path.join(repo,'tools/input_capture_partial_init_selftest.py'),'--output-root',owned.buildOutput,'--variant',variant];
   if(variant==='baseline'){const old=historical['baseline-dependencies/native/main.cpp'];args.push('--main',old.path,'--main-sha256',old.sha256,'--baseline-closure',historical['identity/BASELINE-DEPENDENCIES.json'].path);}
   if(variant==='legacy-emergency-mutant')args.push('--legacy-emergency-closure',historical['identity/LEGACY-EMERGENCY-DEPENDENCIES.json'].path);
   const exit=variant==='current'?0:2;await run('partial-'+variant,python,args,exit,{},owned);
   const file=path.join(owned.buildOutput,'RESULT.json'),canonical=path.join(target,variant,'RESULT.json');
   fs.mkdirSync(path.dirname(canonical),{recursive:true});fs.copyFileSync(file,canonical);
   results.push({variant,exitCode:exit,actualReceipt:{path:file,sha256:hash(file)},ownedWorkspace:owned,result:JSON.parse(fs.readFileSync(file,'utf8'))});
  });
  try{await inspectAll(tasks,2);}finally{results.sort((a,b)=>schema.partialVariants.indexOf(a.variant)-schema.partialVariants.indexOf(b.variant));fs.writeFileSync(path.join(target,'RESULT.json'),JSON.stringify({pass:results.length===schema.partialVariants.length,wholeLifecycleAcceptance:false,results},null,2)+'\n');}
 });
 try{await inspectAll(jobs);execution.toolsAfter=identity();for(const row of Object.values(historical))assert.equal(hash(row.path),row.sha256,'HISTORICAL_INPUT_CHANGED');execution.status='PASS';execution.pass=true;}
 catch(error){execution.status='FAIL';execution.error=error.message;throw error;}
 finally{
  try{execution.toolsAfter=identity();execution.primaryToolsAfter=primaryIdentity();workspace.verify();}
  catch(error){execution.pass=false;execution.status='FAIL';execution.identityError=error.message;throw error;}
  finally{execution.finishedAt=new Date().toISOString();save();}
 }
}
