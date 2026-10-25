param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$ev=[IO.Path]::GetFullPath($EvidenceRoot)
$fixture=Join-Path $ev 'process-fixture'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$source=Get-Content -LiteralPath (Join-Path $ev 'before\Program.cs') -Raw
$a=$source.IndexOf('internal sealed class HostInstanceLease')
$b=$source.IndexOf(' * HC SystemManager changes', $a)
$b=$source.LastIndexOf('/**', $b)
if($a -lt 0 -or $b -lt 0){throw 'preimage lease not found'}
$legacy=$source.Substring($a,$b-$a).Replace('HostInstanceLease','LegacyHostInstanceLease')
Set-Content -LiteralPath (Join-Path $fixture 'LegacyLease.cs') -Value ("using System.Threading;`nnamespace YeManFanHost;`n"+$legacy) -Encoding UTF8
$life=[IO.Path]::GetFullPath((Join-Path $repoRoot 'FanLab\real-host\HostProcessLifetime.cs'))
$successor=[IO.Path]::GetFullPath((Join-Path $repoRoot 'FanLab\real-host\DeferredHostSuccessor.cs'))
$project=@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><RuntimeIdentifier>win-x64</RuntimeIdentifier><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup><Compile Include="$life" Link="HostProcessLifetime.cs"/><Compile Include="$successor" Link="DeferredHostSuccessor.cs"/></ItemGroup></Project>
"@
Set-Content -LiteralPath (Join-Path $fixture 'FanProcessProbe.csproj') -Value $project -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\fixtures\fan_single_host_process.cs') -Destination (Join-Path $fixture 'Program.cs') -Force
& dotnet build (Join-Path $fixture 'FanProcessProbe.csproj') -c Release --nologo | Set-Content (Join-Path $ev 'process-fixture-build.out')
if($LASTEXITCODE -ne 0){throw 'process fixture build failed'}
$bin=Join-Path $fixture 'bin\Release\net10.0-windows\win-x64'
$exe=Join-Path $bin 'YeManFanHost.exe'
Copy-Item -LiteralPath (Join-Path $bin 'FanProcessProbe.exe') -Destination $exe -Force
$children=[Collections.Generic.List[Diagnostics.Process]]::new()
$results=[Collections.Generic.List[object]]::new()
function Start-Probe([string]$Mode,[string[]]$More=@()){
    $out=Join-Path $ev ($Mode+'-'+[guid]::NewGuid().ToString('N')+'.out')
    $err=$out+'.err'
    $p=Start-Process -FilePath $exe -ArgumentList (@($Mode)+$More) -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
    $children.Add($p)
    return [pscustomobject]@{Process=$p;Out=$out;Err=$err}
}
function Wait-Ready($row){
    $due=[datetime]::UtcNow.AddSeconds(6)
    while([datetime]::UtcNow -lt $due){
        if((Test-Path -LiteralPath $row.Out) -and (Get-Content -LiteralPath $row.Out -Raw) -match 'READY'){return}
        if($row.Process.HasExited){throw "fixture exited before ready: $(Get-Content -LiteralPath $row.Err -Raw)"}
        Start-Sleep -Milliseconds 30
    }
    throw 'fixture ready deadline exceeded'
}
function Kill-Exact($row){
    if(!$row.Process.HasExited){$row.Process.Kill(); if(!$row.Process.WaitForExit(4000)){throw 'exact fixture termination failed'}}
}
function Require($condition,[string]$Name){
    if(!$condition){throw "FAIL $Name"}
    $results.Add([pscustomobject]@{name=$Name;result='PASS'})
    Write-Output "PASS $Name"
}
try {
    if(@(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue).Count -ne 0){throw 'real Host present; fixture must not contend with user control'}
    $old=Start-Probe 'legacy-retire'; Wait-Ready $old
    $oldFollower=Start-Probe 'legacy-check'
    Require ($oldFollower.Process.WaitForExit(3000) -and $oldFollower.Process.ExitCode -eq 0 -and !$old.Process.HasExited) 'preimage-reproduces-second-owner-while-first-process-still-alive'
    Kill-Exact $old
    $fixed=Start-Probe 'retire'; Wait-Ready $fixed
    $followers=@(1..6 | ForEach-Object { Start-Probe 'check' })
    foreach($f in $followers){Require ($f.Process.WaitForExit(3000) -and $f.Process.ExitCode -eq 2) 'fixed-retains-owner-after-Main-retired'}
    Require (!$fixed.Process.HasExited) 'fixed-owner-remains-exact-live-process'
    Kill-Exact $fixed
    $retry=Start-Probe 'check'; Require ($retry.Process.WaitForExit(3000) -and $retry.Process.ExitCode -eq 0) 'after-exact-exit-next-owner-can-start'
    foreach($mode in @('deadline','deadline-logblocked','deadline-processexit')){
        $sw=[Diagnostics.Stopwatch]::StartNew(); $row=Start-Probe $mode
        Require ($row.Process.WaitForExit(5000) -and $row.Process.ExitCode -eq 72 -and $sw.ElapsedMilliseconds -lt 4500) "$mode-bounded-exit-and-duplicate-request-does-not-extend"
    }
    $marker=Join-Path $ev 'successor.marker'; $helper=Join-Path $ev 'launcher.marker'
    $deferred=Start-Probe 'deferred' @($marker,$helper); Wait-Ready $deferred
    $overlap=$false
    while(!$deferred.Process.HasExited){
        if(Test-Path -LiteralPath $marker){$overlap=$true}
        Start-Sleep -Milliseconds 20
    }
    Require (!$overlap) 'deferred-successor-never-starts-before-exact-predecessor-exit'
    $due=[datetime]::UtcNow.AddSeconds(8)
    while(!(Test-Path -LiteralPath $marker) -and [datetime]::UtcNow -lt $due){Start-Sleep -Milliseconds 50}
    Require (Test-Path -LiteralPath $marker) 'deferred-successor-really-starts-after-exit'
    $marker2=Join-Path $ev 'no-successor.marker'; $helper2=Join-Path $ev 'timeout-launcher.marker'
    $stuck=Start-Probe 'deferred-stuck' @($marker2,$helper2); Wait-Ready $stuck
    $helperPid=[int](Get-Content -LiteralPath $helper2 -Raw)
    $hp=[Diagnostics.Process]::GetProcessById($helperPid)
    $null=$hp.Handle
    $helperExited=$hp.WaitForExit(6000)
    Write-Output "timeoutHelper exited=$helperExited code=$($hp.ExitCode) marker=$(Test-Path -LiteralPath $marker2)"
    Require ($helperExited -and $hp.ExitCode -eq 74 -and !(Test-Path -LiteralPath $marker2)) 'stuck-predecessor-bounded-launcher-does-not-create-second-host'
    $hp.Dispose(); Kill-Exact $stuck
} finally {
    foreach($p in $children){if(!$p.HasExited){$p.Kill();$null=$p.WaitForExit(4000)};$p.Dispose()}
}
$left=@(Get-CimInstance Win32_Process -Filter "Name='YeManFanHost.exe'" | Where-Object {$_.ExecutablePath -eq $exe})
Require ($left.Count -eq 0) 'zero-residual-fixture-hosts'
$results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $ev 'single-host-process-results.json')
