[CmdletBinding()]
param([ValidateSet('EnvironmentCheck','FullAcceptance')][string]$Mode='EnvironmentCheck',[switch]$Worker,[string]$ExpectedSid)
$ErrorActionPreference='Stop'
# A hosted administrator has no usable unelevated Explorer session. Exercise the
# real production launch policy as a real standard user, only on a disposable VM.
if($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or $env:RUNNER_OS -cne 'Windows'){throw 'Disposable GitHub Windows runner required'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if($repo -ine [IO.Path]::GetFullPath($env:GITHUB_WORKSPACE).TrimEnd('\')){throw 'Hosted gate must run from checked-out sources'}
. (Join-Path $PSScriptRoot 'ymcc_sim/global/release-host-environment.ps1')
$core=Get-YmccSimulationCoreRoot
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin=([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$record=Join-Path $core 'Evidence/hosted-standard-user'
[IO.Directory]::CreateDirectory($record)|Out-Null
function Save-Record([string]$Name,$Value){[IO.File]::WriteAllText((Join-Path $record $Name),($Value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
if($Worker){
 if($isAdmin -or -not $ExpectedSid -or $identity.User.Value -cne $ExpectedSid){throw 'Acceptance requires the exact non-administrator worker'}
 Save-Record 'worker-identity.json' @{sid=$identity.User.Value;isAdministrator=$isAdmin;sessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId;mode=$Mode;source=$repo;realInstallationChanged=$false}
 Set-Location -LiteralPath $repo
 $env:PATH=(Join-Path $env:YMCC_RELEASE_NODE_MODULES '.bin')+';'+$env:PATH
 if($Mode -eq 'EnvironmentCheck'){
  & node tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs --mode plan --evidence-dir (Join-Path $core 'Evidence/DeckyParentGlobal-20261023/standard-user-environment')
  if($LASTEXITCODE -ne 0){throw 'Original hosted gate input declaration failed'}
  & node tools/decky_binding_runtime_selftest.mjs (Join-Path $core 'Evidence/standard-user-runtime')
  if($LASTEXITCODE -ne 0){throw 'Original hosted Runtime consumer failed'}
  $source=@"
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {pathToFileURL} from 'node:url';
const repo=process.cwd(),core=process.env.YMCC_SIM_CORE_ROOT,rows=[];
for(const [name,entry,exportName] of [['native','native-launch-worker.mjs','startNativeLaunchWorkerControl'],['appearance','css-appearance-worker.mjs','startCssAppearanceWorkerControl']]){
 const module=await import(pathToFileURL(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery',entry)));
 const callerEvidence=path.join(core,'Evidence','hosted-standard-user',name);fs.mkdirSync(callerEvidence,{recursive:true});
 const worker=module[exportName]({repo,callerEvidence,behavior:'normal',deadlineMs:Date.now()+45000});const result=await worker.finish();
 rows.push({name,status:result.status,resourcesDestroyed:result.resourcesDestroyed,evidence:worker.evidence});
 fs.writeFileSync(path.join(core,'Evidence','hosted-standard-user','OWNER-CONTROLS.json'),JSON.stringify({scope:'Original inert owner controls; full suites not executed',rows},null,2));
 assert.equal(result.status,'CONTROL_PASS',JSON.stringify(result));assert.equal(result.resourcesDestroyed,true);console.log(JSON.stringify(rows.at(-1)));
}
"@
  $script=Join-Path $record 'original-owner-controls.mjs';[IO.File]::WriteAllText($script,$source,[Text.UTF8Encoding]::new($false))
  & node $script
  if($LASTEXITCODE -ne 0){throw 'Original hosted owner controls failed'}
 }else{
  $env:YEMAN_RELEASE_ENVELOPE='full'
  & powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-workspace.ps1 -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FullAcceptance
  if($LASTEXITCODE -ne 0){throw 'Frozen source build failed'}
  foreach($test in @('test:updater','test:updater-manager-resume','test:updater-resume','test:updater-retry','test:updater-rollback','test:fan-host-paths','test:fan-payload')){& pnpm run $test;if($LASTEXITCODE -ne 0){throw ('Release regression failed: '+$test)}}
  & powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-release.ps1 -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -ReleaseEnvelope full -FullAcceptance
  if($LASTEXITCODE -ne 0){throw 'Full package assembly failed'}
  & pnpm run test:updater-install-chain
  if($LASTEXITCODE -ne 0){throw 'Release install-chain regression failed'}
 }
 Save-Record 'worker-result.json' @{status='PASS';mode=$Mode;sid=$identity.User.Value;formalReleaseAccepted=$false}
 exit 0
}
if(-not $isAdmin){throw 'Hosted provisioning requires the runner administrator'}
$tempRoot=[IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\')
if(-not $core.StartsWith($tempRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Owned core must be under runner temporary storage'}
$toolRoot=[IO.Path]::GetDirectoryName($env:YMCC_RELEASE_NODE_MODULES).TrimEnd('\')
if(-not $toolRoot.StartsWith($tempRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Hosted tools must be under runner temporary storage'}
$userName='yc'+[Guid]::NewGuid().ToString('N').Substring(0,12)
$passwordText='aA1!'+[Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$password=ConvertTo-SecureString -String $passwordText -AsPlainText -Force;$passwordText=$null
$account=$null;$child=$null;$childStarted=$false;$grants=[Collections.Generic.List[object]]::new();$exit=1
$profile=Join-Path $core 'Workspace/hosted-standard-user';[IO.Directory]::CreateDirectory($profile)|Out-Null
try{
 $account=New-LocalUser -Name $userName -Password $password -AccountNeverExpires -PasswordNeverExpires -Description 'Disposable YMCC acceptance worker' -ErrorAction Stop
 Add-LocalGroupMember -SID ([Security.Principal.SecurityIdentifier]::new('S-1-5-32-545')) -Member $account
 $paths=@($repo,$core,(Join-Path $env:YEMAN_WORKSPACE_ROOT 'Build'),(Join-Path $env:YEMAN_WORKSPACE_ROOT 'Release'),$toolRoot)
 foreach($scope in $paths|Select-Object -Unique){
  $scope=[IO.Path]::GetFullPath($scope).TrimEnd('\')
  if($scope -eq [IO.Path]::GetPathRoot($scope).TrimEnd('\')){throw 'Refuse to grant a volume root'}
  [IO.Directory]::CreateDirectory($scope)|Out-Null
  $rights=if($scope -ieq $toolRoot){'ReadAndExecute'}else{'Modify'}
  $acl=Get-Acl -LiteralPath $scope;$rule=[Security.AccessControl.FileSystemAccessRule]::new($account.SID,$rights,'ContainerInherit,ObjectInherit','None','Allow')
  $acl.AddAccessRule($rule);Set-Acl -LiteralPath $scope -AclObject $acl;$grants.Add(@{path=$scope;rule=$rule})
 }
 $pwsh=(Get-Command pwsh.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$pwsh;$info.WorkingDirectory=$repo;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $info.UserName=$userName;$info.Domain=$env:COMPUTERNAME;$info.Password=$password;$info.LoadUserProfile=$true
 foreach($arg in @('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Mode',$Mode,'-Worker','-ExpectedSid',$account.SID.Value)){$info.ArgumentList.Add($arg)}
 foreach($entry in @{USERPROFILE=$profile;APPDATA=(Join-Path $profile 'AppData/Roaming');LOCALAPPDATA=(Join-Path $profile 'AppData/Local');TEMP=(Join-Path $profile 'Temp');TMP=(Join-Path $profile 'Temp');DOTNET_CLI_HOME=$profile;NUGET_PACKAGES=(Join-Path $profile '.nuget/packages')}.GetEnumerator()){$info.Environment[$entry.Key]=$entry.Value;[IO.Directory]::CreateDirectory($entry.Value)|Out-Null}
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $child=[Diagnostics.Process]::new();$child.StartInfo=$info;$childStarted=$child.Start();if(-not $childStarted){throw 'Standard-user worker did not start'}
 $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
 $limit=if($Mode -eq 'EnvironmentCheck'){300000}else{2100000};$clock=[Diagnostics.Stopwatch]::StartNew()
 while(-not $child.WaitForExit(1000)){if($clock.ElapsedMilliseconds -gt $limit){$child.Kill($true);[void]$child.WaitForExit(10000);throw 'Hosted worker exceeded its deadline'}}
 $exit=$child.ExitCode
 [IO.File]::WriteAllText((Join-Path $record 'worker.stdout.log'),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $record 'worker.stderr.log'),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
 Write-Output ('HOSTED_STANDARD_USER_EXIT='+$exit)
 if($exit -ne 0){Get-Content -LiteralPath (Join-Path $record 'worker.stderr.log') -Tail 16}
}finally{
 $cleanupErrors=[Collections.Generic.List[string]]::new()
 if($child){try{if($childStarted -and -not $child.HasExited){$child.Kill($true);[void]$child.WaitForExit(10000)}}catch{$cleanupErrors.Add($_.Exception.Message)};$child.Dispose()}
 foreach($grant in $grants){try{$acl=Get-Acl -LiteralPath $grant.path;$acl.RemoveAccessRuleSpecific($grant.rule);Set-Acl -LiteralPath $grant.path -AclObject $acl}catch{$cleanupErrors.Add($_.Exception.Message)}}
 $accountRemoved=$false
 if($account){try{Remove-LocalUser -SID $account.SID -ErrorAction Stop;$accountRemoved=$true}catch{$cleanupErrors.Add($_.Exception.Message)}}
 $password.Dispose()
 Save-Record 'provisioning-cleanup.json' @{accountRemoved=$accountRemoved;grantCount=$grants.Count;workerExit=$exit;cleanupErrors=@($cleanupErrors);realInstallationChanged=$false;scope='Disposable GitHub runner only'}
 if($cleanupErrors.Count){throw ('Hosted cleanup failed: '+($cleanupErrors -join '; '))}
}
if($exit -ne 0){throw ('Hosted standard-user acceptance failed: '+$exit)}