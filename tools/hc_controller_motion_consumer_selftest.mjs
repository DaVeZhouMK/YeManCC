// Actual existing Legion consumers plus provider/source/bank/final-binding scenarios.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {freeze,verify,extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {activationInputs,runLegionActivationConsumer} from './fixtures/unified_controller_motion/activation.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
const activationOnly=args.length===3&&args[2]==='--activation-only';
if((args.length!==2&&!activationOnly)||args[0]!=='--out')throw Error('Usage: --out fresh outside-source-directory [--activation-only]');
const formalOut=path.resolve(args[1]),rel=path.relative(repo,formalOut);if(!rel||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep)))throw Error('OUTPUT_OUTSIDE_SOURCE_REQUIRED');if(fs.existsSync(formalOut))throw Error('FRESH_OUTPUT_REQUIRED');fs.mkdirSync(formalOut,{recursive:true});
// MSVC includes still encounter MAX_PATH at deeply nested formal evidence paths.
// Keep one invocation-owned workspace in the existing core layer; retain every
// byte and point the formal receipts to it, without changing global TEMP/PATH.
const workspaceBase=path.resolve(repo,'../../../Testing/YMCC/Workspace/ControllerMotion');fs.mkdirSync(workspaceBase,{recursive:true});
if(fs.realpathSync(workspaceBase).toLowerCase()!==workspaceBase.toLowerCase())throw Error('CONTROLLER_MOTION_WORKSPACE_REDIRECTED');
const out=fs.mkdtempSync(path.join(workspaceBase,'r-'));
const workspaceOwner={schemaVersion:1,owner:'controller.unified-motion-providers',pid:process.pid,createdAt:new Date().toISOString(),workspace:out,formalOutput:formalOut};
fs.writeFileSync(path.join(out,'owner.json'),JSON.stringify(workspaceOwner,null,2)+'\n',{flag:'wx'});
fs.writeFileSync(path.join(formalOut,'WORKSPACE.json'),JSON.stringify(workspaceOwner,null,2)+'\n',{flag:'wx'});
const sha=b=>createHash('sha256').update(b).digest('hex'),write=(file,value)=>{const bytes=JSON.stringify(value,null,2)+'\n';fs.writeFileSync(file,bytes);if(file===path.join(out,'result.json'))fs.writeFileSync(path.join(formalOut,'result.json'),bytes);},tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{if(e.isSymbolicLink())throw Error('NO_OUTPUT_LINKS');const f=path.join(dir,e.name);return e.isDirectory()?tree(f):[f];});
const env=(()=>{const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',envFile=path.join(out,'compiler-env.cmd');fs.writeFileSync(envFile,calibrationToolchainPreamble(vcvars)+'set PATH\r\nset INCLUDE\r\nset LIB\r\nexit /b 0\r\n');const envRun=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],{encoding:'utf8',windowsHide:true,timeout:30000});if(envRun.status!==0)throw Error('COMPILER_ENVIRONMENT');const env={...process.env};for(const line of envRun.stdout.split(/\r?\n/)){const i=line.indexOf('=');if(i>0){const key=line.slice(0,i),old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=line.slice(i+1);}}for(const k of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(k.toLowerCase()))delete env[k];;return env;})();
if(activationOnly){
 try{
  const activation=runLegionActivationConsumer({repo,out:path.join(out,'activation'),env});
  fs.mkdirSync(path.join(formalOut,'activation'),{recursive:true});fs.copyFileSync(activation.path,path.join(formalOut,'activation/result.json'));
  write(path.join(out,'result.json'),{schemaVersion:1,status:'PASS',scope:'Lenovo family initialization subcase only; original 37-case/pipe stages are not run by this filter',filter:'activation-only',workspaceOwner,actualHardware:false,activation:{path:activation.path,sha256:activation.sha256,status:activation.status,models:activation.models,runs:activation.runs}});
  console.log('LEGION_ACTIVATION_MODELS_COMPLETED models=4 configurations=2 faults=2');process.exit(0);
 }catch(error){write(path.join(out,'result.json'),{status:'FAIL',filter:'activation-only',error:error.stack,workspaceOwner,actualHardware:false});console.error(error.stack);process.exit(1);}
}
const binding=freeze(repo,out),source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),extra=extract(source,'inputCaptureLegionBankWorkCurrent');
fs.appendFileSync(path.join(out,'production_functions.generated.h'),'\n'+extra.code+'\n');
const sourceRoot=path.join(out,'source'),native=path.join(sourceRoot,'native'),fixture=path.join(sourceRoot,'tools/fixtures/legion_motion_consumer');fs.mkdirSync(native,{recursive:true});fs.mkdirSync(fixture,{recursive:true});
const headers=['touch_haptic_dispatch.h','game_feedback_receipt.h','controller_motion_bank_sessions.h','controller_motion_session.h','controller_motion_processor.h','controller_motion_calibration.h','legion_motion_source.h','legion_motion_bank_evidence.h','legion_motion_message_clock.h','legion_input_protocol.h','input_frame_submission.h','windows_motion_submission.h','virtual_output_status.h'];
const files=[...activationInputs,'tools/input_frame_pipe_issue_selftest.mjs','tools/lib/hc_controller_input_fixture.mjs','capabilities/HcControllerInputCapabilityCatalog.h','capabilities/HcControllerInputCapabilityCatalog.generated.h','native/input_host_pipe_transport.h','tools/fixtures/input_frame_pipe_issue/fixture.cpp','tools/fixtures/input_frame_pipe_issue/windows.h',...headers.map(n=>'native/'+n),'deps/json/json.hpp','tools/fixtures/legion_motion_calibration_consumer/platform_boundary.h',...fs.readdirSync(path.join(repo,'tools/fixtures/legion_motion_consumer')).filter(n=>fs.statSync(path.join(repo,'tools/fixtures/legion_motion_consumer',n)).isFile()).map(n=>'tools/fixtures/legion_motion_consumer/'+n),'tools/fixtures/unified_controller_motion/extra.cpp.inc','tools/hc_controller_motion_consumer_selftest.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/lib/hc_controller_motion_catalog.mjs','capabilities/hc-controller-motion-policy.json','capabilities/hc-device-catalog.json','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const inputs=[...new Set(files)].map(file=>{const p=path.join(repo,file),bytes=fs.readFileSync(p),copy=path.join(sourceRoot,file);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:file,sha256:sha(bytes)};});
let harness=fs.readFileSync(path.join(fixture,'harness.cpp'),'utf8');const extraText=fs.readFileSync(path.join(sourceRoot,'tools/fixtures/unified_controller_motion/extra.cpp.inc'),'utf8');
const extension=[['unified-provider-protocol','unifiedProviderProtocol'],['unified-source-lease','unifiedSourceLease'],['unified-final-guards','unifiedFinalGuards'],['unified-bank-evidence','unifiedBankEvidence'],['unified-background-guard','unifiedBackgroundGuard'],['unified-model-cache-cost','unifiedModelCacheCost'],['unified-cold-calibration','unifiedColdCalibration']];
if(harness.split('struct Scenario {').length!==2)throw Error('ORIGINAL_SCENARIO_MARKER');harness='#include <chrono>\n#include "touch_haptic_dispatch.h"\n#include "game_feedback_receipt.h"\n#include "legion_motion_bank_evidence.h"\n'+harness.replace('struct Scenario {',()=>extraText+'\nstruct Scenario {');harness=harness.replace('static const Scenario scenarios[] = {',()=> 'static const Scenario scenarios[] = {\n'+extension.map(([n,f])=>'    {"'+n+'",'+f+'},').join('\n'));
harness=harness.replace('#include "../legion_motion_calibration_consumer/platform_boundary.h"','#include '+JSON.stringify(path.join(sourceRoot,'tools/fixtures/legion_motion_calibration_consumer/platform_boundary.h').replaceAll('\\','/')));
fs.writeFileSync(path.join(fixture,'harness.cpp'),harness);fs.writeFileSync(path.join(out,'harness.cpp'),harness);

const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]],results=[];
function execute(name,root,config,flags,witness){
 const target=path.join(root,config);fs.mkdirSync(target,{recursive:true});const exe=path.join(target,'consumer.exe'),artifacts=path.join(target,'artifacts');fs.mkdirSync(artifacts);
 const build=spawnSync('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise',...flags,...(witness?['/wd4505']:[]),'/I'+root,'/I'+fixture,'/I'+native,'/I'+path.join(sourceRoot,'deps/json'),path.join(root,'harness.cpp'),'/Fo'+path.join(target,'consumer.obj'),'/Fd'+path.join(target,'consumer.pdb'),'/Fe'+exe,'/link','/INCREMENTAL:NO'],{cwd:target,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4e6});fs.writeFileSync(path.join(target,'compile.log'),(build.stdout||'')+(build.stderr||'')+(build.error?.message||''));if(build.status!==0)throw Error('BUILD_FAILED_NOT_SEMANTIC:'+name+':'+config);
 const run=spawnSync(exe,[artifacts,path.join(fixture,'family-topology.json')],{cwd:target,env,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:4e6});fs.writeFileSync(path.join(target,'run.log'),(run.stdout||'')+(run.stderr||''));const match=run.stdout?.match(/^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+) failedCases=(\d+)\r?$/m),cases=[...String(run.stdout||'').matchAll(/^CASE ([-a-z0-9]+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({name:m[1],checks:+m[2],failures:+m[3]}));if(!match||cases.length!==+match[1]||cases.reduce((n,c)=>n+c.checks,0)!==+match[2])throw Error('INCOMPLETE_TERMINATION:'+name);
 const accepted=witness?run.status===1&&+match[3]>0&&cases.some(c=>c.name===witness&&c.failures>0):run.status===0&&+match[3]===0&&+match[1]===38;if(!accepted)throw Error('SEMANTIC_ASSERTION_NOT_EXPECTED:'+name);
 const cost=run.stdout.match(/^LOCAL_BINDING_COST calls=(\d+) elapsedNs=(\d+) sourceBytes=(\d+) wholeProcessCpuMeasured=false/m);const row={name,configuration:config,status:witness?'EXPECTED_REJECTION':'PASS',compileExitCode:build.status,exitCode:run.status,cases:+match[1],checks:+match[2],failures:+match[3],witness,cost:cost?{calls:+cost[1],elapsedNs:+cost[2],sourceBytes:+cost[3],meaning:'local stable binding calls only; no before comparison or whole-process saving claim'}:null,executableSha256:sha(fs.readFileSync(exe))};results.push(row);return row;
}
function replaceOnce(text,from,to){if(text.split(from).length!==2)throw Error('FAULT_MARKER:'+from);return text.replace(from,()=>to);}
try{
 for(const [config,flags]of configurations){const row=execute('current',out,config,flags);console.log(config+': PASS '+row.cases+'/'+row.checks);}
 const faults=[
 ['cold-warmup-aborted','functions','inputCaptureControllerCalibrationWaitingForSource(GetTickCount64())) return std::nullopt;','false) return std::nullopt;','unified-cold-calibration'],
 ['protocol','catalog','1, "hc.device.LegionGoTablet", "HandheldCompanion.Devices.LegionGoTablet", "controller.motion.legion-tablet", ProtocolKind::Tablet','1, "hc.device.LegionGoTablet", "HandheldCompanion.Devices.LegionGoTablet", "controller.motion.legion-tablet", ProtocolKind::GoS','unified-provider-protocol'],
 ['source-stamp','functions','static_cast<std::uint16_t>(token)','static_cast<std::uint16_t>(token & 0u)','unified-source-lease'],
 ['lease-token','lease','captured.capabilityToken == currentSource.capabilityToken','true','unified-source-lease'],
 ['final-token','functions','sample.source.capabilityToken==binding.token()','true','unified-final-guards'],
 ['bank-token','banks','a.capabilityToken==b.capabilityToken','true','unified-bank-evidence'],
 ['background-token','functions','sample.source.capabilityToken==selectedSource.capabilityToken','true','unified-background-guard'],
 ['model-cache','binding','initialized_ && deviceClass_ == deviceClass','initialized_','unified-model-cache-cost'],
 ['pid-admission','binding','descriptor_->protocol == ProtocolKind::Tablet ? tabletPid(productId) : goSPid(productId)','tabletPid(productId) || goSPid(productId) || productId!=0','unified-provider-protocol'],
 ];
 const originals={functions:fs.readFileSync(path.join(out,'production_functions.generated.h'),'utf8'),lease:fs.readFileSync(path.join(native,'legion_motion_source.h'),'utf8'),banks:fs.readFileSync(path.join(native,'legion_motion_bank_evidence.h'),'utf8'),binding:fs.readFileSync(path.join(out,'controller-motion/capabilities/HcControllerMotionCapabilityCatalog.h'),'utf8'),catalog:fs.readFileSync(path.join(out,'controller-motion/capabilities/HcControllerMotionCapabilityCatalog.generated.h'),'utf8')};
 for(const [name,type,from,to,witness]of faults){
  const root=path.join(out,'faults',name);fs.mkdirSync(root,{recursive:true});let functions=originals.functions;
  const currentInclude='#include '+JSON.stringify(path.join(out,'controller-motion/capabilities/HcControllerMotionCapabilityCatalog.h').replaceAll('\\','/'));
  for(const f of ['production_types.generated.h','production_scopes.generated.h'])fs.copyFileSync(path.join(out,f),path.join(root,f));
  for(const f of ['HcControllerMotionCapabilityCatalog.h','HcControllerMotionCapabilityCatalog.generated.h','hc-device-catalog.generated.h'])fs.copyFileSync(path.join(out,'controller-motion/capabilities',f),path.join(root,f));
  functions=replaceOnce(functions,currentInclude,'#include '+JSON.stringify(path.join(root,'HcControllerMotionCapabilityCatalog.h').replaceAll('\\','/')));
  let localHarness=harness;const listStart=localHarness.indexOf('static const Scenario scenarios[] = {'),listEnd=localHarness.indexOf('\n};',listStart);const selected=extension.find(([n])=>n===witness);if(listStart<0||listEnd<listStart||!selected)throw Error('FAULT_WITNESS_LIST');localHarness=localHarness.slice(0,listStart)+'static const Scenario scenarios[] = {{"'+selected[0]+'",'+selected[1]+'}};'+localHarness.slice(listEnd+3);
  if(type==='functions')functions=replaceOnce(functions,from,to);
  if(type==='binding'||type==='catalog')fs.writeFileSync(path.join(root,type==='binding'?'HcControllerMotionCapabilityCatalog.h':'HcControllerMotionCapabilityCatalog.generated.h'),replaceOnce(originals[type],from,to));
  if(type==='lease'||type==='banks'){
   // All real headers in this fault use one local copy of the mutated native header.
   for(const file of headers)fs.copyFileSync(path.join(native,file),path.join(root,file));
   fs.writeFileSync(path.join(root,type==='lease'?'legion_motion_source.h':'legion_motion_bank_evidence.h'),replaceOnce(originals[type],from,to));
  }
  localHarness=localHarness.replaceAll('#include "production_functions.generated.h"','#include '+JSON.stringify(path.join(root,'production_functions.generated.h').replaceAll('\\','/')));
  fs.writeFileSync(path.join(root,'harness.cpp'),localHarness);fs.writeFileSync(path.join(root,'production_functions.generated.h'),functions);
  for(const [config,flags]of configurations)execute(name,root,config,flags,witness);
 }
 const pipeRoot=path.join(out,'pipe'),pipe=spawnSync(process.execPath,[path.join(repo,'tools/input_frame_pipe_issue_selftest.mjs'),'--out',pipeRoot],{cwd:repo,env:process.env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4e6});fs.writeFileSync(path.join(out,'pipe.log'),(pipe.stdout||'')+(pipe.stderr||''));
 if(pipe.status!==0)throw Error('ORIGINAL_PIPE_CONSUMER_FAILED');
 // The current fixed pipe template counts38 in-case assertions plus its final targetCreateEntries==0 invariant.
 const pipeRuns=fs.readdirSync(path.join(pipeRoot,'runs'));if(pipeRuns.length!==1)throw Error('ORIGINAL_PIPE_RUN_COUNT');const pipeReportPath=path.join(pipeRoot,'runs',pipeRuns[0],'RESULT.json'),pipeReport=JSON.parse(fs.readFileSync(pipeReportPath));if(pipeReport.sourceChanged.length||Object.keys(pipeReport.profiles).length!==2||['Release','Checked'].some(config=>pipeReport.profiles[config]?.passed!==true||pipeReport.profiles[config].tests!==27||pipeReport.profiles[config].assertions!==39))throw Error('ORIGINAL_PIPE_CONSUMER_INCOMPLETE');
 const activation=runLegionActivationConsumer({repo,out:path.join(out,'activation'),env});
 fs.mkdirSync(path.join(formalOut,'activation'),{recursive:true});fs.copyFileSync(activation.path,path.join(formalOut,'activation/result.json'));
 const stability=verify(repo,binding);if(!stability.selectedMethodsStable||!stability.anchorsStable||extract(fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),extra.symbol).sha256!==extra.sha256)throw Error('PRODUCTION_CHANGED');if(inputs.some(row=>sha(fs.readFileSync(path.join(repo,row.path)))!==row.sha256))throw Error('INPUT_CHANGED');
 const sourceHashes=[...inputs,...binding.motionBinding.inputs.map(row=>({path:row.file,sha256:row.sha256})),{path:'native/main.cpp',sha256:binding.binding.sourceSha256},...tree(out).map(file=>({path:file,sha256:sha(fs.readFileSync(file))}))];
 const result={schemaVersion:1,status:'PASS',workspaceOwner,activation:{path:activation.path,sha256:activation.sha256,status:activation.status,models:activation.models,runs:activation.runs},sourceHashes,sourceStability:stability,actualHardware:false,actualDllLoaded:false,pipeConsumer:{report:pipeReportPath,profiles:pipeReport.profiles,sourceChanged:pipeReport.sourceChanged},originalProtocolUnchanged:sha(fs.readFileSync(path.join(repo,'native/legion_input_protocol.h')))===JSON.parse(fs.readFileSync(path.join(repo,'capabilities/hc-controller-motion-policy.json'))).protocolSourceSha256,results,summary:{models:4,protocols:2,originalCases:31,addedCases:7,configurations:2,checks:results.filter(r=>r.name==='current').map(r=>r.checks),compiledFaults:faults.length,negativeExecutions:faults.length*2},scope:'Actual decoder, binding source factory, lease, selected session, bank evidence and final/background guards; controlled callbacks, identity/clock/IO and no actual Host or device'};write(path.join(out,'result.json'),result);console.log('CONTROLLER_MOTION_BINDINGS_COMPLETED configurations=2 faults=9');console.log('LEGION_ACTIVATION_MODELS_COMPLETED models=4 configurations=2 faults=2');
}catch(error){write(path.join(out,'result.json'),{status:'FAIL',error:error.message,results,actualHardware:false});console.error(error.message);process.exitCode=1;}
