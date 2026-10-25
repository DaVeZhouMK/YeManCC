import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runProcess } from './runner.mjs';
const root = path.dirname(fileURLToPath(import.meta.url));
const powershell = process.env.YMCC_SIM_TEST_PWSH ?? 'pwsh.exe';
function alive(pid) {
 try { process.kill(pid, 0); return true; }
 catch (error) { if(error.code==='ESRCH')return false; throw error; }
}
for (const mode of ['normal-exit', 'runner-timeout']) {
 test(`Windows owned job reclaims descendants on ${mode}`, {skip: process.platform !== 'win32'}, async () => {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'YMCC job fixture 中文 '));
  try {
   const script=path.join(dir,'fixture.ps1'), marker=path.join(dir,'created-processes.json'), argFile=path.join(dir,'arguments.json');
   fs.writeFileSync(script,'\uFEFF'+[
    'param([string]$Marker)',
    "$ErrorActionPreference='Stop'",
    "$grandchild=Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoLogo','-NoProfile','-NonInteractive','-Command','Start-Sleep -Seconds 120') -WindowStyle Hidden -PassThru",
    "@{childPid=$PID;grandchildPid=$grandchild.Id}|ConvertTo-Json|Set-Content -LiteralPath $Marker -Encoding UTF8",
    "Write-Output 'JOB_FIXTURE_REACHED'",
    mode==='runner-timeout'?'while($true){Start-Sleep -Milliseconds 200}':'exit 0',
   ].join('\r\n'));
   fs.writeFileSync(argFile,JSON.stringify(['-Marker',marker]));
   const result=await runProcess(powershell,['-NoProfile','-NonInteractive','-File',path.join(root,'host-process.ps1'),'-ScriptPath',script,'-ArgumentsPath',argFile],{cwd:dir,env:process.env,timeoutMs:mode==='runner-timeout'?6500:12000});
   assert.ok(fs.existsSync(marker),JSON.stringify(result));
   const created=JSON.parse(fs.readFileSync(marker,'utf8').replace(/^\uFEFF/,''));
   if(mode==='runner-timeout')assert.equal(result.timedOut,true);
   else {assert.equal(result.exitCode,0,JSON.stringify(result));assert.match(result.stdout,/JOB_FIXTURE_REACHED/);}
   for(let i=0;i<30&&(alive(created.childPid)||alive(created.grandchildPid));i++)await new Promise(r=>setTimeout(r,100));
   assert.equal(alive(created.childPid),false,'Owned child must have exited');
   assert.equal(alive(created.grandchildPid),false,'Owned descendant must not survive the job');
  } finally { fs.rmSync(dir,{recursive:true,force:true}); }
 });
}