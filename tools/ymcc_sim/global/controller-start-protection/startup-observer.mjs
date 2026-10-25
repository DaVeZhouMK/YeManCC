import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
export function runStartupObserver(repo,out){
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
  {name:'old-15s-freeze',parts:early,expected:'late physical source remains admissible inside original 30s request'},
  {name:'renewed-observer-deadline',parts:renewed,expected:'startup observer honors original deadline, never its own renewed 30s'},
  {name:'cancelled-intent-revival',parts:revive,expected:'cancelled original startup cannot be revived'}]){
  const dir=path.join(out,'startup-observer',configuration,row.name);fs.mkdirSync(dir,{recursive:true});
  const cpp=path.join(dir,'observer.cpp'),exe=path.join(dir,'observer.exe');
  fs.writeFileSync(cpp,head.replaceAll('@ROOT@',repo.replaceAll('\\','/'))+row.parts+foot);
  const script=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
   'cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"'+path.join(repo,'deps/json')+'" '+(configuration==='Release'?'/O2 /DNDEBUG':'/Od /RTC1 /Zi')+' "'+cpp+'" /Fe"'+exe+'" /Fo"'+path.join(dir,'observer.obj')+'" /Fd"'+path.join(dir,'observer.pdb')+'"\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(dir,'build.cmd'),script);
  const built=spawnSync('cmd.exe',['/d','/c',path.join(dir,'build.cmd')],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:45000});
  fs.writeFileSync(path.join(dir,'build.log'),(built.stdout??'')+(built.stderr??''));if(built.status!==0)throw Error('STARTUP_COMPILE_FAILED:'+dir);
  const run=spawnSync(exe,[],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:10000});const log=(run.stdout??'')+(run.stderr??'');fs.writeFileSync(path.join(dir,'result.log'),log);
  const ok=row.expected?run.status===1&&log.includes('FAIL: '+row.expected):run.status===0&&log.includes('STARTUP_NATIVE_OBSERVER_PASS checks=58 hardwareOperations=0');
  results.push({configuration,name:row.name,passed:ok,exitCode:run.status,expectedNegative:!!row.expected,expected:row.expected??null,sourceSha256:hash(fs.readFileSync(cpp)),exeSha256:hash(fs.readFileSync(exe))});
 }
 return results;
}