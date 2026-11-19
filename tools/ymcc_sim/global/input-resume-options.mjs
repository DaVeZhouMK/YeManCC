// Selection only: the original resume producer owns every behavioral assertion.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {rawProgramLauncherDeclaration} from './decky-persistent-lifecycle/raw-owned-producer.mjs';

export const INPUT_RESUME_ID='controller.unified-standard-input';
export const INPUT_RESUME_CASE='keep-resubscription';
const nativeToolSource='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json';
const sourceNames=[
 'tools/input_power_gate_r122_selftest.mjs','tools/fixtures/input_power_gate_r122/resume.mjs',
 'tools/fixtures/input_power_gate_r122/resume.cpp','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/lib/hc_controller_input_fixture.mjs',
 'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs',
 nativeToolSource,
 'tools/ymcc_sim/global/host-process.ps1','deps/json/json.hpp',
 'tools/ymcc_sim/global/input-resume-options.mjs','tools/ymcc_sim/global/candidate-options.mjs',
 'tools/ymcc_sim/optimization/cli.mjs','tools/ymcc_sim/global/runner.mjs',
];
export function validateInputResumeOptions(options,unrelatedKeys=[]){
 if(!['inputResumeOnly','inputResumeCase','expectedMainSha256'].some(key=>options[key]!==undefined))return null;
 if(options.inputResumeOnly!==true)throw Error('INPUT_RESUME_ONLY_REQUIRED');
 if(![INPUT_RESUME_CASE,'unknown-delivery'].includes(options.inputResumeCase))throw Error('INPUT_RESUME_EXPLICIT_ORIGINAL_CASE_REQUIRED');
 if(options.tests?.length!==1||options.tests[0]!==INPUT_RESUME_ID||options.modules?.length||options.bugPlan||options.fullAcceptance||options.requireAll||options.capabilities||!['plan','run'].includes(options.mode))throw Error('INPUT_RESUME_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST');
 if(options.profile!==undefined&&!['desktop','full'].includes(options.profile))throw Error('INPUT_RESUME_PROFILE_EXCLUDES_ORIGINAL_TEST');
 if(options.domains?.length&&!options.domains.includes('controller'))throw Error('INPUT_RESUME_DOMAIN_EXCLUDES_ORIGINAL_TEST');
 if([...unrelatedKeys,'commonHidRuntimeOnly','commonHidRuntimeCase','completeHostContract','coordinatorCase','coordinatorToolPlan'].some(key=>options[key]!==undefined))throw Error('INPUT_RESUME_UNRELATED_CANDIDATE_INPUT');
 if(options.expectedMainSha256!==undefined&&(typeof options.expectedMainSha256!=='string'||!/^[a-f0-9]{64}$/.test(options.expectedMainSha256)))throw Error('INPUT_RESUME_EXPECTED_MAIN_SHA256_REQUIRED');
 if(options.MainFile!==undefined&&!options.expectedMainSha256)throw Error('INPUT_RESUME_MAIN_OVERRIDE_REQUIRES_SHA256');
 for(const key of ['MainFile','HeaderRoot'])if(options[key]!==undefined){
  const value=options[key];if(typeof value!=='string'||!path.isAbsolute(value)||/[\0\r\n]/.test(value))throw Error('INPUT_RESUME_ABSOLUTE_INPUT_REQUIRED:'+key);
  const stat=fs.statSync(value);if(key==='MainFile'?!stat.isFile():!stat.isDirectory())throw Error('INPUT_RESUME_INPUT_TYPE:'+key);
 }
 return {caseName:options.inputResumeCase,mainFile:options.MainFile,headerRoot:options.HeaderRoot,expectedMainSha256:options.expectedMainSha256};
}
export function configureInputResumeAdapter(adapter,selection,repoRoot){
 const main=selection.mainFile??path.join(repoRoot,'native/main.cpp'),headerRoot=selection.headerRoot??path.join(repoRoot,'native');
 const mainSha256=crypto.createHash('sha256').update(fs.readFileSync(main)).digest('hex');
 if(selection.expectedMainSha256&&mainSha256!==selection.expectedMainSha256)throw Error('INPUT_RESUME_MAIN_IDENTITY_MISMATCH');
 const sources=[...sourceNames],external=[];
 for(const file of [main,...['sleep_power_dispatch.h','sleep_power_state.h','sleep_wake_admission.h'].map(name=>path.join(headerRoot,name))]){
  if(!fs.statSync(file).isFile())throw Error('INPUT_RESUME_SOURCE_FILE_REQUIRED');
  const relative=path.relative(repoRoot,file);
  if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))external.push(path.resolve(file));else sources.push(relative.replaceAll('\\','/'));
 }
 const native=JSON.parse(fs.readFileSync(path.join(repoRoot,nativeToolSource),'utf8').replace(/^\uFEFF/,''));
 if(!Array.isArray(native.files)||!native.files.length||native.files.some(row=>typeof row.path!=='string'||!path.isAbsolute(row.path)))throw Error('INPUT_RESUME_NATIVE_TOOL_DECLARATION_REQUIRED');
 const powershell=process.env.YMCC_SIM_RAW_PROGRAM_PWSH??'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe';
 const launcher=rawProgramLauncherDeclaration({...process.env,YMCC_SIM_RAW_PROGRAM_PWSH:powershell});
 const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const tools=[process.execPath,process.env.ComSpec??'C:/Windows/System32/cmd.exe',vc,...launcher.files,...native.files.map(row=>row.path)];
 return {...adapter,title:'Original retained-writer input-resume consumer: '+selection.caseName,path:'tools/input_power_gate_r122_selftest.mjs',
  args:['--resume-only','--case',selection.caseName,'--main',main,'--expected-main-sha256',mainSha256,'--header-root',headerRoot,'--out','{outDir}/input-resume'],
  timeoutMs:180000,resultFiles:['input-resume/RESULT.json'],evidenceInputs:[...new Set(sources)],artifactInputs:[...new Set([...external,...tools])],
  originalProjection:{canonicalTestId:adapter.id,caseName:selection.caseName,variant:'input-resume-only',defaultParentDispatched:false,cacheQualified:false,wholeRuntimeAcceptance:false,actualOsOrDevices:false}};
}
