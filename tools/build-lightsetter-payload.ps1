[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[string]$PayloadDestination='')
$ErrorActionPreference='Stop'
$lightingTestExport=($env:YEMAN_TEST_EXPORT -eq '1')
if($PSVersionTable.PSEdition -ne 'Core'){
 $core=Get-Command pwsh.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1
 $lightingCoreArguments=@('-NoProfile','-File',$PSCommandPath,'-ProjectRoot',$ProjectRoot,'-WorkspaceRoot',$WorkspaceRoot)
 if($PayloadDestination){$lightingCoreArguments+=@('-PayloadDestination',$PayloadDestination)}
 & $core.Source @lightingCoreArguments
 if($LASTEXITCODE -ne 0){throw 'Lighting candidate Core build failed'};return
}
. (Join-Path $PSScriptRoot 'lighting-build-contract.ps1')
. (Join-Path $PSScriptRoot 'lighting-candidate-source.ps1')
$project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');$workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
$destination=if($PayloadDestination){[IO.Path]::GetFullPath($PayloadDestination).TrimEnd('\')}else{Join-Path $project 'PowerControl/light-setter'}
$candidate=Resolve-YmccLightingChild $workspace ('Build/Lighting/'+[guid]::NewGuid().ToString('N').Substring(0,12));New-Item -ItemType Directory -Path $candidate|Out-Null
$dotnet=(Get-Command dotnet.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$toolInputs=@(foreach($name in @('build-lightsetter-payload.ps1','lighting-build-contract.ps1','lighting-candidate-source.ps1')){$file=Join-Path $PSScriptRoot $name;$copy=Join-Path $candidate ('tool-sources/tools/'+$name);New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force|Out-Null;Copy-Item -LiteralPath $file -Destination $copy;[pscustomobject]@{path=('tools/'+$name);sha256=(Get-YmccLightingFileHash $file);snapshotPath=$copy.Substring($workspace.Length+1).Replace('\','/')}})
$toolCalls=[Collections.Generic.List[object]]::new();$applied=[Collections.Generic.List[object]]::new();$phase='evaluation'
function Save([string]$file,$object){[IO.File]::WriteAllText($file,($object|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))}
function Relative([string]$file){$full=[IO.Path]::GetFullPath($file);if(-not $full.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_CANDIDATE_OUTPUT_ESCAPE'};return $full.Substring($workspace.Length+1).Replace('\','/')}
function Reference([string]$file){[pscustomobject]@{path=(Relative $file);sha256=(Get-YmccLightingFileHash $file)}}
function Run([string]$name,[string]$exe,[string[]]$arguments){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.WorkingDirectory=$project
 # The approved Core embeds its compiler-host runtime in its portable PDB.
 # Use the exact runtime requested by the pinned SDK for compilation only;
 # helper/test application execution retains its normal runtime resolution.
 if([IO.Path]::GetFileNameWithoutExtension($exe) -ieq 'dotnet' -and $arguments.Count -gt 0 -and $arguments[0] -in @('msbuild','publish')){$info.Environment['DOTNET_ROLL_FORWARD']='Disable'}
 foreach($argument in $arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info;$started=[DateTime]::UtcNow
 try{
  if(-not $process.Start()){throw 'LIGHTING_CANDIDATE_PROCESS_START'};$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timedOut=$false
  while(-not $process.WaitForExit(1000)){if(([DateTime]::UtcNow-$started).TotalSeconds -gt 240){$timedOut=$true;$process.Kill($true);$process.WaitForExit();break}}
  $text=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult();[IO.File]::WriteAllText((Join-Path $candidate ($name+'.stdout.log')),$text,[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $candidate ($name+'.stderr.log')),$errorText,[Text.UTF8Encoding]::new($false))
  $toolCalls.Add([pscustomobject]@{name=$name;compilerRuntimeRollForward=$info.Environment['DOTNET_ROLL_FORWARD'];executable=$exe;executableSha256=(Get-YmccLightingFileHash $exe);arguments=$arguments;pid=$process.Id;exitCode=$process.ExitCode;naturalExit=(-not $timedOut);startedUtc=$started.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o')})
  if($timedOut -or $process.ExitCode -ne 0){throw ('LIGHTING_CANDIDATE_PROCESS_FAILED: '+$name)}
  return $text
 }finally{$process.Dispose()}
}
function Replace([string]$from,[string]$to){$temp=$to+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp';try{Copy-Item -LiteralPath $from -Destination $temp;if(Test-Path -LiteralPath $to -PathType Leaf){[IO.File]::Replace($temp,$to,[Management.Automation.Language.NullString]::Value,$true)}else{[IO.File]::Move($temp,$to)}}finally{if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}}}
function Restore-YmccLightingExactAcl([string]$Path,[string]$Sddl){
 if(-not $Sddl){return}
 $current=(Get-Acl -LiteralPath $Path).Sddl
 if($current -ceq $Sddl){return}
 # ReplaceFile may update SE_DACL_AUTO_INHERITED. Do not restore over any
 # other metadata change, and keep the exact full SDDL admission check.
 $wanted=[Security.AccessControl.RawSecurityDescriptor]::new($Sddl)
 $observed=[Security.AccessControl.RawSecurityDescriptor]::new($current)
 if(([int]$wanted.ControlFlags -bxor [int]$observed.ControlFlags) -ne 0x400){throw ('LIGHTING_CANDIDATE_ACL_CHANGED expected='+$Sddl+' observed='+$current)}
 $observed.SetFlags($wanted.ControlFlags)
 if($observed.GetSddlForm([Security.AccessControl.AccessControlSections]::All) -cne $wanted.GetSddlForm([Security.AccessControl.AccessControlSections]::All)){throw ('LIGHTING_CANDIDATE_ACL_CHANGED expected='+$Sddl+' observed='+$current)}
 if(-not ('YmccLightingBuildAclV1' -as [type])){
  Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class YmccLightingBuildAclV1 { [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool SetFileSecurityW(string path, uint information, byte[] descriptor); }'
 }
 $binary=[byte[]]::new($wanted.BinaryLength);$wanted.GetBinaryForm($binary,0)
 if(-not [YmccLightingBuildAclV1]::SetFileSecurityW($Path,4,$binary)){throw ('LIGHTING_CANDIDATE_ACL_RESTORE_FAILED win32='+[Runtime.InteropServices.Marshal]::GetLastWin32Error())}
 if((Get-Acl -LiteralPath $Path).Sddl -cne $Sddl){throw 'LIGHTING_CANDIDATE_ACL_RESTORE_READBACK'}
}
try{
 $projectFile=Join-Path $project 'FanLab/LightSetter/YeManLightSetter.csproj'
 $evaluationText=Run 'source-evaluation' $dotnet @('msbuild',$projectFile,'-nologo','-getItem:Compile,Content,ProjectReference','-getProperty:DirectoryBuildPropsPath,DirectoryBuildTargetsPath,TargetFramework,UseWPF,NETCoreSdkVersion')
 $evaluation=$evaluationText|ConvertFrom-Json;$inputs=@(Get-YmccLightingBuildSourceIdentity -ProjectRoot $project -Evaluation $evaluation)
 $evaluationFile=Join-Path $candidate 'source-evaluation.json';[IO.File]::WriteAllText($evaluationFile,$evaluationText,[Text.UTF8Encoding]::new($false))
 $coreEvaluationText=Run 'core-source-evaluation' $dotnet @('msbuild',(Join-Path $project 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj'),'-nologo','-getItem:Compile,Content,ProjectReference','-getProperty:DirectoryBuildPropsPath,DirectoryBuildTargetsPath,TargetFramework,UseWPF,NETCoreSdkVersion')
 $coreEvaluation=$coreEvaluationText|ConvertFrom-Json;$coreEvaluationFile=Join-Path $candidate 'core-source-evaluation.json';[IO.File]::WriteAllText($coreEvaluationFile,$coreEvaluationText,[Text.UTF8Encoding]::new($false))
 $inputs+=@(Get-YmccLightingBuildSourceIdentity -ProjectRoot $project -Evaluation $coreEvaluation -ProjectRelative 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj' -RolePrefix 'Core');$inputs=@($inputs|Sort-Object path)
 Save (Join-Path $candidate 'source-inputs.json') $inputs
 $phase='helper-publish';$payload=Join-Path $candidate 'payload'
 Run 'helper-publish' $dotnet @('publish',$projectFile,'-c','Release','--nologo','--artifacts-path',(Join-Path $candidate 'helper-artifacts'),'--output',$payload,'-p:UseSharedCompilation=false','-nodeReuse:false')|Out-Null
 $phase='independent-core-publish';$coreOutput=Join-Path $candidate 'independent-core'
 Run 'core-publish' $dotnet @('publish',(Join-Path $project 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj'),'-c','Release','--nologo','--artifacts-path',(Join-Path $candidate 'core-artifacts'),'--output',$coreOutput,'-p:Platform=AnyCPU','-p:UseSharedCompilation=false','-nodeReuse:false')|Out-Null
 $pinText=Get-Content -LiteralPath (Join-Path $project 'FanLab/LightSetter/LightingAssemblyPins.cs') -Raw -Encoding UTF8;$pin=[regex]::Match($pinText,'StandaloneLightingSha256\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
 if($pin -notmatch '^[a-f0-9]{64}$' -or (Get-YmccLightingFileHash (Join-Path $coreOutput 'YeManLightDeviceCore.dll')) -cne $pin){throw 'LIGHTING_CANDIDATE_CORE_PIN'}
 Copy-Item -LiteralPath (Join-Path $coreOutput 'YeManLightDeviceCore.dll') -Destination (Join-Path $payload 'YeManLightDeviceCore.dll')
 Assert-YmccLightingBuildSources -ProjectRoot $project -Inputs $inputs
 $phase='actual-source-pair';$helper=Join-Path $payload 'YeManLightSetter.dll';$pdb=Join-Path $payload 'YeManLightSetter.pdb';$sourceProofFile=Join-Path $candidate 'source-proof.json'
 $sourceProof=Write-YmccLightingCandidateSourceProof -ProjectRoot $project -WorkspaceRoot $workspace -HelperPath $helper -PdbPath $pdb -SourceInputs $inputs -OutputPath $sourceProofFile
 $helperSha=Get-YmccLightingFileHash $helper
 if($lightingTestExport){
  $runtime=@(foreach($name in Get-YmccLightingRuntimeNames){$file=Join-Path $payload $name;[pscustomobject]@{name=$name;path=(Relative $file);sha256=(Get-YmccLightingFileHash $file);bytes=(Get-Item -LiteralPath $file).Length}})
  $record=[ordered]@{schemaVersion=1;kind='ymcc-lighting-compiled-test-v1';status='COMPILED_TEST_ONLY';createdUtc=[DateTime]::UtcNow.ToString('o');helperSha256=$helperSha;coreSha256=$pin;sourceInputs=$inputs;toolInputs=$toolInputs;sourceProof=(Reference $sourceProofFile);runtimeFiles=$runtime;toolCalls=@($toolCalls);fixturesExecuted=$false;hardwareIo=$false;physicalAcceptance=$false}
 }else{
 $phase='actual-consumer-validation';$globalRoot=Join-Path $candidate 'global'
 $globalText=Run 'global-consumers' (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-File',(Join-Path $project 'tools/ymcc_sim/ymcc_sim.ps1'),'-Domain','global','-Mode','run','-Tests','lighting.signal-consumer','-LightingPayloadRoot',$payload,'-ExpectedHelperSha256',$helperSha,'-RuntimeRoot',(Join-Path $project 'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'),'-SharedRuntimeRoot',(Join-Path $project 'PowerControl/ymcc-shared-runtime'),'-HcProfile','ymcc-device-core-v1','-EvidenceDir',$globalRoot)
 $matches=[regex]::Matches($globalText,'(?m)^RESULT (.+)\r?$');if($matches.Count -ne 1){throw 'LIGHTING_CANDIDATE_GLOBAL_RESULT'}
 $globalFile=$matches[0].Groups[1].Value.Trim();$globalFull=[IO.Path]::GetFullPath($globalFile);if(-not $globalFull.StartsWith($globalRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_CANDIDATE_GLOBAL_ESCAPE'}
 $signalFile=Join-Path (Split-Path -Parent $globalFull) 'cases/lighting.signal-consumer/lighting/result.json'
 Assert-YmccLightingBuildSources -ProjectRoot $project -Inputs $inputs
 $runtime=@(foreach($name in Get-YmccLightingRuntimeNames){$file=Join-Path $payload $name;[pscustomobject]@{name=$name;path=(Relative $file);sha256=(Get-YmccLightingFileHash $file);bytes=(Get-Item -LiteralPath $file).Length}})
 $record=[ordered]@{schemaVersion=1;kind='ymcc-lighting-build-candidate-v1';status='PASS';createdUtc=[DateTime]::UtcNow.ToString('o');helperSha256=$helperSha;coreSha256=$pin;sourceInputs=$inputs;toolInputs=$toolInputs;evaluation=(Reference $evaluationFile);coreEvaluation=(Reference $coreEvaluationFile);sourceProof=(Reference $sourceProofFile);globalResult=(Reference $globalFull);signalResult=(Reference $signalFile);runtimeFiles=$runtime;toolCalls=@($toolCalls);hardwareIo=$false;helperMainInvoked=$false;physicalAcceptance=$false;sourceApproval='This exact new build and actual candidate consumers; fixed7bf historical approval not reused.'}
 }
 $candidateReceipt=Join-Path $candidate 'CANDIDATE-ACCEPTANCE.json';Save $candidateReceipt $record
 if($lightingTestExport){Assert-YmccLightingCompiledTestPayload -ProjectRoot $project -WorkspaceRoot $workspace -CandidateReceiptPath $candidateReceipt -PayloadRoot $payload|Out-Null}else{Assert-YmccLightingBuildCandidate -ProjectRoot $project -WorkspaceRoot $workspace -CandidateReceiptPath $candidateReceipt -PayloadRoot $payload|Out-Null}
 # PROMOTE

 $phase='promotion-preflight'
 $allowed=@(Get-YmccLightingRuntimeNames)+@('YeManLightSetter.pdb')
 $destinationAllowed=$destination.StartsWith((Join-Path $workspace 'Build')+'\',[StringComparison]::OrdinalIgnoreCase) -or $destination -ieq (Join-Path $project 'PowerControl/light-setter')
 if(-not $destinationAllowed){throw 'LIGHTING_CANDIDATE_DESTINATION_SCOPE'}
 if(Test-Path -LiteralPath $destination){
  $existing=@(Get-ChildItem -LiteralPath $destination -Recurse -File|ForEach-Object {$_.FullName.Substring($destination.Length+1).Replace('\','/')})
  if(@($existing|Where-Object {$_ -cnotin $allowed}).Count){throw 'LIGHTING_CANDIDATE_DESTINATION_HAS_UNREVIEWED_FILES'}
 }
 New-Item -ItemType Directory -Path $destination -Force|Out-Null
 $changes=@(foreach($name in $allowed){
  $to=Resolve-YmccLightingChild $destination $name;$from=Join-Path $payload $name;$before=$null;$sddl=$null;$backup=$null
  if(Test-Path -LiteralPath $to -PathType Leaf){$before=Get-YmccLightingFileHash $to;$sddl=(Get-Acl -LiteralPath $to).Sddl;$backup=Join-Path $candidate ('preimage/'+$name);New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force|Out-Null;Copy-Item -LiteralPath $to -Destination $backup;if((Get-YmccLightingFileHash $backup) -cne $before){throw 'LIGHTING_CANDIDATE_BACKUP_DIFFERS'}}
  [pscustomobject]@{name=$name;from=$from;to=$to;before=$before;after=(Get-YmccLightingFileHash $from);sddl=$sddl;backup=$backup}
 })
 $phase='promotion'
 foreach($change in $changes){
  if($change.before){if((Get-YmccLightingFileHash $change.to) -cne $change.before){throw 'LIGHTING_CANDIDATE_PREIMAGE_CHANGED'}}elseif(Test-Path -LiteralPath $change.to){throw 'LIGHTING_CANDIDATE_TARGET_APPEARED'}
  Replace $change.from $change.to;$applied.Add($change)
  if($change.sddl){Restore-YmccLightingExactAcl $change.to $change.sddl}
  if((Get-YmccLightingFileHash $change.to) -cne $change.after -or ($change.sddl -and (Get-Acl -LiteralPath $change.to).Sddl -cne $change.sddl)){throw 'LIGHTING_CANDIDATE_PROMOTION_READBACK'}
 }
 if($lightingTestExport){Assert-YmccLightingCompiledTestPayload -ProjectRoot $project -WorkspaceRoot $workspace -CandidateReceiptPath $candidateReceipt -PayloadRoot $destination|Out-Null}else{Assert-YmccLightingBuildCandidate -ProjectRoot $project -WorkspaceRoot $workspace -CandidateReceiptPath $candidateReceipt -PayloadRoot $destination|Out-Null}
 $marker=Resolve-YmccLightingChild $workspace 'Build/App/LightSetter/CANDIDATE-ACCEPTANCE.json';New-Item -ItemType Directory -Path (Split-Path -Parent $marker) -Force|Out-Null
 Replace $candidateReceipt $marker
 $report=[ordered]@{schemaVersion=1;status='PASS';candidateRoot=$candidate;candidateReceipt=$candidateReceipt;marker=$marker;helperSha256=$helperSha;coreSha256=$pin;sourceInputs=$inputs.Count;runtimeFiles=6;promotedFiles=$changes.Count;destination=$destination;changes=$changes;toolCalls=@($toolCalls);hardwareIo=$false;helperMainInvoked=$false;fullBuildExecuted=$false;fullPackageExecuted=$false}
 Save (Join-Path $candidate 'BUILD-RESULT.json') $report
 Write-Output ('LIGHTING_BUILD_CANDIDATE_OK helper='+$helperSha+' receipt='+$marker)
}catch{
 $errorMessage=$_.Exception.Message;$rollbackErrors=@()
 for($i=$applied.Count-1;$i -ge 0;$i--){$change=$applied[$i];try{if((Get-YmccLightingFileHash $change.to) -cne $change.after){throw 'Rollback target was changed by another writer'};if($change.before){Replace $change.backup $change.to;if($change.sddl){Restore-YmccLightingExactAcl $change.to $change.sddl};if((Get-YmccLightingFileHash $change.to) -cne $change.before -or ($change.sddl -and (Get-Acl -LiteralPath $change.to).Sddl -cne $change.sddl)){throw 'Rollback preimage mismatch'}}else{Remove-Item -LiteralPath $change.to}}catch{$rollbackErrors+=@($_.Exception.Message)}}
 Save (Join-Path $candidate 'BUILD-RESULT.json') ([ordered]@{schemaVersion=1;status='FAIL';phase=$phase;error=$errorMessage;appliedFiles=$applied.Count;rollbackPerformed=($applied.Count -gt 0);rollbackErrors=$rollbackErrors;destination=$destination;toolCalls=@($toolCalls);hardwareIo=$false;helperMainInvoked=$false})
 throw ('LIGHTING_BUILD_CANDIDATE_FAILED phase='+$phase+' evidence='+$candidate+' error='+$errorMessage)
}