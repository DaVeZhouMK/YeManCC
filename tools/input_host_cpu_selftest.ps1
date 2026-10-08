[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$BaselineAssembly,
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedBaselineSha256,
 [string]$OutDir=''
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$BaselineAssembly=[IO.Path]::GetFullPath($BaselineAssembly)
if(-not(Test-Path -LiteralPath $BaselineAssembly -PathType Leaf)){throw 'EXACT_BASELINE_ASSEMBLY_MISSING'}
if((Get-FileHash -LiteralPath $BaselineAssembly -Algorithm SHA256).Hash -ine $ExpectedBaselineSha256){throw 'BASELINE_SHA256_MISMATCH'}
if(-not $OutDir){$OutDir=Join-Path $repo ('Build\Validation\CPU-InputHost-recheck-'+[datetime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$OutDir=[IO.Path]::GetFullPath($OutDir)
if((Test-Path -LiteralPath $OutDir)-and @(Get-ChildItem -LiteralPath $OutDir -Force).Count){throw 'OUTPUT_DIRECTORY_NOT_EMPTY'}
New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
function Run-Dotnet([string[]]$Arguments){
 & dotnet @Arguments 2>&1|Tee-Object -FilePath (Join-Path $OutDir 'test-output.log') -Append
 if($LASTEXITCODE -ne 0){throw ('DOTNET_COMMAND_FAILED: '+$LASTEXITCODE)}
}
# Builds/tests only. No normal target arguments, hardware driver install,
# app deployment, elevated helpers or production process termination.
Run-Dotnet @('build',(Join-Path $repo 'InputHost\YeManInputHost.csproj'),'-c','Release','--artifacts-path',(Join-Path $OutDir 'host-artifacts'),'--output',(Join-Path $OutDir 'host'),'--nologo')
Run-Dotnet @('build',(Join-Path $repo 'tools\input_host_cpu_selftest\InputHostCpuSelftest.csproj'),'-c','Release','--artifacts-path',(Join-Path $OutDir 'runner-artifacts'),'--output',(Join-Path $OutDir 'runner'),'--nologo')
$candidate=Join-Path $OutDir 'host\YeManInputHost.dll'
Run-Dotnet @((Join-Path $OutDir 'runner\InputHostCpuSelftest.dll'),$BaselineAssembly,$candidate,(Join-Path $OutDir 'cpu-helper-ab-evidence.json'))
foreach($flag in @('--selftest-protocol','--selftest-xbox360-state','--selftest-steamdeck-state','--selftest-ds-wire','--selftest-endpoint-fence','--selftest-backend-advisory')){Run-Dotnet @($candidate,$flag)}
Write-Output ('PASS InputHost helper parity + lifecycle checks + six production selftest entries; output='+$OutDir)