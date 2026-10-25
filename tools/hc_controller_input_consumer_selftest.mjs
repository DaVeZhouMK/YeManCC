// Real main-source classifier + original source/neutral/final-pipe consumers.
// All OS, device and pipe boundaries remain the existing inert fixture boundaries.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {controllerInputTestDefinition} from './ymcc_sim/global/controller-catalog/inputs.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);if(args.length!==2||args[0]!=='--out')throw Error('Usage --out fresh outside evidence directory');
const out=path.resolve(args[1]),relative=path.relative(repo,out);if(!relative||(!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative))||fs.existsSync(out)||/["%\r\n]/.test(out))throw Error('CONTROLLER_INPUT_FRESH_EXTERNAL_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
const sha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex'),inputs=controllerInputTestDefinition(repo),sources=[...new Set([...inputs.sources.map(f=>path.join(repo,f)),...inputs.artifacts])];
const before=sources.map(file=>({path:file,sha256:sha(file)})),positives=[],faults=[];let failure=null;
const call=(exe,argv,dir,log,timeout=120000)=>{const result=spawnSync(exe,argv,{cwd:dir,windowsHide:true,encoding:'utf8',timeout,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(dir,log),(result.stdout??'')+(result.stderr??'')+(result.error?.stack??''));return result;};
const load=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
try {
 const generated=call(process.execPath,[path.join(repo,'tools/hc_capability_catalog_generate.mjs'),'--check','--out',path.join(out,'catalog-check')],out,'catalog-check.log');if(generated.status!==0)throw Error('CONTROLLER_TOTAL_CATALOG_NOT_CURRENT');
 for(const [id,script]of [['frame','input_frame_submission_selftest.mjs'],['pipe','input_frame_pipe_issue_selftest.mjs'],['power','input_power_gate_r122_selftest.mjs'],['msi-read','msi_canonical_submission_r123_selftest.mjs']]) {
  const target=path.join(out,id),execution=call(process.execPath,[path.join(repo,'tools',script),'--out',target],out,id+'.log');
  if(execution.status!==0)throw Error('CONTROLLER_ACTUAL_CONSUMER_FAILED:'+id);
  const report=load(path.join(target,'LATEST.json'));
  const configs=report.results??report.profiles,expected=id==='pipe'?['Release','Checked']:['Release-diagnostic-off','Release-diagnostic-on','Checked-diagnostic-off','Checked-diagnostic-on'];
  if(!configs||(id!=='pipe'&&report.passed!==true)||report.sourceChangedDuringRun?.length||report.sourceChanged?.length||Object.keys(configs).length!==expected.length||expected.some(name=>!configs[name]?.passed||!configs[name].assertions)||(id==='frame'&&Object.values(configs).some(r=>r.cases!==181||r.assertions!==2982)))throw Error('CONTROLLER_ORIGINAL_CONFIGURATIONS_NOT_PROVEN:'+id);
  positives.push({id,status:'PASS',run:report.run,reportSha256:sha(path.join(report.run,'RESULT.json')),configurations:Object.entries(configs).map(([name,r])=>({name,cases:r.cases??r.tests,assertions:r.assertions}))});
  console.log(id+': PASS in '+expected.length+' configurations');
 }
 const baseline=positives[0].run,originalCpp=fs.readFileSync(path.join(baseline,'fixture.cpp'),'utf8'),oldInclude='#include '+JSON.stringify(path.join(baseline,'controller-input/HcControllerInputCapabilityCatalog.h').replaceAll('\\','/'));
 if(originalCpp.split(oldInclude).length!==2)throw Error('CONTROLLER_FIXTURE_PREAMBLE_NOT_UNIQUE');
 const header=fs.readFileSync(path.join(baseline,'controller-input/HcControllerInputCapabilityCatalog.generated.h'),'utf8');
 const spec=[
  ['missing-registration','"controller.input.xinput.standard"','""'],
  ['xinput-mask-bypassed','}, true, true}', '}, false, true}'],
  ['msi-discriminator-lost','InputKind::MsiDInput, {"msi-dinput", ""}','InputKind::XInput, {"msi-dinput", ""}'],
  ['unknown-source-admitted','{"msi-dinput", ""}','{"msi-dinput", "unknown"}'],
  ['msi-mask-required','}, false, true}', '}, true, true}'],
 ];
 for(const [index,[id,marker,replacement]]of spec.entries()){
  const dir=path.join(out,'faults',String(index));fs.mkdirSync(dir,{recursive:true});const changed=header.replace(marker,()=>replacement);if(changed===header)throw Error('CONTROLLER_FAULT_MARKER_MISSING:'+id);
  fs.copyFileSync(path.join(baseline,'controller-input/HcControllerInputCapabilityCatalog.h'),path.join(dir,'HcControllerInputCapabilityCatalog.h'));
  fs.writeFileSync(path.join(dir,'HcControllerInputCapabilityCatalog.generated.h'),changed);
  const alteredInclude='#include '+JSON.stringify(path.join(dir,'HcControllerInputCapabilityCatalog.h').replaceAll('\\','/'));
  fs.writeFileSync(path.join(dir,'fixture.cpp'),originalCpp.replace(oldInclude,()=>alteredInclude));
  const configurations=[];for(const name of ['Release','Checked']){
   const build=path.join(dir,name);fs.mkdirSync(build);const exe=path.join(build,'fixture.exe'),batch=path.join(build,'build.cmd'),vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const q=s=>'"'+s+'"';
   fs.writeFileSync(batch,calibrationToolchainPreamble(vc)+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+(name==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG')+' /I'+q(path.join(baseline,'source'))+' '+q(path.join(dir,'fixture.cpp'))+' /Fo'+q(path.join(build,'fixture.obj'))+' /Fe'+q(exe)+'\r\nexit /b %errorlevel%\r\n');
   const compiled=call('cmd.exe',['/d','/c',batch],build,'compile.log',60000);if(compiled.status!==0)throw Error('CONTROLLER_FAULT_COMPILE_NOT_DETECTION:'+id);
   const executed=call(exe,[],build,'execute.log',30000);let observed;try{observed=JSON.parse(executed.stdout);}catch{throw Error('CONTROLLER_FAULT_NO_SEMANTIC_REPORT:'+id);}
   const detected=executed.status===1&&observed.failures?.some(f=>f.case.startsWith('unified/'))&&observed.cases?.length===181;
   configurations.push({name,detected,exitCode:executed.status,failedAssertions:observed.failures?.length,caseCount:observed.cases?.length,executableSha256:sha(exe)});
  }
  const detected=configurations.every(c=>c.detected);faults.push({id,status:detected?'PASS':'FAIL',actualSemanticDetection:detected,configurations});console.log(id+': '+(detected?'DETECTED':'FAIL'));if(!detected)throw Error('CONTROLLER_BINDING_FAULT_NOT_DETECTED:'+id);
 }
}catch(error){failure=error.message;}
const changes=before.filter(row=>sha(row.path)!==row.sha256),status=!failure&&!changes.length&&positives.length===4&&faults.length===5?'PASS':'FAIL';
function tree(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>entry.isDirectory()?tree(path.join(dir,entry.name)):[path.join(dir,entry.name)]);}
const result={schemaVersion:1,status,positives,faults,failure,sourceInputs:before,sourceChanges:changes,outputEvidence:tree(out).map(file=>({file,sha256:sha(file)})),claims:{actualClassifierAndOriginalConsumers:status==='PASS',deviceIo:false,realHostOrDriver:false,sourceHidingChanged:false,wholeControllerDomainComplete:false,performanceReductionMeasured:false}};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status,positiveConsumers:positives.length,compiledBindingFaults:faults.length,failure,sourceChanges:changes.length}));if(status!=='PASS')process.exitCode=1;
