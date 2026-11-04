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

test('Windows owned job rejects pre-assign child after owner loss', {skip: process.platform !== 'win32'}, async () => {
 const {simulationCorePath}=await import('./release-host-environment.mjs');
 const evidence=path.resolve(simulationCorePath('Evidence'));
 const base=path.resolve(process.env.YMCC_SIM_PREASSIGN_EVIDENCE_DIR??path.join(evidence,'OwnedGuardAdmission'));
 const relative=path.relative(evidence,base);
 assert.ok(relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'Pre-assign evidence must stay in the core Evidence directory');
 fs.mkdirSync(base,{recursive:true});
 assert.equal(fs.realpathSync.native(base).toLowerCase(),base.toLowerCase(),'No redirected evidence root');
 const dir=fs.mkdtempSync(path.join(base,'preassign-'));
 const original=path.join(root,'host-process.ps1'),observed=path.join(dir,'observed-host-process.ps1');
 const handoff=path.join(dir,'started-before-assign.json'),proofFile=path.join(dir,'proof.json');
 const target=path.join(dir,'inert-target.ps1'),marker=path.join(dir,'target-started.txt');
 const argsFile=path.join(dir,'arguments.json'),ownerReceipt=path.join(dir,'killed-owner-job.json'),outerReceipt=path.join(dir,'safety-job.json');
 const quote=value=>"'"+value.replaceAll("'","''")+"'";
 const source=fs.readFileSync(original,'utf8'),anchor='$stderr=$process.StandardError.ReadToEndAsync()';
 assert.equal(source.split(anchor).length,2,'Review the exact Start/Assign pause point');
 const insertion=[
  '# YMCC_TEST_PREASSIGN_PAUSE_BEGIN',
  "$taskProbeStage=[ordered]@{schemaVersion=1;stage='STARTED_BEFORE_ASSIGN';ownerPid=$PID;ownerBirth=$taskGuardOwnerBirth.ToString();childPid=$process.Id;childBirth=$process.StartTime.ToUniversalTime().Ticks.ToString()}",
  `[IO.File]::WriteAllText(${quote(handoff+'.partial')},($taskProbeStage|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))`,
  `[IO.File]::Move(${quote(handoff+'.partial')},${quote(handoff)})`,
  '[Threading.Thread]::Sleep(30000)',
  '# YMCC_TEST_PREASSIGN_PAUSE_END',
 ].join('\r\n');
 const delta='\r\n'+insertion,copy=source.replace(anchor,anchor+delta);
 assert.equal(copy.replace(delta,''),source,'Removing only the observer must restore the exact shared host');
 fs.writeFileSync(path.join(dir,'original-host-process.ps1'),source);
 fs.writeFileSync(observed,copy);fs.writeFileSync(path.join(dir,'observer-delta.txt'),delta);
 fs.writeFileSync(target,`[IO.File]::WriteAllText(${quote(marker)},'TARGET_MUST_NOT_START')\r\n`);
 fs.writeFileSync(argsFile,'[]');
 const driver=path.join(dir,'probe.ps1');
 fs.writeFileSync(driver,String.raw`param([string]$Image,[string]$Owner,[string]$Target,[string]$Arguments,[string]$Handoff,[string]$Proof,[string]$Marker,[string]$OwnerReceipt)
$ErrorActionPreference='Stop'
$taskProbeClock=[Diagnostics.Stopwatch]::StartNew()
$taskProbeOwner=$null;$taskProbeChild=$null;$taskProbeOwnerBirth=''
$taskProbeResult=[ordered]@{schemaVersion=1;status='UNKNOWN';ownerCreatedByThisProbe=$false;ownerHandleBound=$false;childHandleBound=$false;ownerKilled=$false;targetMarkerExists=$false;childExitedBeforeSafetyJobDisposal=$false}
try {
 $taskProbeInfo=[Diagnostics.ProcessStartInfo]::new();$taskProbeInfo.FileName=$Image;$taskProbeInfo.UseShellExecute=$false;$taskProbeInfo.CreateNoWindow=$true
 foreach($taskProbeArg in @('-NoLogo','-NoProfile','-NonInteractive','-File',$Owner,'-ScriptPath',$Target,'-ArgumentsPath',$Arguments,'-DeadlineReceiptPath',$OwnerReceipt)){$taskProbeInfo.ArgumentList.Add($taskProbeArg)}
 $taskProbeOwner=[Diagnostics.Process]::Start($taskProbeInfo)
 $taskProbeOwnerHandle=$taskProbeOwner.Handle;$taskProbeOwnerBirth=$taskProbeOwner.StartTime.ToUniversalTime().Ticks.ToString()
 $taskProbeResult.ownerCreatedByThisProbe=$true;$taskProbeResult.ownerHandleBound=$true
 while(-not [IO.File]::Exists($Handoff)-and -not $taskProbeOwner.HasExited -and $taskProbeClock.ElapsedMilliseconds -lt 4500){[Threading.Thread]::Sleep(10)}
 if(-not [IO.File]::Exists($Handoff)){throw 'UNKNOWN_START_ASSIGN_WINDOW_NOT_REACHED'}
 $taskProbeStage=Get-Content -LiteralPath $Handoff -Raw|ConvertFrom-Json
 if($taskProbeStage.stage -ne 'STARTED_BEFORE_ASSIGN' -or $taskProbeStage.ownerPid -ne $taskProbeOwner.Id -or $taskProbeStage.ownerBirth -ne $taskProbeOwnerBirth){throw 'OWNED_WRAPPER_PID_BIRTH_MISMATCH'}
 $taskProbeChild=[Diagnostics.Process]::GetProcessById([int]$taskProbeStage.childPid)
 $taskProbeChildHandle=$taskProbeChild.Handle
 if($taskProbeChild.StartTime.ToUniversalTime().Ticks.ToString() -ne $taskProbeStage.childBirth -or $taskProbeChild.HasExited -or $taskProbeOwner.HasExited){throw 'LIVE_CHILD_OR_OWNER_IDENTITY_MISMATCH'}
 $taskProbeResult.childHandleBound=$true;$taskProbeResult.stage=$taskProbeStage
 $taskProbeResult.ownerKilledAtMs=$taskProbeClock.Elapsed.TotalMilliseconds
 $taskProbeOwner.Kill();$taskProbeResult.ownerKilled=$true
 $taskProbeResult.ownerExited=$taskProbeOwner.WaitForExit(1500)
 $taskProbeResult.childExitedBeforeSafetyJobDisposal=$taskProbeChild.WaitForExit(4500)
 $taskProbeResult.childObservedAtMs=$taskProbeClock.Elapsed.TotalMilliseconds
 if($taskProbeResult.childExitedBeforeSafetyJobDisposal){$taskProbeResult.childExitCode=$taskProbeChild.ExitCode}
 $taskProbeResult.targetMarkerExists=[IO.File]::Exists($Marker)
 $taskProbeResult.abruptOwnerReceiptExists=[IO.File]::Exists($OwnerReceipt)
 if(-not $taskProbeResult.ownerExited -or -not $taskProbeResult.childExitedBeforeSafetyJobDisposal){throw 'UNKNOWN_CHILD_TERMINAL_OUTSIDE_BOUNDED_PROBE_ORIGINAL_30S_NOT_SHORTENED'}
 if($taskProbeResult.childExitCode -eq 0 -or $taskProbeResult.targetMarkerExists -or $taskProbeResult.abruptOwnerReceiptExists){throw 'PREASSIGN_ADMISSION_OR_OWNER_TERMINAL_INVALID'}
 $taskProbeResult.status='PASS'
} catch {$taskProbeResult.error=$_.Exception.Message}
finally {
 if($taskProbeOwner -and -not $taskProbeOwner.HasExited -and $taskProbeOwner.StartTime.ToUniversalTime().Ticks.ToString() -eq $taskProbeOwnerBirth){$taskProbeOwner.Kill();[void]$taskProbeOwner.WaitForExit(1500)}
 if($taskProbeChild){$taskProbeChild.Dispose()};if($taskProbeOwner){$taskProbeOwner.Dispose()}
 $taskProbeResult.observerProcessHandlesDisposed=$true;$taskProbeResult.elapsedMs=$taskProbeClock.Elapsed.TotalMilliseconds
 $taskProbeResult|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $Proof -Encoding utf8
}
if($taskProbeResult.status -eq 'PASS'){exit 0}else{exit 2}
`);
 const driverArgs=['-Image',powershell,'-Owner',observed,'-Target',target,'-Arguments',argsFile,'-Handoff',handoff,'-Proof',proofFile,'-Marker',marker,'-OwnerReceipt',ownerReceipt];
 const driverArgFile=path.join(dir,'driver-arguments.json');fs.writeFileSync(driverArgFile,JSON.stringify(driverArgs));
 // The unchanged safety Job stays alive until the driver records the child's own exit.
 const command=['-NoProfile','-NonInteractive','-File',original,'-ScriptPath',driver,'-ArgumentsPath',driverArgFile,'-DeadlineEpochMs',String(Date.now()+12500),'-CleanupReserveMs','1000','-DeadlineReceiptPath',outerReceipt];
 const result=await runProcess(powershell,command,{cwd:dir,env:process.env,timeoutMs:16000});
 fs.writeFileSync(path.join(dir,'process.json'),JSON.stringify({...result,command:[powershell,...command]},null,2)+'\n');
 fs.writeFileSync(path.join(dir,'scope.json'),JSON.stringify({kind:'ORIGINAL_PARENT_SINGLE_PAUSE_POINT_OBSERVER',original,observed,sourceRestoredByRemovingOnlyDelta:copy.replace(delta,'')===source,childAdmissionInternalPositionObserved:false,originalChildAdmissionTimeoutMs:30000,fullUnmodifiedParentInjection:false},null,2)+'\n');
 assert.equal(result.exitCode,0,JSON.stringify({dir,...result}));assert.equal(result.timedOut,false);
 const proof=JSON.parse(fs.readFileSync(proofFile,'utf8').replace(/^\uFEFF/,''));
 assert.equal(proof.status,'PASS',JSON.stringify({dir,proof}));assert.equal(proof.ownerCreatedByThisProbe,true);
 assert.equal(proof.ownerHandleBound,true);assert.equal(proof.childHandleBound,true);assert.equal(proof.ownerKilled,true);
 assert.equal(proof.childExitedBeforeSafetyJobDisposal,true);assert.notEqual(proof.childExitCode,0);
 assert.equal(proof.targetMarkerExists,false);assert.equal(fs.existsSync(marker),false);
 assert.equal(proof.abruptOwnerReceiptExists,false);assert.equal(proof.observerProcessHandlesDisposed,true);
 const job=JSON.parse(fs.readFileSync(outerReceipt,'utf8').replace(/^\uFEFF/,''));
 assert.equal(job.jobDisposed,true);assert.equal(job.deadlineExpired,false);assert.equal(job.exitCode,0);
});
