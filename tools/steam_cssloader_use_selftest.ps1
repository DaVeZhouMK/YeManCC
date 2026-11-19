param([string]$OutputDirectory='G:\YeManCC-Work\Deliverables\Steam-CSS-OnDemand-20261012\validation\use',[switch]$SharedIdentityOnly,[switch]$ReadDiagnosticsOnly,[string]$NodePath='node.exe',[switch]$CompletionSourceOnly,[ValidateSet('Capture','IdentityWake','Joined')][string]$CompletionCase='Capture')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if($ReadDiagnosticsOnly -and !$SharedIdentityOnly){throw 'ReadDiagnosticsOnly requires SharedIdentityOnly'}
if($CompletionSourceOnly -and $SharedIdentityOnly){throw 'CompletionSourceOnly cannot combine with SharedIdentityOnly'}
if(!$CompletionSourceOnly -and $CompletionCase -ne 'Capture'){throw 'CompletionCase requires CompletionSourceOnly'}
$sharedFlags=''
$consumerArguments='"'+$OutputDirectory+'"'
if($CompletionSourceOnly){
    $sharedFlags='/DYMCC_CSS_COMPLETION_SOURCE_ONLY'
    switch($CompletionCase){
        'Capture' {$completionAction='--completion-source-only';$completionDirectory='completion-source'}
        'IdentityWake' {$completionAction='--identity-wake-only';$completionDirectory='identity-wake'}
        'Joined' {$completionAction='--identity-joint-only';$completionDirectory='identity-joint'}
    }
    $consumerArguments=$completionAction+' "'+(Join-Path $OutputDirectory $completionDirectory)+'"'
}
if($SharedIdentityOnly){
    $private=Join-Path $OutputDirectory 'shared-inputs'
    & $NodePath (Join-Path $PSScriptRoot 'fixtures\steam_appearance_shared_identity.mjs') $root $private
    if($LASTEXITCODE -ne 0){throw 'Shared Steam identity input preparation failed'}
    $sharedFlags='/DYMCC_CSS_SHARED_IDENTITY_ONLY /I"'+(Join-Path $private 'native')+'" /I"'+(Join-Path $private 'deps\json')+'" /I"'+$private+'"'
    $consumerArguments='--shared-identity-only "'+$OutputDirectory+'"'+$(if($ReadDiagnosticsOnly){' --read-diagnostics'}else{''})
}
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM BuildTools may omit Windows SDK registry discovery; use the installed SDK explicitly.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /sourceDependencies "$OutputDirectory\use.dependencies.json" $sharedFlags /I"$root\native" "$root\tools\steam_cssloader_use_selftest.cpp" /Fo"$OutputDirectory\use.obj" /Fe"$OutputDirectory\use.exe" /link advapi32.lib user32.lib iphlpapi.lib ws2_32.lib /VERBOSE:LIB
if errorlevel 1 exit /b 1
"$OutputDirectory\use.exe" $consumerArguments
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'result.log')
if($LASTEXITCODE -ne 0){throw "CSS temporary use test failed: $LASTEXITCODE"}
