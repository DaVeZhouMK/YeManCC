import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {prepareRealStoreSources} from './prepare.mjs';
import {MODULE,REAL_STORE_SOURCES} from './sources.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration} from '../../ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const pin=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};};
const save=(file,value)=>{const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,JSON.stringify(value,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}};
const inside=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
function plainParents(file){for(let current=path.resolve(file);;current=path.dirname(current)){if(fs.existsSync(current))assert(!fs.lstatSync(current).isSymbolicLink(),'REAL_STORE_REPARSE_PATH_REFUSED');if(path.dirname(current)===current)break;}}
export function parseRealStoreArguments(args){
 assert(Array.isArray(args),'REAL_STORE_ARGUMENT_ARRAY_REQUIRED');
 const options={profile:'Checked',selection:'both',budgetSeconds:600,prepareOnly:false};let enabled=false;const seen=new Set();
 for(let i=0;i<args.length;i++){
  const name=args[i];assert(!seen.has(name),'REAL_STORE_DUPLICATE_OPTION:'+name);seen.add(name);
  if(name==='--real-store')enabled=true;
  else if(name==='--prepare-only')options.prepareOnly=true;
  else if(['--out','--profile','--case','--budget-seconds'].includes(name)){
   const value=args[++i];assert(typeof value==='string'&&value.length>0,'REAL_STORE_OPTION_VALUE_REQUIRED');
   if(name==='--out')options.output=value;else if(name==='--profile')options.profile=value;else if(name==='--case')options.selection=value;else{assert(/^[1-9][0-9]*$/.test(value),'REAL_STORE_INTEGER_BUDGET_REQUIRED');options.budgetSeconds=Number(value);}
  }else throw Error('REAL_STORE_UNKNOWN_OPTION:'+name);
 }
 assert(enabled&&options.output&&path.isAbsolute(options.output),'REAL_STORE_EXPLICIT_ABSOLUTE_OUTPUT_REQUIRED');
 assert(['Release','Checked'].includes(options.profile),'REAL_STORE_UNKNOWN_PROFILE');
 assert(['both','success','sharing-denial'].includes(options.selection),'REAL_STORE_UNKNOWN_CASE');
 assert(Number.isSafeInteger(options.budgetSeconds)&&options.budgetSeconds>=1&&options.budgetSeconds<=1800,'REAL_STORE_SINGLE_INVOCATION_LIMIT');
 return options;
}
export async function runRealStoreFromArgs({repo,args}={}){
 const started=Date.now(),options=parseRealStoreArguments(args);repo=path.resolve(repo);
 assert.equal(path.resolve(repo,MODULE).toLowerCase(),here.toLowerCase(),'REAL_STORE_MODULE_MUST_BELONG_TO_SELECTED_REPO');
 const output=path.resolve(options.output);assert(output!==repo&&!inside(repo,output),'REAL_STORE_OUTPUT_INSIDE_SOURCE_REFUSED');plainParents(output);
 const deadline=started+options.budgetSeconds*1000;
 const binding=await prepareRealStoreSources({repo,output,profile:options.profile});
 const resultFile=path.join(binding.run,'ENTRY-RESULT.json');
 if(options.prepareOnly){const result={status:'PREPARED_NOT_EXECUTED',source:pin(path.join(binding.run,'REAL-STORE-SOURCES.json')),newCompiles:0,newNativeCalls:0,sourceReady:false};save(resultFile,result);console.log(JSON.stringify({...result,run:binding.run}));return result;}
 try{
  assert(deadline-Date.now()>150000,'REAL_STORE_ASSESSED_PREPARATION_COMPILE_AND_CLEANUP_TIME_REQUIRED');
  const toolchainFile=path.join(here,'toolchain.json'),toolchain=JSON.parse(fs.readFileSync(toolchainFile,'utf8'));
  assert.equal(toolchain.schemaVersion,1);const toolArtifacts=toolchain.files.map(row=>{const actual=pin(row.path);assert.equal(actual.sha256,row.sha256,'REAL_STORE_TOOL_CHANGED:'+row.path);assert.equal(actual.bytes,row.bytes);return actual;});
  const select=name=>{const rows=toolArtifacts.filter(row=>path.basename(row.path).toLowerCase()===name);assert.equal(rows.length,1,'REAL_STORE_UNIQUE_TOOL_REQUIRED:'+name);return rows[0];};
  const node=select('node.exe'),powershell=select('pwsh.exe'),cmd=select('cmd.exe'),telemetry=select('vctip.exe');assert.equal(pin(process.execPath).sha256,node.sha256,'REAL_STORE_PINNED_NODE_REQUIRED');
  const sourcePaths=[...REAL_STORE_SOURCES.map(relative=>path.join(repo,relative)),...binding.sourceRecords.map(row=>row.snapshot),binding.consumer.path,binding.writerInclude.path,binding.businessInclude.path,...binding.bridgeRows.map(row=>row.candidate.path),path.join(binding.run,'REAL-STORE-SOURCES.json'),path.join(binding.run,'source-binding.json'),...['production_types.h','production_debt_helpers.h','production_restore_helpers.h'].map(name=>path.join(binding.snapshot,'tools',name))];
  const unique=paths=>[...new Map(paths.map(file=>[path.resolve(file).toLowerCase(),path.resolve(file)])).values()];
  const sourceInputs=unique(sourcePaths).map(pin),parameters=options.selection==='both'?['success','sharing-denial']:[options.selection];
  const workerCapMs=deadline-Date.now();assert(workerCapMs>150000);
  const plan={schemaVersion:1,kind:'ORIGINAL_OWNED_RETURN_REAL_STORE',repo,run:binding.run,binding:pin(path.join(binding.run,'REAL-STORE-SOURCES.json')),profile:options.profile,atUtc:new Date().toISOString(),deadlineEpochMs:deadline,workerCapMs,sourceInputs,toolArtifacts,toolchain:pin(toolchainFile),cmd:cmd.path,parameters,ordinaryDefaultExpanded:false,logicalOnAccepted:false,physicalReaderAccepted:false};
  const planFile=path.join(binding.run,'EXECUTION-PLAN.json');save(planFile,plan);
  const identity=spawnSync(powershell.path,['-NoLogo','-NoProfile','-NonInteractive','-Command','([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'],{encoding:'utf8',windowsHide:true,timeout:5000});assert.equal(identity.status,0,'REAL_STORE_OWNER_IDENTITY_REQUIRED');
  const temp=path.join(binding.run,'private-env');fs.mkdirSync(temp);
  const windows=path.resolve(toolchain.windowsRoot),env={SystemRoot:windows,WINDIR:windows,COMSPEC:cmd.path,PATH:[path.dirname(node.path),path.dirname(powershell.path),path.join(windows,'System32'),windows,path.join(windows,'System32/Wbem')].join(';'),PATHEXT:'.COM;.EXE;.BAT;.CMD',TEMP:temp,TMP:temp,USERPROFILE:temp,APPDATA:temp,LOCALAPPDATA:temp,VSLANG:'1033',VSCMD_SKIP_SENDTELEMETRY:'1'};
  for(const name of ['ProgramFiles','ProgramFiles(x86)','ProgramW6432','CommonProgramFiles','CommonProgramFiles(x86)','CommonProgramW6432','ProgramData','ALLUSERSPROFILE','SystemDrive','OS','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS'])if(typeof process.env[name]==='string')env[name]=process.env[name];
  const launcher=rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:powershell.path});for(const row of launcher.files){const actual=pin(row),declared=toolArtifacts.find(tool=>tool.path.toLowerCase()===actual.path.toLowerCase());assert(declared&&declared.sha256===actual.sha256,'REAL_STORE_LAUNCHER_TOOL_REQUIRED');}
  const allocated=Date.now(),remaining=deadline-allocated;assert(remaining>150000);
  const producer=startOwnedRawProducer({id:'original-msi-owned-return-real-store',program:node.path,args:[fileURLToPath(import.meta.url),'--worker-plan',planFile],cwd:repo,env,host:path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),powershell:powershell.path,ownerStartTicks:identity.stdout.trim(),directory:path.join(binding.run,'owned'),stdoutPath:path.join(binding.run,'producer.stdout.raw'),stderrPath:path.join(binding.run,'producer.stderr.raw'),allocatedAtEpochMs:allocated,deadlineEpochMs:deadline,parentStartedAtEpochMs:started,parentBudgetMs:options.budgetSeconds*1000,originalBudgetMs:options.budgetSeconds*1000,parentRemainingBeforeMs:remaining,cleanupReserveMs:30000,sourceInputs:[...sourceInputs,pin(planFile)],toolArtifacts,toolCleanupPolicy:{kind:'MSVC_VCTIP_AFTER_ROOT_EXIT',maximumProcesses:1,program:telemetry}});
  const invocation=await producer.wait();save(path.join(binding.run,'INVOCATION.json'),invocation);
  const componentFile=path.join(binding.run,'COMPONENT-RESULT.json'),component=fs.existsSync(componentFile)?JSON.parse(fs.readFileSync(componentFile,'utf8')):null;
  const passed=invocation.exitCode===0&&invocation.error===null&&!invocation.timedOut&&!invocation.outputTruncated&&component?.componentAccepted===true&&component.cases.length===parameters.length;
  const result={status:passed?'SELECTED_REAL_STORE_COMPONENT_PASSED':'FAIL_SELECTED_REAL_STORE_COMPONENT',selected:parameters,profile:options.profile,component:component?pin(componentFile):null,invocation:pin(path.join(binding.run,'INVOCATION.json')),source:plan.binding,wallMs:Date.now()-started,newCompiles:component?.compiler?.build?.exitCode===0?1:0,actualNativeCalls:component?.steps?.filter(row=>row.name==='native').length??0,sourceReady:false,logicalOnAccepted:false,physicalReaderAccepted:false,ordinaryDefaultExpanded:false};
  save(resultFile,result);console.log(JSON.stringify({...result,run:binding.run}));if(!passed)process.exitCode=1;return result;
 }catch(error){const result={status:'FAIL_REAL_STORE_ENTRY',error:error.message,run:binding.run,wallMs:Date.now()-started,logicalOnAccepted:false,physicalReaderAccepted:false};save(resultFile,result);process.exitCode=1;console.error(JSON.stringify(result));return result;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 assert(process.argv.length===4&&process.argv[2]==='--worker-plan','REAL_STORE_INTERNAL_WORKER_ARGUMENTS_REQUIRED');
 const {executeRealStorePlan}=await import('./worker.mjs');await executeRealStorePlan(path.resolve(process.argv[3]));
}
