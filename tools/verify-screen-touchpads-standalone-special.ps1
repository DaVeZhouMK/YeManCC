[CmdletBinding()]
param([string]$ValidationRoot,[switch]$MenuPopupContinuationOnly,[switch]$PrepareOnly,[string]$MenuPopupContinuationCase="")
$ErrorActionPreference='Stop'if($PSBoundParameters.ContainsKey('MenuPopupContinuationCase') -and (!$MenuPopupContinuationOnly -or $MenuPopupContinuationCase -notin @('opened-pending-one-followup','cancelled-at-second-dispatch'))){throw 'Invalid menu continuation case or missing explicit native continuation selection'}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\Validation\ScreenTouchpads-StandaloneSpecial'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
if($PrepareOnly -and !$MenuPopupContinuationOnly){throw 'PrepareOnly requires explicit menu popup continuation selection'}
if($MenuPopupContinuationOnly){
    # Directed extension of the original screen standalone consumer only.
    # No default JS suite or system/virtual input is executed on this path.
    if(Test-Path -LiteralPath $ValidationRoot){throw 'Fresh menu continuation output directory required'}
    [IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
    $utf8=[Text.UTF8Encoding]::new($false)
    function Write-New([string]$Path,[byte[]]$Bytes){
        $f=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        try{$f.Write($Bytes,0,$Bytes.Length);$f.Flush($true)}finally{$f.Dispose()}
    }
    $runtime=Join-Path $project 'native/steam_menu_game_runtime.h'
    $source=[IO.File]::ReadAllText($runtime)
    $begin=$source.IndexOf('    const auto admitted=[&]{return current()&&host->hostLease->current();};',[StringComparison]::Ordinal)
    $end=if($begin -ge 0){$source.IndexOf("`n}",$begin,[StringComparison]::Ordinal)}else{-1}
    if($begin -lt 0 -or $end -le $begin){throw 'Original native menu final suffix missing'}
    $tail=Join-Path $ValidationRoot 'candidate-tail.inc'
    Write-New $tail ($utf8.GetBytes($source.Substring($begin,$end-$begin)+"`n"))
    $selected=@('native/steam_menu_game_runtime.h','native/steam_menu_popup_continuation.h','tools/screen_touchpads_standalone_special_selftest.cpp','tools/fixtures/steam_menu_native_continuation_cases.inc','tools/verify-screen-touchpads-standalone-special.ps1')
    $pins=@($selected|ForEach-Object{$p=Join-Path $project $_;$b=[IO.File]::ReadAllBytes($p);[ordered]@{path=$p;bytes=$b.Length;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}})
    $prepared=[ordered]@{status='PREPARED_NATIVE_MENU_CONTINUATION_ONLY';sourceReady=$false;compiled=$false;businessExecuted=$false;systemInputInjected=$false;defaultSuiteExecuted=$false;sources=$pins;generatedTail=[ordered]@{path=$tail;bytes=(Get-Item -LiteralPath $tail).Length;sha256=(Get-FileHash -LiteralPath $tail -Algorithm SHA256).Hash.ToLowerInvariant()}}
    Write-New (Join-Path $ValidationRoot 'PREPARED.json') ($utf8.GetBytes(($prepared|ConvertTo-Json -Depth 12)))
    if($PrepareOnly){$prepared|ConvertTo-Json -Depth 12;return}
    $exe=Join-Path $ValidationRoot 'native-menu-continuation.exe';$obj=Join-Path $ValidationRoot 'native-menu-continuation.obj'
    $deps=Join-Path $ValidationRoot 'native-menu-continuation.dependencies.json'
    $cmd=Join-Path $ValidationRoot 'build-native-menu-continuation.cmd'
    $batch=@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DYMCC_STEAM_MENU_POPUP_CONTINUATION_ONLY /DYMCC_MENU_POPUP_CONTINUATION_NATIVE_TAIL /I"$project\native" /I"$project\deps\json" /I"$ValidationRoot" /sourceDependencies "$deps" "$project\tools\screen_touchpads_standalone_special_selftest.cpp" /Fo"$obj" /Fe"$exe" /link /VERBOSE:LIB
"@
    Write-New $cmd ([Text.Encoding]::ASCII.GetBytes($batch))
    & $env:ComSpec /d /c $cmd
    if($LASTEXITCODE -ne 0){throw 'Native menu continuation fixture compile failed'}
    $report=Join-Path $ValidationRoot 'native-menu-continuation.json'
    if($MenuPopupContinuationCase){& $exe $report --menu-popup-continuation --case $MenuPopupContinuationCase}else{& $exe $report --menu-popup-continuation}
    if($LASTEXITCODE -ne 0){throw 'Native menu continuation directed test failed'}
    foreach($p in $pins){if((Get-FileHash -LiteralPath $p.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $p.sha256){throw 'Selected source drift during native continuation validation'}}
    return
}
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
$receiptPath=Join-Path $ValidationRoot 'standalone-special-verification.json'
$files=@('native\main.cpp','native\steam_live_cdp.h','native\steam_live.js','native\steam_live_script.h','src\bridge\screenTouchpads.ts','src\components\ScreenTouchpadsSettings.vue','tools\screen_standalone_special_actions_selftest.mjs','native\screen_touchpads.h','native\screen_standalone_special.h','native\screen_special_actions.h','native\screen_button_overlay.h','native\screen_touchpad_cursor.h','native\screen_control_glyphs.h','tools\screen_touchpads_standalone_special_selftest.cpp','tools\verify-screen-touchpads-standalone-special.ps1')
function Get-Sources {
    @($files | ForEach-Object {$path=Join-Path $project $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})
}
$receipt=[ordered]@{ok=$false;status='running';systemInputInjected=$false;virtualTargetStarted=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=(Get-Sources)}
function Save-Receipt { $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8 }
Save-Receipt
try {
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(!(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 BuildTools are required.'}
    $exe=Join-Path $ValidationRoot 'standalone-special.exe';$obj=Join-Path $ValidationRoot 'standalone-special.obj'
    $source=Join-Path $PSScriptRoot 'screen_touchpads_standalone_special_selftest.cpp'
    $cmd=Join-Path $ValidationRoot 'build-standalone-special.cmd'
    @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd
    if($LASTEXITCODE -ne 0){throw 'Standalone special helper compilation failed.'}
    $report=Join-Path $ValidationRoot 'standalone-special-selftest.json'
    if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
    & $exe $report
    $code=$LASTEXITCODE
    if(!(Test-Path -LiteralPath $report)){throw 'No fresh standalone special report.'}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    $receipt.cases=@($r.cases).Count
    $receipt.failedCases=@($r.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
    if($code -ne 0 -or !$r.ok){throw 'Standalone special regression failed.'}
    Push-Location -LiteralPath $project
    try {
        & node (Join-Path $PSScriptRoot 'steam_live_script_embed.mjs') --check
        if($LASTEXITCODE -ne 0){throw 'Steam live script embed is stale.'}
        $steamReport=Join-Path $ValidationRoot 'steam-actions-selftest.json'
        & node (Join-Path $PSScriptRoot 'screen_standalone_special_actions_selftest.mjs') $steamReport | Out-Null
        if($LASTEXITCODE -ne 0){throw 'Steam standalone action regression failed.'}
        $steam=Get-Content -LiteralPath $steamReport -Raw | ConvertFrom-Json
        if(!$steam.ok){throw 'Steam standalone action report failed.'}
        $receipt.steamActionCases=$steam.passed
    } finally { Pop-Location }
    $after=Get-Sources
    for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed while testing: $($after[$i].path)"}}
    $receipt.helperSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $receipt.ok=$true;$receipt.status='passed';$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt
    Write-Output "STANDALONE_SPECIAL_VERIFIED $ValidationRoot"
} catch {
    $receipt.ok=$false;$receipt.status='failed';$receipt.error=$_.Exception.Message;$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt
    throw
}
