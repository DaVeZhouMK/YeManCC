[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(!$ComponentRoot){$ComponentRoot=Join-Path $repo 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if($Out -match '["\r\n]'){throw 'Invalid evidence path'}
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$target=Join-Path $Out ('PowerControl-'+[guid]::NewGuid().ToString('N'))
$modules=@('steam-appearance\preset.ps1','steam-appearance\resident.ps1','steam-appearance\idle.ps1')
foreach($relative in @('steam-appearance.ps1')+$modules){
    $destination=Join-Path $target $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $ComponentRoot $relative) -Destination $destination
}
$shell=Join-Path $PSHOME $(if($PSVersionTable.PSEdition -eq 'Desktop'){'powershell.exe'}else{'pwsh.exe'})
$cases=New-Object Collections.Generic.List[object]
function Invoke-OwnedLibrary {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$shell
    # An explicit inert backend path suppresses production listener/state discovery.
    # Library returns before dispatch; no Steam/CSS action is reachable here.
    $info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $target 'steam-appearance.ps1')+'" -Action Library -BackendPath "C:\fixture\CssLoader-Standalone-Headless.exe"'
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    try{
        $output=$process.StandardOutput.ReadToEnd();$errorOutput=$process.StandardError.ReadToEnd()
        if(!$process.WaitForExit(20000)){$process.Kill();throw 'Owned library helper timed out'}
        [pscustomobject]@{exitCode=$process.ExitCode;output=$output;error=$errorOutput}
    }finally{$process.Dispose()}
}
foreach($relative in $modules){
    $file=Join-Path $target $relative;$original=[IO.File]::ReadAllBytes($file)
    foreach($mutation in @('syntax','missing')){
        if($mutation -eq 'syntax'){
            [IO.File]::WriteAllText($file,([Text.Encoding]::UTF8.GetString($original)+[Environment]::NewLine+'function BrokenFieldModule {'),(New-Object Text.UTF8Encoding($true)))
        }else{Remove-Item -LiteralPath $file -Force}
        try{
            $result=Invoke-OwnedLibrary
            [IO.File]::WriteAllText((Join-Path $Out (($relative.Replace('\','_'))+'-'+$mutation+'.stdout.txt')),$result.output)
            [IO.File]::WriteAllText((Join-Path $Out (($relative.Replace('\','_'))+'-'+$mutation+'.stderr.txt')),$result.error)
            try{$json=$result.output|ConvertFrom-Json}catch{throw ('Module failure escaped the JSON contract: '+$relative+' / '+$mutation)}
            if($result.exitCode -ne 1 -or $result.error.Length -ne 0 -or !$json -or $json.ok -isnot [bool] -or $json.ok -or $json.message -isnot [string] -or !$json.message.Contains($relative)){throw ('Module failure response invalid: '+$relative+' / '+$mutation)}
            $cases.Add([pscustomobject]@{module=$relative;mutation=$mutation;exitCode=$result.exitCode;json=$true;stderrEmpty=$true})
        }finally{[IO.File]::WriteAllBytes($file,$original)}
    }
}
$healthy=Invoke-OwnedLibrary
if($healthy.exitCode -ne 0 -or $healthy.output.Trim().Length -or $healthy.error.Length){throw 'Healthy exact library import regressed'}
$cases.Add([pscustomobject]@{module='all';mutation='none';exitCode=$healthy.exitCode;libraryLoaded=$true})
function Get-OwnedSourceHash([string]$Path) {
    # A Core launcher may pass PSModulePath to Windows PowerShell; do not rely on
    # the optional Utility module to attest the exact executed source bytes.
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
    try {([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}
    finally {$stream.Dispose();$sha.Dispose()}
}
$sourceFiles=@(foreach($relative in @('steam-appearance.ps1')+$modules){$p=Join-Path $ComponentRoot $relative;[pscustomobject]@{relative=$relative;sha256=(Get-OwnedSourceHash $p)}})
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=$cases.ToArray();shell=$shell;sourceRoot=$ComponentRoot;sourceFiles=$sourceFiles;realSteamOperations=0;realCssOperations=0;scope='Exact entry and dot-sourced modules in owned copies; child helpers return at Library; synthetic corruption never reaches an action'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) actual appearance module load checks"
