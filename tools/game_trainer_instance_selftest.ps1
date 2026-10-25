[CmdletBinding()]
param([string]$ValidationRoot='G:\YeManCC-Work\Mainline\Build\Validation\TrainerInstanceHardening-20261015',[string]$BackendPath='')
$ErrorActionPreference='Stop'
if(-not $BackendPath){$BackendPath=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$testRoot=Join-Path $ValidationRoot ('real-process-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($testRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new();$owned=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Reject([scriptblock]$body){$rejected=$false;try{& $body|Out-Null}catch{$rejected=$true};Assert $rejected 'Expected rejection'}
function Check([string]$name,[scriptblock]$body){try{& $body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function Json([string]$path,$value){[IO.Directory]::CreateDirectory((Split-Path $path))|Out-Null;[IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $value -Depth 10),[Text.UTF8Encoding]::new($false))}
$game=Join-Path $testRoot 'Game.exe';$marker=Join-Path $testRoot 'launch-marker.txt';$title='YMCC Trainer Instance Fixture'
$escapedMarker=$marker.Replace('"','""')
$cs=@"
using System;using System.IO;using System.Diagnostics;using System.Drawing;using System.Windows.Forms;
class QuietForm:Form {
 [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int cmd);
 protected override bool ShowWithoutActivation {get{return true;}}
 protected override CreateParams CreateParams {get{var p=base.CreateParams;p.ExStyle|=0x08000080;return p;}}
 protected override void OnShown(EventArgs e){base.OnShown(e);ShowWindow(Handle,4);}
}
class Fixture {
 [STAThread] static void Main(string[] args) {
  if(args.Length>0 && args[0]=="game") {
   var f=new QuietForm();f.Text="$title";f.ShowInTaskbar=true;f.StartPosition=FormStartPosition.Manual;f.Location=new Point(-32000,-32000);f.Size=new Size(100,40);
   var t=new Timer();t.Interval=60000;t.Tick+=(s,e)=>f.Close();t.Start();Application.Run(f);
  } else {File.AppendAllText(@"$escapedMarker",Process.GetCurrentProcess().Id.ToString()+Environment.NewLine);}
 }
}
"@
$csPath=Join-Path $testRoot 'fixture.cs';[IO.File]::WriteAllText($csPath,$cs,[Text.Encoding]::UTF8)
$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $csc /nologo /target:winexe /r:System.Windows.Forms.dll /r:System.Drawing.dll ('/out:'+$game) $csPath
if($LASTEXITCODE){throw 'Unable to compile isolated process fixture'}
$library=Join-Path $testRoot 'library';$trainerFolder=Join-Path $library 'fixture';[IO.Directory]::CreateDirectory($trainerFolder)|Out-Null
$trainer=Join-Path $trainerFolder 'Trainer.exe';[IO.File]::Copy($game,$trainer)
$settings=Join-Path $testRoot 'settings';$db=Join-Path $settings 'db'
Json (Join-Path $settings 'settings.json') @{downloadPath=$library;safePath=$false;launchAsAdmin=$false}
Json (Join-Path $db 'translations.json') @(@{en_US=$title;zh_CN=$title;steam_id=1361510})
$sha=(Get-FileHash -LiteralPath $game).Hash.ToLowerInvariant();$trainerSha=(Get-FileHash -LiteralPath $trainer).Hash.ToLowerInvariant()
function Start-GameFixture {
 $process=Start-Process -FilePath $game -ArgumentList 'game' -WindowStyle Hidden -PassThru
 $owned.Add($process);$until=[DateTime]::UtcNow.AddSeconds(8)
 do{$observation=& $module {param($p)Get-TrainerProcessObservation $p} $process.Id;if($observation){return $observation};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
 throw 'Actual fixture process/window observation unavailable'
}
function Select-FixtureTarget($value){& $module {param($v)$script:InstanceSelected=$v;function script:Read-TrainerSharedGameTarget {$t=$script:InstanceSelected.Clone();$t.name='Game';$t.snapshotPath=Join-Path (Split-Path $t.path) 'game-target.json';return $t}} $value}
function Guard($observation,[string]$hash=$sha){& $module {param($p,$i,$c,$h)Assert-TrainerLaunchTarget $p $i $c $h} $game $observation.pid $observation.created $hash}
try {
 $first=Start-GameFixture;Select-FixtureTarget $first
 Check 'real Win32 PID, birth, image and complete window title are observed' {Assert ($first.path -ieq $game -and $first.title -ceq $title -and $first.pid -gt 0 -and $first.created -match '^[1-9][0-9]*$')}
 Check 'real selected process confirms a unique isolated GCM identity' {$identity=Resolve-TrainerIdentity $title $game $db @{games=@{}} (Join-Path $testRoot 'no-custom-library') $first.pid $first.created;Assert ($identity.method -eq 'ymcc-game-target' -and $identity.executableSha256 -eq $sha)}
 Check 'exact real process tuple and game hash pass final launch guard' {Guard $first}
 Check 'wrong creation time is rejected even when EXE still runs' {$bad=$first.Clone();$bad.created='134365281498356567';Reject {Guard $bad}}
 Check 'another image path cannot borrow a live PID' {Reject {& $module {param($p,$i,$c,$h)Assert-TrainerLaunchTarget $p $i $c $h} $trainer $first.pid $first.created $sha}}
 Check 'changed EXE hash is rejected before starting a trainer' {Reject {Guard $first ('0'*64)}}
 Check 'partial process evidence is rejected' {Reject {& $module {param($p,$i)Assert-TrainerLaunchTarget $p $i '' ''} $game $first.pid}}
 $process=$owned[0];$process.Kill();Assert ($process.WaitForExit(8000)) 'Fixture did not stop'
 $second=Start-GameFixture;Select-FixtureTarget $second
 Check 'same EXE restart cannot reuse its old observed process tuple' {Reject {Guard $first};Guard $second}
 & $module {param($s,$l)
  $script:InstanceTestOriginalResolve=(Get-Command Resolve-TrainerLaunchPlan).ScriptBlock;$script:InstanceTestSettings=$s;$script:InstanceTestLibrary=$l
  function script:Resolve-TrainerLaunchPlan {
   param([string]$Mode,[string]$TrainerPath,[string]$TrainerFolder,[string]$ExpectedSha256)
   & $script:InstanceTestOriginalResolve -Mode $Mode -TrainerPath $TrainerPath -TrainerFolder $TrainerFolder -ExpectedSha256 $ExpectedSha256 -GcmSettingsRoot $script:InstanceTestSettings -GcmDefaultRoot $script:InstanceTestLibrary
  }
 } $settings $library
 function Launch($observation) {
  $cache=Join-Path $testRoot 'cache';$id='instance-'+[guid]::NewGuid().ToString('N');$result=Join-Path $cache ('requests/'+$id+'.json')
  Invoke-TrainerLaunch -Mode Trainer -TrainerPath $trainer -TrainerFolder $trainerFolder -ExpectedSha256 $trainerSha -GameExecutablePath $game -GameProcessId $observation.pid -GameProcessCreated $observation.created -ExpectedGameSha256 $sha -CacheRoot $cache -ResultPath $result -RequestId $id
  return ([IO.File]::ReadAllText($result)|ConvertFrom-Json)
 }
 Check 'production launch entry rejects old instance without creating a process' {$rejected=Launch $first;Assert (-not $rejected.ok -and $rejected.error);Assert (-not [IO.File]::Exists($marker))}
 Check 'production OS launcher starts an inert trainer only for the current instance' {
  $receipt=Launch $second;Assert $receipt.ok $receipt.error
  $until=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($marker) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 100}
  Assert ([IO.File]::Exists($marker));$lines=@([IO.File]::ReadAllLines($marker));Assert ($lines.Count -eq 1 -and $lines[0] -eq [string]$receipt.pid)
  $status=[IO.File]::ReadAllText((Join-Path $testRoot 'cache/last-launch-status.json'))|ConvertFrom-Json;Assert ($status.ok -and $status.gameProcessId -eq $second.pid -and $status.result.pid -eq $receipt.pid)
 }
} finally {
 foreach($process in $owned){try{if(-not $process.HasExited){$process.Kill();$process.WaitForExit(5000)|Out-Null}}catch{}}
 $failed=@($checks|Where-Object{-not $_.ok});$report=@{passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();root=$testRoot;realGameProcesses=0;realTrainerExecutions=0;actualInertTrainerLaunches=$(if([IO.File]::Exists($marker)){@([IO.File]::ReadAllLines($marker)).Count}else{0});networkRequests=0;mockBoundary='The shared native target is an explicit isolated fixture and GCM settings/root selection is redirected into this isolated test library; process observations and OS process creation are production implementations.'}
 Json (Join-Path $ValidationRoot 'real-process-tests.json') $report
 [pscustomobject]$report|Select-Object passed,failed,actualInertTrainerLaunches,root|ConvertTo-Json
}
if($failed.Count){exit 1}
