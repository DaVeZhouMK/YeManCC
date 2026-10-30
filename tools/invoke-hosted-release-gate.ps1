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
 # Node is an intermediate process: a PS7 module path can shadow the PS5 Utility
 # module and hide Get-FileHash. Let each descendant shell build its own defaults.
 # Microsoft Learn: about_PSModulePath / Starting Windows PowerShell from PS7.
 $env:PSModulePath=$null
 $env:WinPSModulePath=$null
 $env:PATH=(Join-Path $env:YMCC_RELEASE_NODE_MODULES '.bin')+';'+$env:PATH
 # The checkout belongs to runneradmin. Trust only this exact checkout for this
 # worker process; do not change global Git settings or wildcard safe.directory.
 $configCount=if($env:GIT_CONFIG_COUNT){[int]$env:GIT_CONFIG_COUNT}else{0}
 [Environment]::SetEnvironmentVariable(('GIT_CONFIG_KEY_'+$configCount),'safe.directory','Process')
 [Environment]::SetEnvironmentVariable(('GIT_CONFIG_VALUE_'+$configCount),$repo,'Process')
 $env:GIT_CONFIG_COUNT=[string]($configCount+1)
 $sourceCommit=[string](& git -C $repo rev-parse HEAD)
 if($LASTEXITCODE -ne 0){throw 'Standard-user Git source identity failed'}
 $pnpmVersion=[string](& pnpm.cmd --version)
 if($LASTEXITCODE -ne 0 -or $pnpmVersion.Trim() -cne '11.16.0'){throw 'Standard-user locked pnpm identity failed'}
 Save-Record 'worker-source-tools.json' @{sourceCommit=$sourceCommit.Trim();pnpmVersion=$pnpmVersion.Trim();gitTrustScope=$repo;globalGitSettingsChanged=$false}
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
$account=$null;$child=$null;$childStarted=$false;$grants=[Collections.Generic.List[object]]::new();$lightingOwners=[Collections.Generic.List[object]]::new();$exit=1
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
 # Existing checkout files belong to the runner administrator. Bind only the
 # seven replaceable lighting files to the ordinary worker before it captures
 # full SDDL; ReplaceFile ownership must remain stable throughout its check.
 $lightingRoot=Join-Path $repo 'PowerControl/light-setter'
 foreach($name in @('YeManLightSetter.exe','YeManLightSetter.dll','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json','YeManLightDeviceCore.dll','YeManLightSetter.runtime.json','YeManLightSetter.pdb')){
  $file=Join-Path $lightingRoot $name
  if(!(Test-Path -LiteralPath $file -PathType Leaf)){continue}
  if((Get-Item -LiteralPath $file -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Hosted lighting file is linked'}
  $acl=Get-Acl -LiteralPath $file;$beforeOwner=$acl.GetOwner([Security.Principal.SecurityIdentifier]);$acl.SetOwner($account.SID)
  Set-Acl -LiteralPath $file -AclObject $acl
  $lightingOwners.Add(@{path=$file;owner=$beforeOwner})
  if((Get-Acl -LiteralPath $file).GetOwner([Security.Principal.SecurityIdentifier]).Value -cne $account.SID.Value){throw 'Hosted lighting owner setup failed'}
 }
 Save-Record 'lighting-owner-provisioning.json' @{files=@($lightingOwners|ForEach-Object{$_.path});owner=$account.SID.Value;sourceBytesChanged=$false;scope='Disposable checkout lighting payload only'}
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
 foreach($lightingFile in $lightingOwners){try{$acl=Get-Acl -LiteralPath $lightingFile.path;$acl.SetOwner($lightingFile.owner);Set-Acl -LiteralPath $lightingFile.path -AclObject $acl}catch{$cleanupErrors.Add($_.Exception.Message)}}
 foreach($grant in $grants){try{$acl=Get-Acl -LiteralPath $grant.path;$acl.RemoveAccessRuleSpecific($grant.rule);Set-Acl -LiteralPath $grant.path -AclObject $acl}catch{$cleanupErrors.Add($_.Exception.Message)}}
 $accountRemoved=$false
 if($account){try{Remove-LocalUser -SID $account.SID -ErrorAction Stop;$accountRemoved=$true}catch{$cleanupErrors.Add($_.Exception.Message)}}
 $password.Dispose()
 Save-Record 'provisioning-cleanup.json' @{accountRemoved=$accountRemoved;grantCount=$grants.Count;workerExit=$exit;cleanupErrors=@($cleanupErrors);realInstallationChanged=$false;scope='Disposable GitHub runner only'}
 if($cleanupErrors.Count){throw ('Hosted cleanup failed: '+($cleanupErrors -join '; '))}
}
if($exit -ne 0){throw ('Hosted standard-user acceptance failed: '+$exit)}