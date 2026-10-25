<# D-only prepared entry. Execute only after integration into the existing global runner
   and a coordinator-owned Host window. The runner's host-process.ps1 gates this
   wrapper inside its kill-on-close Job; this entry neither creates a substitute
   ownership gate nor takes over an unknown consumer. Host argv is exactly --self-test. #>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,
      [Parameter(Mandatory=$true)][string]$ContractPath,
      [Parameter(Mandatory=$true)][string]$NodeExe)
$ErrorActionPreference='Stop'
$code=10;$process=$null;$contract=$null;$outFile=$null;$errFile=$null
$record=[ordered]@{schemaVersion=1;mode='complete-host-selftest';started=$false;blockReason='ENTRY_NOT_READY';arguments=@('--self-test');pid=$null;processExitCode=$null;naturalExit=$false;timedOut=$false;forcedTermination=$false;outputComplete=$false;payloadRoot=$null;executable=$null;tuple=$null;noConsumers=@();payloadBefore=@();payloadAfter=@();guard=@{globalRunner=$false;jobMember=$false;helperSha256=$null};nodeSha256=$null;realBackendRequested=$false;hardwareWritesRequested=$false;realTokenFileRequested=$false;installedApplicationMutated=$false;candidateInstalled=$false}
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Fail([string]$Reason){throw [InvalidOperationException]::new($Reason)}
function NoConsumers([string]$Phase){
 # The same read-only condition used by connect-installed-components; no Stop-Process.
 # Unknown enumeration errors propagate and block. Enumerate once without treating missing names as an error.
 $consumers=@(Get-Process -ErrorAction Stop|Where-Object {$_.ProcessName -in @('YeManFanHost','YeManLightSetter')})
 $item=@{phase=$Phase;observedUtc=[DateTime]::UtcNow.ToString('o');consumerCount=$consumers.Count;status=if($consumers.Count -eq 0){'PASS'}else{'BLOCKED'}}
 $record.noConsumers+=@($item)
 if($consumers.Count -ne 0){Fail 'EXISTING_RUNTIME_CONSUMER_REQUIRES_DEDICATED_WINDOW'}
}
function Assert-ReviewedPayloadManifest($Manifest,$Contract){
 # Production Host payloads use schema2. The independent contract still pins
 # the complete manifest bytes and every file; this only verifies its shape.
 if($Manifest.schemaVersion -ne 2 -or @($Manifest.files).Count -ne (@($Contract.payloadFiles).Count-1)){Fail 'PAYLOAD_MANIFEST_SCHEMA_OR_MEMBERSHIP_INVALID'}
 if(@($Manifest.files.path|Select-Object -Unique).Count -ne @($Manifest.files).Count){Fail 'PAYLOAD_MANIFEST_DUPLICATE_NAME'}
 foreach($file in $Manifest.files){$match=@($Contract.payloadFiles|Where-Object {$_.name -ceq $file.path -and $_.sha256 -ceq $file.sha256});if($match.Count -ne 1 -or $file.path -ceq 'YeManFanHost.payload.json'){Fail 'PAYLOAD_MANIFEST_PIN_DISAGREES'}}
}
function PayloadSnapshot([string]$Payload){
 $rows=@();$entries=@(Get-ChildItem -LiteralPath $Payload -Force -ErrorAction Stop)
 foreach($entry in $entries){
  if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $entry.PSIsContainer){Fail 'FLAT_REVIEWED_PAYLOAD_REQUIRED'}
  $rows+=@([ordered]@{name=$entry.Name;sha256=(Hash $entry.FullName)})
 }
 $expected=@($contract.payloadFiles)
 if($rows.Count -ne $expected.Count){Fail 'PAYLOAD_MEMBERSHIP_CHANGED'}
 foreach($row in $rows){$match=@($expected|Where-Object {$_.name -ceq $row.name -and $_.sha256 -ceq $row.sha256});if($match.Count -ne 1){Fail 'PAYLOAD_FILE_IDENTITY_CHANGED'}}
 return $rows
}
try {
 if($PSVersionTable.PSVersion.Major -lt 7 -or $env:YMCC_SIM_GLOBAL_RUN -cne '1'){Fail 'EXISTING_GLOBAL_OWNED_RUNNER_REQUIRED'}
 $env:NODE_OPTIONS=$null;$env:NODE_PATH=$null
 if(-not [IO.Path]::IsPathFullyQualified($EvidenceDir) -or -not(Test-Path -LiteralPath $EvidenceDir -PathType Container)){Fail 'EXPLICIT_OWN_CASE_DIRECTORY_REQUIRED'}
 $case=(Resolve-Path -LiteralPath $EvidenceDir).ProviderPath
 if((Get-Item -LiteralPath $case).Attributes -band [IO.FileAttributes]::ReparsePoint){Fail 'CASE_REPARSE_POINT_REFUSED'}
 $work=Join-Path $case 'complete-host-selftest'
 if(Test-Path -LiteralPath $work){Fail 'FRESH_SELFTEST_EVIDENCE_REQUIRED'}
 New-Item -ItemType Directory -Path $work|Out-Null
 $outFile=Join-Path $work 'stdout.log';$errFile=Join-Path $work 'stderr.log'
 [IO.File]::WriteAllBytes($outFile,[byte[]]@());[IO.File]::WriteAllBytes($errFile,[byte[]]@())
 if(-not [IO.Path]::IsPathFullyQualified($ContractPath) -or -not [IO.Path]::IsPathFullyQualified($NodeExe)){Fail 'EXPLICIT_CONTRACT_AND_REVIEWED_NODE_REQUIRED'}
 $contract=Get-Content -Raw -Encoding UTF8 -LiteralPath $ContractPath|ConvertFrom-Json
 Copy-Item -LiteralPath $ContractPath -Destination (Join-Path $work 'contract.json')
 if($contract.schemaVersion -ne 1 -or $contract.kind -cne 'ymcc-complete-host-selftest-v1' -or @($contract.hostArguments).Count -ne 1 -or $contract.hostArguments[0] -cne '--self-test'){Fail 'ONLY_COMPLETE_SELFTEST_ARGUMENT_ALLOWED'}
 if($contract.hostTimeoutMs -isnot [long] -and $contract.hostTimeoutMs -isnot [int]){Fail 'HOST_TIMEOUT_INVALID'}
 if($contract.hostTimeoutMs -lt 1000 -or $contract.hostTimeoutMs -gt 180000 -or $contract.maxLogBytes -lt 1024 -or $contract.maxLogBytes -gt 16777216){Fail 'HOST_BUDGET_INVALID'}
 $record.nodeSha256=Hash $NodeExe
 if($record.nodeSha256 -cne $contract.tools.nodeSha256){Fail 'REVIEWED_NODE_IDENTITY_MISMATCH'}
 # Pure data contract validation only; this Node module has no launch path.
 $validator=Join-Path $PSScriptRoot 'validate-contract.mjs'
 & $NodeExe $validator '--contract' $ContractPath
 if($LASTEXITCODE -ne 0){Fail 'DATA_CONTRACT_VALIDATION_FAILED'}
 $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
 $jobHelper=Join-Path $repo 'tools/ymcc_sim/global/host-process.ps1'
 $record.guard.helperSha256=Hash $jobHelper
 if($record.guard.helperSha256 -cne $contract.tools.jobHelperSha256){Fail 'EXISTING_JOB_HELPER_IDENTITY_CHANGED'}
 Add-Type -TypeDefinition @"
using System;using System.ComponentModel;using System.Runtime.InteropServices;
public static class YmccCompleteSelfTestJobProbe {
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool IsProcessInJob(IntPtr process,IntPtr job,out bool answer);
 public static bool CurrentIsMember(IntPtr process){bool member;if(!IsProcessInJob(process,IntPtr.Zero,out member))throw new Win32Exception(Marshal.GetLastWin32Error());return member;}
}
"@
 $record.guard.globalRunner=$true
 $record.guard.jobMember=[YmccCompleteSelfTestJobProbe]::CurrentIsMember([Diagnostics.Process]::GetCurrentProcess().Handle)
 if(-not $record.guard.jobMember){Fail 'EXISTING_GATED_JOB_MEMBERSHIP_REQUIRED'}
 if(-not $env:YEMAN_SIM_PAYLOAD_ROOT -or -not [IO.Path]::IsPathFullyQualified($env:YEMAN_SIM_PAYLOAD_ROOT)){Fail 'EXPLICIT_CURRENT_HOST_PAYLOAD_REQUIRED'}
 $payload=(Resolve-Path -LiteralPath $env:YEMAN_SIM_PAYLOAD_ROOT).ProviderPath
 if($payload -eq $case -or $payload.StartsWith($case+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or $case.StartsWith($payload+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){Fail 'PAYLOAD_AND_EVIDENCE_MUST_BE_SEPARATE'}
 if((Get-Item -LiteralPath $payload).Attributes -band [IO.FileAttributes]::ReparsePoint){Fail 'PAYLOAD_REPARSE_POINT_REFUSED'}
 $record.payloadRoot=$payload;$record.executable=Join-Path $payload 'YeManFanHost.exe'
 NoConsumers 'initial'
 $record.payloadBefore=@(PayloadSnapshot $payload)
 $record.tuple=[ordered]@{hostDllSha256=(Hash (Join-Path $payload 'YeManFanHost.dll'));backendSha256=(Hash (Join-Path $payload 'YeManFanBackend.dll'));apphostSha256=(Hash $record.executable);manifestSha256=(Hash (Join-Path $payload 'YeManFanHost.payload.json'))}
 foreach($key in @('hostDllSha256','backendSha256','apphostSha256','manifestSha256')){if($record.tuple[$key] -cne $contract.expectedTuple.$key){Fail ('PAYLOAD_TUPLE_MISMATCH:'+ $key)}}
 $manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $payload 'YeManFanHost.payload.json')|ConvertFrom-Json
 Assert-ReviewedPayloadManifest $manifest $contract
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$record.executable;$info.WorkingDirectory=$payload
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 [void]$info.ArgumentList.Add('--self-test')
 foreach($name in @('TEMP','TMP','TMPDIR','LOCALAPPDATA','APPDATA','YEMANCC_SELFTEST_LOG_ROOT')){$dir=Join-Path $work ('isolation-'+$name);New-Item -ItemType Directory -Path $dir|Out-Null;$info.Environment[$name]=$dir}
 foreach($name in @($info.Environment.Keys)){if($name -match '^(DOTNET_STARTUP_HOOKS|DOTNET_ADDITIONAL_DEPS|DOTNET_SHARED_STORE|COR_ENABLE_PROFILING|COR_PROFILER|CORECLR_ENABLE_PROFILING|CORECLR_PROFILER|YEMAN_SIM_|YMCC_AC\d)'){$info.Environment.Remove($name)|Out-Null}}
 $info.Environment['YEMAN_SIM_PAYLOAD_ROOT']=$payload
 # Recheck immediately before launch. Unknown enumeration or any active writer blocks.
 NoConsumers 'immediately-before-start'
 $record.payloadBefore=@(PayloadSnapshot $payload)
 $record.startedUtc=[DateTime]::UtcNow.ToString('o')
 $process=[Diagnostics.Process]::Start($info);$record.started=$true;$record.pid=$process.Id;$record.blockReason=$null
 $outStream=[IO.File]::Open($outFile,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 $errStream=[IO.File]::Open($errFile,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try {
  $outTask=$process.StandardOutput.BaseStream.CopyToAsync($outStream);$errTask=$process.StandardError.BaseStream.CopyToAsync($errStream)
  $watch=[Diagnostics.Stopwatch]::StartNew()
  while(-not $process.WaitForExit(100)){
   if($watch.ElapsedMilliseconds -ge $contract.hostTimeoutMs -or $outStream.Length -gt $contract.maxLogBytes -or $errStream.Length -gt $contract.maxLogBytes){
    $record.timedOut=$watch.ElapsedMilliseconds -ge $contract.hostTimeoutMs;$record.forcedTermination=$true;$record.blockReason=if($record.timedOut){'OWNED_HOST_TIMEOUT'}else{'OWNED_HOST_LOG_LIMIT'}
    # Only this exact owned PID is stopped. The outer existing Job reclaims descendants.
    $process.Kill();if(-not $process.WaitForExit(5000)){Fail 'OWNED_HOST_DID_NOT_STOP'};break
   }
  }
  $record.processExitCode=$process.ExitCode;$record.naturalExit=(-not $record.forcedTermination)
  if(-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outTask,$errTask),5000)){Fail 'RAW_OUTPUT_CAPTURE_INCOMPLETE'}
  $record.outputComplete=$true
 } finally {$outStream.Dispose();$errStream.Dispose()}
 $record.finishedUtc=[DateTime]::UtcNow.ToString('o');$record.payloadAfter=@(PayloadSnapshot $payload)
 $record.stdoutSha256=Hash $outFile;$record.stderrSha256=Hash $errFile
 $code=2
} catch {
 $record.blockReason=$_.Exception.Message
 if($record.started){$code=2}else{$code=10}
 if($process -and -not $process.HasExited){$record.forcedTermination=$true;$process.Kill();[void]$process.WaitForExit(5000)}
} finally {
 if($process){$process.Dispose()}
 if($work -and (Test-Path -LiteralPath $work -PathType Container)){
  [IO.File]::WriteAllText((Join-Path $work 'process.json'),($record|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
  if($contract -and (Test-Path -LiteralPath $NodeExe -PathType Leaf) -and $record.nodeSha256 -ceq $contract.tools.nodeSha256){
   & $NodeExe (Join-Path $PSScriptRoot 'report.mjs') '--evidence-dir' $case '--contract' (Join-Path $work 'contract.json')
   $code=$LASTEXITCODE
  }
 }
}
exit $code