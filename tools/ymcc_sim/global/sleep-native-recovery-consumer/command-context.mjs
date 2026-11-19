import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {prepareCalibrationNode,verifyCalibrationNode,calibrationPrefix} from '../sleep-non-user-wake-consumer/compiler-leaf.mjs';
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function prepareCommandContext({root,project,out,tool,mod,env,write,config}){
 const guard=path.join(out,'python-command-guard');fs.mkdirSync(guard);
 fs.copyFileSync(path.join(mod,'command-sitecustomize.py'),path.join(guard,'sitecustomize.py'),fs.constants.COPYFILE_EXCL);env.PYTHONPATH=guard;
 const scripts={};
 for(const [script,stem,batch,state,libraries] of [
  ['sleep_wake_resume_selftest.py','sleep_wake_resume_native_selftest','run-wake-recovery-selftest.cmd','owned-fixture-state','advapi32.lib crypt32.lib'],
  ['sleep_recovery_api_selftest.py','sleep_recovery_api_selftest','run-sleep-recovery-api.cmd','owned-api-fixture-state','advapi32.lib ole32.lib shell32.lib crypt32.lib']]){
  const calibrationOut=path.join(out,script+'.calibration');fs.mkdirSync(calibrationOut);
  const calibration=prepareCalibrationNode({root,project,out:calibrationOut,tool,env,write});
  const object=script==='sleep_wake_resume_selftest.py'?'sleep_wake_resume_selftest':stem;
  scripts[script]={...calibration,batch:['cmd.exe','/d','/c',path.join(out,batch)],lines:[...calibrationPrefix(tool),...(script==='sleep_wake_resume_selftest.py'?[`where cl.exe > "${path.join(out,'resolved-cl.txt')}"`,'if errorlevel 1 exit /b 1']:[]),`cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "${path.join(out,stem+'.cpp')}" /I"${path.join(project,'native')}" /I"${path.join(project,'deps/json')}" /Fo"${path.join(out,object+'.obj')}" /Fe"${path.join(out,stem+'.exe')}" /link ${libraries}`,'if errorlevel 1 exit /b 1',`"${path.join(out,stem+'.exe')}" "${path.join(out,state)}"`]};
 }
 write(config,{root:out,project,python:tool.pythonExecutable,vcvars64:path.resolve(tool.vcvars64),sharedPythonGuard:path.join(mod,'sitecustomize.py'),sharedPythonGuardSha256:hash(path.join(mod,'sitecustomize.py')),scripts});
 env.YMCC_SLEEP_PY_CONFIG=config;
}
export function verifyCommandContext(config,audit){
 const selected=config.scripts[audit.currentOriginalScript];
 if(!selected)throw Error('S4_COMMAND_SCRIPT_NOT_REVIEWED');
 return verifyCalibrationNode(selected,audit);
}
