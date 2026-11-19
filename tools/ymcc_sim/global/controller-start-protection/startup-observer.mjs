import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
import {artifacts,compilerEnvironment} from './adapter.mjs';

// Share setup only inside one producer invocation. Each variant still builds its own EXE.
export function prepareStartupCompiler(out){
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const cmd=path.resolve(process.env.SystemRoot||'C:/Windows','System32/cmd.exe');
 const recipe=path.join(out,'compiler-environment.cmd');
 if(/["%\r\n]/.test(recipe))throw Error('START_PROTECTION_UNSUPPORTED_SETUP_PATH');
 fs.writeFileSync(recipe,calibrationToolchainPreamble(vcvars)+compilerEnvironment+'set\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
 const start=performance.now();
 const setup=spawnSync(cmd,['/d','/u','/c',recipe],{cwd:out,windowsHide:true,encoding:'utf16le',timeout:45000});
 const setupMs=performance.now()-start;
 // The snapshot may contain inherited local settings. Never persist or print it.
 const evidence={initializations:1,setupMs,exitCode:setup.status,signal:setup.signal,error:setup.error?.code??null};
 fs.writeFileSync(path.join(out,'compiler-environment-process.json'),JSON.stringify(evidence,null,2),{flag:'wx'});
 if(setup.status!==0||setup.error||setup.signal)throw Error('START_PROTECTION_TOOL_SETUP_FAILED');
 const env=Object.create(null),keys=new Set();
 for(const line of setup.stdout.replace(/\r?\n$/,'').split(/\r?\n/)){
  const equals=line.indexOf('=');
  if(equals===0&&/^=[A-Za-z]:=/.test(line))continue; // cmd drive-CWD entries; all compiler paths are absolute.
  if(equals<1)throw Error('START_PROTECTION_TOOL_ENV_ROW_INVALID');
  const name=line.slice(0,equals).toUpperCase();
  if(keys.has(name))throw Error('START_PROTECTION_TOOL_ENV_DUPLICATE:'+name);
  keys.add(name);
  env[name]=line.slice(equals+1);
 }
 if(!env.PATH||!env.INCLUDE||!env.LIB||!env.VCTOOLSINSTALLDIR||!path.isAbsolute(env.VCTOOLSINSTALLDIR)||env.VSLANG!=='1033'||['CL','_CL_','LINK','_LINK_'].some(name=>env[name]!==undefined)){
  throw Error('START_PROTECTION_TOOL_ENV_INVALID');
 }
 const compiler=path.resolve(env.VCTOOLSINSTALLDIR,'bin/Hostx64/x64/cl.exe');
 if(!artifacts().some(p=>path.resolve(p).toLowerCase()===compiler.toLowerCase()))throw Error('START_PROTECTION_COMPILER_NOT_DECLARED');
 const summary={...evidence,compiler,compilerSha256:crypto.createHash('sha256').update(fs.readFileSync(compiler)).digest('hex'),compilerInvocations:0,artifactReuse:0};
 return {summary,build(dir,source,exe,configuration,extra=[]){
  const stem=path.basename(source,'.cpp');
  const args=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/showIncludes',...extra,
   ...(configuration==='Release'?['/O2','/DNDEBUG']:['/Od','/RTC1','/Zi']),source,'/Fe'+exe,'/Fo'+path.join(dir,stem+'.obj'),'/Fd'+path.join(dir,stem+'.pdb')];
  summary.compilerInvocations++;
  const began=performance.now();
  const process=spawnSync(compiler,args,{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:45000});
  const buildMs=performance.now()-began;
  fs.writeFileSync(path.join(dir,'build-command.json'),JSON.stringify({compiler,args,elapsedMs:buildMs,timeoutMs:45000,exitCode:process.status,signal:process.signal,error:process.error?.code??null},null,2),{flag:'wx'});
  fs.writeFileSync(path.join(dir,'build.log'),(process.stdout??'')+(process.stderr??''),{flag:'wx'});
  return {process,buildMs};
 }};
}

export function runStartupObserver(repo,out,compiler){
 const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
 // Reuse fixture data; execute actual extracted production functions with memory IO.
 const probe=fs.readFileSync(path.join(repo,'tools/startup_readiness_native_probe.py'),'utf8');
 const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replaceAll('\r\n','\n');
 const head=/head=r'''([\s\S]*?)'''\.replace/.exec(probe)?.[1],foot=/foot=r'''([\s\S]*?)'''/.exec(probe)?.[1];
 if(!head||!foot)throw Error('STARTUP_OBSERVER_FIXTURE_CHANGED');
 const names=['effectiveRuntimeInputSettings','publishStartupInputState','skipDisabledStartupInputGate','initializeStartupInputGate','recordStartupInputAttempt','observeStartupInputGate'];
 const parts=names.map(name=>{const match=new RegExp('^static (?:void|json) '+name+'\\([^\\n]*\\) \\{\\n[\\s\\S]*?^\\}\\n','m').exec(main);if(!match)throw Error('STARTUP_OBSERVER_FUNCTION_MISSING:'+name);return match[0];}).join('\n');
 const anchor=/const bool admissionExpired = matchingStartupRequest \?[\s\S]*?VirtualStartProtection::maxMs;/;
 const early=parts.replace(anchor,'const bool admissionExpired = GetTickCount64() - g_startupInputSince >= 15000;');
 const renewed=parts.replace(anchor,'const bool admissionExpired = startupNow - g_startupInputSince >= ymcc::VirtualStartProtection::maxMs;');
 const revive=parts.replace('(!startupRequest.locked || (startupRequest.phase != "pending" && startupRequest.phase != "verifying"))','!startupRequest.locked');
 if(early===parts||renewed===parts||revive===parts)throw Error('STARTUP_MUTATION_NOT_APPLIED');
 const results=[];
 for(const configuration of ['Release','Checked'])for(const row of [
  {name:'positive',parts},
  {name:'old-15s-freeze',parts:early,checks:1,expected:'late physical source remains admissible inside original 30s request'},
  {name:'renewed-observer-deadline',parts:renewed,checks:2,expected:'startup observer honors original deadline, never its own renewed 30s'},
  {name:'cancelled-intent-revival',parts:revive,checks:1,expected:'cancelled original startup cannot be revived'}]){
  const dir=path.join(out,'startup-observer',configuration,row.name);fs.mkdirSync(dir,{recursive:true});
  const cpp=path.join(dir,'observer.cpp'),exe=path.join(dir,'observer.exe');
  fs.writeFileSync(cpp,head.replaceAll('@ROOT@',repo.replaceAll('\\','/'))+row.parts+foot);
  const {process:built,buildMs}=compiler.build(dir,cpp,exe,configuration,['/I'+path.join(repo,'deps/json')]);
  if(built.status!==0||built.error||built.signal)throw Error('STARTUP_COMPILE_FAILED:'+dir+':'+(built.error??built.signal??built.status));
  const negative=Boolean(row.expected),args=negative?[row.name]:[];
  const runStart=performance.now();
  const run=spawnSync(exe,args,{cwd:dir,windowsHide:true,encoding:'utf8',timeout:10000});
  const runMs=performance.now()-runStart,stdout=run.stdout??'',stderr=run.stderr??'';
  fs.writeFileSync(path.join(dir,'result.log'),stdout+stderr);
  const outLines=stdout.trimEnd().split(/\r?\n/),errLines=stderr.trimEnd().split(/\r?\n/);
  const ok=!run.error&&!run.signal&&(negative?
    run.status===1&&outLines.length===1&&outLines[0]===`CASE=${row.name}`&&errLines.length===2&&
      errLines[0]==='FAIL: '+row.expected&&errLines[1]===`CHECKS=${row.checks}`:
    run.status===0&&stderr===''&&outLines.length===1&&outLines[0]==='STARTUP_NATIVE_OBSERVER_PASS checks=58 hardwareOperations=0');
  const count=negative?/^CHECKS=(\d+)$/m.exec(stderr):/^STARTUP_NATIVE_OBSERVER_PASS checks=(\d+) hardwareOperations=0$/m.exec(stdout);
  results.push({configuration,name:row.name,passed:ok,exitCode:run.status,expectedNegative:negative,expected:row.expected??null,
    selected:args[0]??null,checks:count?+count[1]:null,failureNames:errLines.filter(x=>x.startsWith('FAIL: ')).map(x=>x.slice(6)),
    buildMs,runMs,sourceSha256:hash(fs.readFileSync(cpp)),exeSha256:hash(fs.readFileSync(exe))});
 }
 return results;
}
