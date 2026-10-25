param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8)
$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###'
$at=$text.LastIndexOf($marker)+$marker.Length
$prefix=$text.Substring(0,$at)+"`r`n"
$dir=Join-Path $Evidence ('wrapper (test)-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($dir)|Out-Null
$payload=[IO.File]::ReadAllText((Join-Path $Evidence 'inert-wrapper-payload.txt'),[Text.Encoding]::UTF8)
$results=@()
foreach($route in @('nircmd','fallback')) {
    $bat=Join-Path $dir ($route+' 中文 ! 启动.bat')
    $receipt=Join-Path $dir ($route+'-receipt.json')
    $header=$prefix
    if($route -eq 'fallback'){$header=$header.Replace('if exist "C:\SOFT\NirCmd\nircmd.exe" (','if exist "Z:\YMCC-ABSENT-FOR-INERT-FALLBACK\nircmd.exe" (')}
    [IO.File]::WriteAllText($bat,$header+$payload,[Text.UTF8Encoding]::new($false))
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$env:ComSpec;$start.Arguments='/d /c ""'+$bat+'""';$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.EnvironmentVariables['YEMAN_TEST_RECEIPT']=$receipt
    $child=[Diagnostics.Process]::Start($start)
    try {if(-not $child.WaitForExit(15000)){throw 'inert wrapper timeout'}} finally {$child.Dispose()}
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(-not [IO.File]::Exists($receipt) -and $clock.ElapsedMilliseconds -lt 10000){Start-Sleep -Milliseconds 100}
    if(-not [IO.File]::Exists($receipt)){throw ('missing wrapper receipt '+$route)}
    $data=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json
    if(-not $data.pass -or $data.consoleVisible -or $data.source -cne $bat){throw ('wrapper hidden/Unicode binding failed '+$route)}
    $results+=[ordered]@{route=$route;pass=$true;consoleVisible=$data.consoleVisible;unicodeAndExclamationPath=$true;productActions=0}
}
$body=$text.Substring($at);$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
foreach($name in @('Start-Quiet','Initialize-WindowApi')) {
 $fn=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false))[0]
 . ([scriptblock]::Create($fn.Extent.Text))
}
Initialize-WindowApi
$result=Start-Quiet $env:ComSpec '/d /c "echo inert-native-helper & exit /b 7"' -Capture
if($result.Code -ne 7 -or $result.Text -notmatch 'inert-native-helper'){throw 'native helper output/exit capture failed'}
# Deliberately stall one owned helper and verify actual bounded destruction.
$clock=[Diagnostics.Stopwatch]::StartNew()
$timeout=Start-Quiet (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 10"' -Capture
if($timeout.Code -ne -2 -or $timeout.Text -notmatch 'helper-timeout pid=(\d+) retired=True' -or $clock.ElapsedMilliseconds -gt 6500){throw 'owned helper timeout did not retire the actual child in bounds'}
$ownedId=[int]$Matches[1]
if(Get-Process -Id $ownedId -ErrorAction SilentlyContinue){throw 'owned timed-out helper still exists'}
[ordered]@{status='PASS';routes=$results;nativeHelperCapture=$true;ownedStalledHelperRetired=$true;actualWindowApiCompiled=$true;productApplicationsStartedOrStopped=$false}|ConvertTo-Json -Depth 5|Tee-Object -FilePath (Join-Path $Evidence 'wrapper-result.json')