param([Parameter(Mandatory=$true)][string]$ScriptDirectory,[Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$fixtureSource=Join-Path $PSScriptRoot 'lossless_close_owned.cs'
$fixtureExe=Join-Path $EvidenceDirectory 'OwnedLosslessCloseWindow.exe'
& (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe') /nologo /target:exe "/out:$fixtureExe" $fixtureSource
if($LASTEXITCODE -ne 0){throw 'Owned hidden window fixture compile failed'}
$scripts=@(Get-ChildItem -LiteralPath $ScriptDirectory -Filter '*.ps1')
$close=@($scripts|Where-Object {[IO.File]::ReadAllText($_.FullName).Contains('YMCC_LOSSLESS_CLOSE')})
if($close.Count -ne 1){throw 'Expected one exact generated product close script'}
$actual=[IO.File]::ReadAllText($close[0].FullName)
$match=[regex]::Match($actual,'(?s)Add-Type -TypeDefinition @"\r?\n(?<body>.*?)\r?\n"@')
if(-not $match.Success){throw 'Product Add-Type body not found'}
Add-Type -TypeDefinition $match.Groups['body'].Value
$results=New-Object System.Collections.ArrayList
$foreground=Add-Type -PassThru -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class CloseProbeForeground{[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();}'
$before=[CloseProbeForeground]::GetForegroundWindow()
foreach($case in @('hidden','wrongbirth','wrongpath','wrongclass','ambiguous','ignore')){
 $mode=if($case -in @('wrongbirth','wrongpath')){'hidden'}else{$case}
 $info=New-Object Diagnostics.ProcessStartInfo
 $info.FileName=$fixtureExe;$info.Arguments=$mode;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.RedirectStandardOutput=$true
 $owned=[Diagnostics.Process]::Start($info)
 try{
  $ready=$owned.StandardOutput.ReadLineAsync()
  if(-not $ready.Wait(2000) -or $ready.Result -ne 'READY'){throw 'Hidden window did not initialize'}
  $created=[uint64]$owned.StartTime.ToUniversalTime().ToFileTimeUtc()
  $path=$fixtureExe
  if($case -eq 'wrongbirth'){$created++}
  if($case -eq 'wrongpath'){$path=Join-Path $EvidenceDirectory 'Wrong.exe'}
  $rejected=$false;$message=''
  try{[YmccLosslessClose]::Close([uint32]$owned.Id,$path,$created)}catch{$rejected=$true;$message=$_.Exception.Message}
  if($case -eq 'hidden'){
   if($rejected -or -not $owned.WaitForExit(1000)){throw ('Hidden exact product WM_CLOSE failed: '+$message)}
  }else{
   if(-not $rejected -or $owned.HasExited){throw ('A negative boundary was not preserved: '+$case)}
  }
  [void]$results.Add([pscustomobject]@{case=$case;pass=$true;rejected=$rejected;message=$message})
 }finally{if(-not $owned.HasExited){$owned.Kill();[void]$owned.WaitForExit(2000)};$owned.Dispose()}
}
[pscustomobject]@{pass=$true;cases=$results;foregroundUnchanged=($before -eq [CloseProbeForeground]::GetForegroundWindow());actualLsActions=0;hiddenOwnedWindowsOnly=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'close-consumer.json') -Encoding utf8

