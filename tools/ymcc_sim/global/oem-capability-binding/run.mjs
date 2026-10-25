import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {generateFixture} from './fixture.mjs';
import {runProcess} from '../runner.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(here,'../../../..');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(value,message)=>{if(!value)throw Error(message);};
export async function runBinding(evidenceRoot,{sourceRoot=repo}={}){
 const root=path.resolve(evidenceRoot);need(!fs.existsSync(root),'FRESH_EVIDENCE_DIRECTORY_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const positive=generateFixture(sourceRoot,path.join(root,'positive'));
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const powershell=process.env.YMCC_SIM_PWSH||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
 const host=path.join(repo,'tools/ymcc_sim/global/host-process.ps1');
 const results=[];
 async function execute(name,dir,{negative=false}={}){
  const toolchain=path.join(dir,'installed-toolchain.cmd');fs.writeFileSync(toolchain,calibrationToolchainPreamble(vcvars));
  for(const file of [dir,toolchain])need(!/["%\r\n]/.test(file),'UNSUPPORTED_BUILD_PATH');
  fs.writeFileSync(path.join(dir,'build.cmd'),'@echo off\r\ncall "'+toolchain+'"\r\nif errorlevel 1 exit /b 1\r\ncl.exe /nologo /EHsc /std:c++17 /utf-8 /W4 /Zi /Od /Fe:"'+dir+'/oem-binding.exe" /Fo:"'+dir+'/oem-binding.obj" /Fd:"'+dir+'/compile.pdb" "'+dir+'/actual-oem-binding.cpp" /link /DEBUG /PDB:"'+dir+'/oem-binding.pdb"\r\nexit /b %errorlevel%\r\n');
  const config=path.join(dir,'config.json'),args=path.join(dir,'arguments.json');write(config,{directory:dir});write(args,[config]);
  const processResult=await runProcess(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',host,'-ScriptPath',path.join(here,'Compile-Original.ps1'),'-ArgumentsPath',args],{cwd:repo,env:process.env,timeoutMs:90000});
  fs.writeFileSync(path.join(dir,'stdout.log'),processResult.stdout);fs.writeFileSync(path.join(dir,'stderr.log'),processResult.stderr);
  need(fs.existsSync(path.join(dir,'compile-result.json')),'COMPILATION_RECEIPT_MISSING:'+name);
  const compile=JSON.parse(fs.readFileSync(path.join(dir,'compile-result.json'),'utf8').replace(/^\uFEFF/,''));need(compile.exitCode===0,'COMPILATION_FAILED_NOT_A_SEMANTIC_NEGATIVE:'+name);
  const output=processResult.stdout+'\n'+processResult.stderr;
  if(negative)need(processResult.exitCode!==0&&!processResult.timedOut&&/^OEM_BINDING_FAIL /m.test(output),'REAL_HEADER_FAULT_NOT_DETECTED:'+name);
  else need(processResult.exitCode===0&&!processResult.timedOut&&/^OEM_BINDING_SUMMARY checks=\d+ selected=\d+ hardwareAccess=false productionSelectors=true productionEvaluator=true\r?$/m.test(output),'ORIGINAL_CONSUMPTION_NOT_COMPLETED');
  const summary=output.match(/^OEM_BINDING_SUMMARY checks=(\d+) selected=(\d+)/m);
  const result={name,status:negative?'EXPECTED_REJECTION':'PASS',exitCode:processResult.exitCode,compileExitCode:compile.exitCode,checks:summary?Number(summary[1]):null,selected:summary?Number(summary[2]):null,assertionFailure:output.match(/^OEM_BINDING_FAIL (.+)$/m)?.[1]??null,hardwareAccess:false,directory:dir};results.push(result);return result;
 }
 const original=await execute('positive',path.join(root,'positive'));
 need(original.selected===positive.cases*positive.families&&original.checks>original.selected,'ACTUAL_SELECTION_COUNT_MISMATCH');
 const mutations=[
  ['canonical-consumer-class','if (model && model->consumerClass != deviceClass)','if (false && model && model->consumerClass != deviceClass)'],
  ['family-rebind','initialized_ && family_ == family && deviceClass_ == deviceClass','initialized_ && deviceClass_ == deviceClass'],
  ['class-rebind','initialized_ && family_ == family && deviceClass_ == deviceClass','initialized_ && family_ == family'],
 ];
 for(const [name,before,after] of mutations){
  const mirror=path.join(root,'mutated-source-'+name);for(const input of positive.inputs){const destination=path.join(mirror,input.path);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(input.copy,destination);}
  const header=path.join(mirror,'capabilities/HcOemInputCapabilityBinding.h'),bytes=fs.readFileSync(header,'utf8');need(bytes.split(before).length===2,'MUTATION_ANCHOR_NOT_UNIQUE:'+name);fs.writeFileSync(header,bytes.replace(before,after));
  const target=path.join(root,'negative-'+name);generateFixture(mirror,target);await execute(name,target,{negative:true});
 }
 const current=positive.inputs.map(input=>({path:input.path,expected:input.sha256,actual:hash(fs.readFileSync(path.join(sourceRoot,input.path)))}));
 const result={schemaVersion:1,status:current.every(row=>row.actual===row.expected)?'PASS':'SOURCE_CHANGED',generatedAt:new Date().toISOString(),scope:'Immutable OEM identity binding prototype with unchanged actual production selectors/key tables/evaluator. Main consumer adoption pending.',registeredProductConsumer:false,mainBindingPresent:positive.mainBindingPresent,gateFragmentSha256:positive.gateFragmentSha256,physicalHardwareValidated:false,checks:original.checks,selections:original.selected,sourceIdentity:current,results};
 write(path.join(root,'result.json'),result);console.log(JSON.stringify({status:result.status,checks:result.checks,selections:result.selections,results:results.map(({name,status,assertionFailure})=>({name,status,assertionFailure})),result:path.join(root,'result.json')}));
 return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 if(process.argv.length!==4||process.argv[2]!=='--evidence-dir')throw Error('--evidence-dir <new absolute directory> required');
 runBinding(process.argv[3]).then(result=>{if(result.status!=='PASS')process.exitCode=2;},error=>{console.error(error);process.exitCode=1;});
}
