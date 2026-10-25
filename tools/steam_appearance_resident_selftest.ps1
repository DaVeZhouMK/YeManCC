[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
function Load-Function($Path,$Name){$t=$null;$e=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$t,[ref]$e);if($e.Count){throw $e[0]};$f=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $Name},$true);if(!$f){throw "Missing $Name"};$f.Extent.Text}
Invoke-Expression (Load-Function (Join-Path $repo 'PowerControl\steam-appearance.ps1') 'Start-AppearanceBackend')
Invoke-Expression (Load-Function (Join-Path $repo 'PowerControl\steam-appearance\resident.ps1') 'Assert-SingleAppearanceBackend')
Invoke-Expression (Load-Function (Join-Path $repo 'PowerControl\steam-appearance\resident.ps1') 'Get-AppearanceProcessPath')
Invoke-Expression (Load-Function (Join-Path $repo 'PowerControl\steam-appearance\preset.ps1') 'Get-PresetResourceHash')
$script:BackendExe=Join-Path $Out 'CssLoader-Standalone-Headless.exe';[IO.File]::WriteAllText($script:BackendExe,'inert placeholder')
$script:AppearanceFixtureMutexPrefix='Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.'
function New-AppearanceMutex([string]$Purpose) {
 if($Purpose -notin @('Launch','ThemeControl')){throw 'APPEARANCE_FIXTURE_MUTEX_PURPOSE_DENIED'}
 New-Object Threading.Mutex($false,($script:AppearanceFixtureMutexPrefix+$Purpose))
}
$script:Rows=@();$script:Ready=$true;$script:Editing=$false;$script:Port=$false;$script:Spawns=0;$script:Waits=0
function Row($Id,$Parent){[pscustomobject]@{Id=$Id;Path=$script:BackendExe;StartTime=([datetime]'2026-10-23').AddSeconds($Id);Parent=$Parent}}
function Read-PresetState {$null}
function Test-CssDesktopEditing {$script:Editing}
function Assert-CssDesktopNotEditing {if($script:Editing){throw 'editor-active'}}
function Test-CurrentUserProcess($Process){$true}
function Get-BackendProcesses {@($script:Rows|Where-Object Path -eq $script:BackendExe)}
function Get-Process {param($Name) $script:Rows}
function Test-BackendOwner {$script:Ready}
function Get-NetTCPConnection {param($State,$LocalPort) if($script:Port){[pscustomobject]@{OwningProcess=999}}}
function Get-CimInstance {param($ClassName,$Filter) foreach($p in $script:Rows){[pscustomobject]@{ProcessId=$p.Id;ParentProcessId=$p.Parent}}}
function Start-ResidentAppearanceBackend {$script:Spawns++;$script:Rows=@((Row 100 0),(Row 101 100));$script:Ready=$true}
function Start-Sleep {param($Milliseconds) $script:Waits++;throw 'bounded-not-ready-fixture'}
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Name){if(!$Value){throw $Name};$cases.Add($Name)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
$script:Rows=@((Row 100 0),(Row 101 100));$result=Start-AppearanceBackend
Assert ($result.ok -and !$result.started -and $script:Spawns -eq 0) 'Packaged parent and child are reused as one instance'
$script:Rows=@((Row 100 0),(Row 200 0));Assert (Reject {Start-AppearanceBackend}) 'Independent existing roots are rejected without killing or replacing either'
$script:Rows=@([pscustomobject]@{Id=100;Path='D:\other\CssLoader-Standalone-Headless.exe';Parent=0});Assert (Reject {Start-AppearanceBackend}) 'Another installation still opening its port blocks creation'
$script:Rows=@();$script:Port=$true;Assert (Reject {Start-AppearanceBackend}) 'Occupied CSS port blocks creation even with no matching executable'
$script:Port=$false;$script:Editing=$true;Assert (Reject {Start-AppearanceBackend}) 'Desktop editing blocks creation of a missing backend'
$script:Editing=$false;$script:Ready=$false;$script:Rows=@((Row 100 0));Assert (Reject {Start-AppearanceBackend}) 'Unresponsive existing backend is retained instead of restarted'
Assert ($script:Waits -eq 1 -and $script:Spawns -eq 0) 'No retry launch occurs while an existing process is unready'
$script:Rows=@();$script:Ready=$false;$result=Start-AppearanceBackend;$again=Start-AppearanceBackend
Assert ($result.ok -and $result.started -and $again.ok -and !$again.started -and $script:Spawns -eq 1) 'Missing backend starts exactly once and the next Ensure reuses it'
$script:Editing=$true;$again=Start-AppearanceBackend;Assert ($again.ok -and $script:Spawns -eq 1) 'Existing editor-owned backend is preserved and reused'
$hashFile=Join-Path $Out 'resource.css';[IO.File]::WriteAllText($hashFile,'first');$script:HashReads=0;$script:PresetResourceHashes=@{}
function Get-ConfigHash($Path){$script:HashReads++;(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
$a=Get-PresetResourceHash $hashFile;$b=Get-PresetResourceHash $hashFile
Assert ($a -eq $b -and $script:HashReads -eq 1) 'Repeated status stages hash an unchanged resource only once per helper'
[IO.File]::WriteAllText($hashFile,'updated-longer');$c=Get-PresetResourceHash $hashFile
Assert ($c -ne $a -and $script:HashReads -eq 2) 'Resource modification invalidates the in-helper hash cache'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0;fixtureSpawns=$script:Spawns;hashReads=$script:HashReads}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) resident singleton and resource budget checks"
