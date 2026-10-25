import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {MODULE,REPO,CASE_ID,INPUTS,moduleSources} from './adapter.mjs';
const digest=data=>crypto.createHash('sha256').update(data).digest('hex'),hash=file=>digest(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(condition,code)=>{if(!condition)throw Error(code);};
const inside=(root,target)=>{const rel=path.relative(root,target);return rel===''||(rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel));};
function sourceFile(root,relative){need(typeof relative==='string'&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'TDP_PROTOCOL_SOURCE_PATH_INVALID');const real=fs.realpathSync(path.join(root,relative));need(inside(fs.realpathSync(root),real),'TDP_PROTOCOL_SOURCE_ESCAPE');return real;}
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string'&&path.isAbsolute(args[1])&&!/[\0\r\n]/.test(args[1]),'TDP_PROTOCOL_ARGUMENT_INVALID');const root=path.resolve(args[1]);need(!inside(REPO,root)&&!fs.existsSync(root),'TDP_PROTOCOL_FRESH_EXTERNAL_EVIDENCE_REQUIRED');return{root};}
export function verifyOriginal(raw,spec,{negative=false,processExitCode=0}={}){
 need(raw?.schemaVersion===1&&raw.kind==='tdp-original-python-protocol'&&raw.assertOptimization===0&&raw.isolated===true&&raw.noSite===true&&raw.noBytecodeWrite===true,'TDP_PROTOCOL_ASSERTION_OR_RUNTIME_MODE');
 need(raw.helperMainInvoked===false&&raw.physicalHardwareAccepted===false&&Array.isArray(raw.observation?.forbidden)&&raw.observation.forbidden.length===0,'TDP_PROTOCOL_BOUNDARY_VIOLATION');
 need(Array.isArray(raw.compiledCode)&&raw.compiledCode.length===2&&raw.compiledCode.every(row=>typeof row.path==='string'&&/^[a-f0-9]{64}$/.test(row.sha256)&&/^[a-f0-9]{64}$/.test(row.sourceSha256)),'TDP_PROTOCOL_EXECUTED_CODE_MISSING');
 if(negative)need(processExitCode===1&&raw.exitCode===1&&raw.failure?.type==='AssertionError'&&raw.failure.function===spec.expectedFailedGroup&&String(raw.failure.sourceLine).includes(spec.expectedAssertText),'TDP_PROTOCOL_ORIGINAL_ASSERTION_NOT_CAUGHT');
 else{
  need(processExitCode===0&&raw.exitCode===0&&raw.failure===null&&raw.stdout.endsWith('tdp protocol self-test: PASS\n'),'TDP_PROTOCOL_ORIGINAL_NOT_PASS');
  need(Object.keys(raw.observation.groups).length===spec.groups.length&&spec.groups.every(name=>raw.observation.groups[name]?.calls===1&&raw.observation.groups[name]?.returns===1),'TDP_PROTOCOL_GROUP_NOT_EXECUTED');
 }
 return{groups:Object.keys(raw.observation.groups),assertLineVisits:Object.values(raw.observation.assertLineVisits).reduce((a,b)=>a+b,0),failureDetected:negative?{function:raw.failure.function,line:raw.failure.line,type:raw.failure.type}:null};
}
function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'TDP_PROTOCOL_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else if(entry.isFile())files.push(file);else throw Error('TDP_PROTOCOL_OUTPUT_NOT_FILE');need(files.length<=500,'TDP_PROTOCOL_OUTPUT_LIMIT');}}visit(root);return files.sort();}
export async function runProtocolConsumer(root,{repoRoot=REPO}={}){
 need(path.isAbsolute(root)&&!inside(repoRoot,root)&&!fs.existsSync(root),'TDP_PROTOCOL_FRESH_EXTERNAL_EVIDENCE_REQUIRED');
 const spec=read(path.join(repoRoot,MODULE,'spec.json')),tool=read(path.join(repoRoot,MODULE,'toolchain-inputs.json'));
 need(spec.schemaVersion===1&&tool.schemaVersion===1&&Array.isArray(tool.files)&&tool.files.length<640,'TDP_PROTOCOL_PROFILE_INVALID');
 need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'TDP_PROTOCOL_NODE_IDENTITY');
 for(const file of tool.files)need(hash(file.path)===file.sha256,'TDP_PROTOCOL_TOOLCHAIN_IDENTITY');
 const inputs=[...new Set([...moduleSources(),...INPUTS,'tools/ymcc_sim/global/host-process.ps1'])];
 const sourceHashes=inputs.map(relative=>({path:relative,sha256:hash(sourceFile(repoRoot,relative))}));
 fs.mkdirSync(root,{recursive:true});for(const row of sourceHashes){const target=path.join(root,'input-sources',row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(sourceFile(repoRoot,row.path),target);need(hash(target)===row.sha256,'TDP_PROTOCOL_SNAPSHOT_CHANGED');}
 const processLedger=[];
 try{
  function execute(label,negative){
   const dir=path.join(root,label);fs.mkdirSync(dir);
   for(const relative of INPUTS){const target=path.join(dir,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(root,'input-sources',relative),target);}
   const helper=path.join(dir,INPUTS[1]);if(negative){const text=fs.readFileSync(helper,'utf8');need(text.split(spec.fault.before).length===2,'TDP_PROTOCOL_FAULT_ANCHOR_CHANGED');fs.writeFileSync(helper,text.replace(spec.fault.before,spec.fault.after));}
   const argumentsPath=path.join(dir,'job-arguments.json');write(argumentsPath,['-Python',tool.pythonExecutable,'-Worker',path.join(repoRoot,MODULE,'worker.py'),'-TestScript',path.join(dir,INPUTS[0]),'-EvidenceRoot',dir]);
   const command=[tool.powershellExecutable,'-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(repoRoot,MODULE,'Run-Worker.ps1'),'-ArgumentsPath',argumentsPath];
   const child=spawnSync(command[0],command.slice(1),{cwd:repoRoot,windowsHide:true,timeout:30000,encoding:'utf8',maxBuffer:2*1024*1024});
   fs.writeFileSync(path.join(dir,'stdout.log'),child.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),child.stderr??'');processLedger.push({label,exitCode:child.status,signal:child.signal,error:child.error?.code??null,pid:child.pid,command});
   need(!child.error&&!child.signal&&Number.isInteger(child.status),'TDP_PROTOCOL_PRODUCER_TERMINATION');need(/^YMCC_SIM_OWNED_JOB childPid=\d+/m.test(child.stdout??''),'TDP_PROTOCOL_OWNED_JOB_NOT_OBSERVED');
   const reportPath=path.join(dir,'original-result.json'),raw=read(reportPath),verified=verifyOriginal(raw,spec,{negative,processExitCode:child.status});
   need(Array.isArray(raw.runtimeInputs)&&raw.runtimeInputs.length===tool.runtimeInputs.length,'TDP_PROTOCOL_RUNTIME_SET_CHANGED');const known=new Map(tool.runtimeInputs.map(row=>[row.path,row.sha256]));
   need(raw.runtimeInputs.every(row=>known.get(row.path)===row.sha256),'TDP_PROTOCOL_RUNTIME_BYTES_CHANGED');
   for(const code of raw.compiledCode){need(inside(dir,code.path)&&inside(dir,code.source),'TDP_PROTOCOL_CODE_OUTSIDE_CASE');need(hash(code.path)===code.sha256&&hash(code.source)===code.sourceSha256,'TDP_PROTOCOL_CODE_HASH_MISMATCH');}
   need(hash(path.join(dir,INPUTS[0]))===sourceHashes.find(row=>row.path===INPUTS[0]).sha256,'TDP_PROTOCOL_ORIGINAL_ASSERTIONS_CHANGED');
   return{...verified,reportPath,reportSha256:hash(reportPath),runtimeInputCount:raw.runtimeInputs.length,compiledCode:raw.compiledCode,ownedFixtureJobObserved:true};
  }
  const positive=execute('positive',false),negative=execute('negative-intel-target',true);
  const drift=sourceHashes.filter(row=>hash(sourceFile(repoRoot,row.path))!==row.sha256||hash(path.join(root,'input-sources',row.path))!==row.sha256);need(drift.length===0,'TDP_PROTOCOL_SOURCE_CHANGED_DURING_RUN');
  for(const file of tool.files)need(hash(file.path)===file.sha256,'TDP_PROTOCOL_TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{processLedger});write(path.join(root,'actual-fault.json'),{path:INPUTS[1],before:spec.fault.before,after:spec.fault.after,originalAssertionsUnchanged:true,failureDetected:negative.failureDetected,currentSourceDeliberateFault:true,historicalFailure:false});
  const report={schemaVersion:1,status:'PASS',passed:true,id:CASE_ID,kind:'tdp-original-protocol-consumer',positive,negative,sourceHashes:[...sourceHashes,...walk(root).map(file=>({path:file,sha256:hash(file)}))],sourceChanges:[],executedProductModules:['PowerControl/pawnio/YeManTdpCtl.py:detect_amd_family/amd_set_with/_intel_power_limit_value/_intel_single_target_limits/apply_tdp/_daemon_dispatch_request/_daemon_allowed_client_paths'],inputConsumerStateFeedback:{input:'Original five protocol self-test groups and original fake mailbox/backends',consumer:'Actual frozen product Python helper functions',state:'Original family/mailbox/encoding/targets/client-path assertions',feedback:'Original full PASS or precise original assertion failure'},compatibility:{nativePayloadRuntimeAcceptance:'NOT_EXECUTED',newHcVersionAssumed:false},boundary:{originalTestScriptUnchanged:true,productHelperMain:false,actualDriverCalls:0,actualRegistryCalls:0,networkActions:0,fixtureExecution:'Python interpreter inside existing owned Windows Job',pythonIsolation:'-I -S -B and fresh unused pycache prefix',fullPhysicalPowerAcceptance:false},remainingGaps:['Actual PawnIO/MSR/SMU/WMI transport, device acceptance and physical power are not executed','OEMCPU/OEMGPU total catalog binding remains separate','No installed helper executable or packaged dependency set is accepted here'],fullGoalComplete:false};
  write(path.join(root,'result.json'),report);return report;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',code:error.message,processLedger,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const{root}=parseArgs(process.argv.slice(2)),result=await runProtocolConsumer(root);console.log(JSON.stringify({id:CASE_ID,status:result.status,groups:result.positive.groups.length,negative:result.negative.failureDetected,sourceHashes:result.sourceHashes.length}));}catch(error){console.error(error.stack??error.message);process.exitCode=2;}}
