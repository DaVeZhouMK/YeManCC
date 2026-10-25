[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$out=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $out -Force|Out-Null
$headers=@('context','decode','io','windows','join')|ForEach-Object{Join-Path $repo "native\msi_usb_name_origin_$_.h"}
$tests=@('context','decode','io')|ForEach-Object{Join-Path $repo "tools\msi_usb_name_origin_$($_)_selftest.cpp"}
$inputs=@($headers)+@($tests)+@($PSCommandPath)
$before=@($inputs|ForEach-Object{Get-FileHash -LiteralPath $_|Select-Object Path,Hash})
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$results=@()
foreach($kind in @('context','decode','io')){
    $source=Join-Path $repo "tools\msi_usb_name_origin_$($kind)_selftest.cpp"
    $exe=Join-Path $out "$kind-selftest.exe"
    $report=Join-Path $out "$kind-report.json"
    & cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /W4 /WX /permissive- /DUNICODE /D_UNICODE "/I$repo\native" "/I$repo\deps\json" $source "/Fo$out\$kind.obj" "/Fe$exe" *> (Join-Path $out "$kind-build.log")
    if($LASTEXITCODE -ne 0){throw "$kind standalone compile failed; see $kind-build.log"}
    & $exe $report *> (Join-Path $out "$kind-run.log")
    if($LASTEXITCODE -ne 0){throw "$kind standalone pure/fake selftest failed; see $kind-run.log"}
    $results+= [ordered]@{kind=$kind;exitCode=0;binary=(Get-FileHash -LiteralPath $exe|Select-Object Path,Hash);report=$report}
}
foreach($input in $before){if($input.Hash -ne (Get-FileHash -LiteralPath $input.Path).Hash){throw ('Source changed during validation: '+$input.Path)}}
[ordered]@{passed=$true;tests=$results;source=$before;liveDeviceApiCalls=0;mappingOrProcessTests=$false;deployed=$false;scope='Pure descriptor/context logic and fully injected fake IO lifecycle; no hardware, shared-mapping or Steam acceptance'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $out 'verification-summary.json') -Encoding utf8
Write-Output "MSI_USB_NAME_ORIGIN_PURE_FAKE_TESTS_OK $out"
