[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(!$ComponentRoot){$ComponentRoot=Join-Path $repo 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$script:AppearanceObservationOnly=$true;$script:AutomaticResultProtocol=2
New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
Get-ChildItem -LiteralPath (Join-Path $ComponentRoot 'steam-appearance\themes') -Directory|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination $script:ThemeRoot -Recurse -Force}
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'SOURCE_PARSE_FAILED'}
$dispatch=@($ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.TryStatementAst] -and $_.Body.Extent.Text -match "if\(\`$Action -eq 'Automatic'\)"})
if($dispatch.Count -ne 1){throw 'EXACT_AUTOMATIC_DISPATCH_REQUIRED'}
$script:RpcReads=New-Object Collections.Generic.List[string]
$script:Editing=$false;$script:EditAfterRender=$false;$script:Repair=$false;$script:Prepared=$true;$script:Rendered=$true;$script:ChangedSession=$false;$script:SessionReads=0;$script:DomReads=0;$script:HintReads=0
$SessionPid=123;$SessionBirth='456';$BackendPath=$script:BackendExe;$portOwners=@();$Payload=''
function Get-Process {param($Name,$Id) @()}
function Get-BackendProcesses {[pscustomobject]@{Id=42;Path=$script:BackendExe}}
function Test-BackendOwner {$true}
function Test-CssDesktopEditing {$script:Editing}
function Get-AppearanceSteamSession {$script:SessionReads++;[pscustomobject]@{pid=123;birth=$(if($script:ChangedSession -and $script:SessionReads -gt 1){'457'}else{'456'});root='C:\owned-fixture-steam'}}
function Test-AppearanceRendered($Session,$Status){$script:DomReads++;if($script:EditAfterRender){$script:Editing=$true};$script:Rendered}
function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1){throw 'OWNED_FAMILY_REQUIRED'}}
function Initialize-PresetTargetHint {$script:HintReads++}
function Recover-PresetTransaction {throw 'READ_ONLY_TRANSACTION_REPLAY_FORBIDDEN'}
function Invoke-AutomaticAppearanceRefresh {[IO.File]::WriteAllText((Join-Path $Out 'forbidden-refresh.txt'),'READ_ONLY_REFRESH_FORBIDDEN');throw 'READ_ONLY_REFRESH_FORBIDDEN'}
function Save-PresetTargetHint {throw 'READ_ONLY_HINT_WRITE_FORBIDDEN'}
function Get-PresetMetadata($Rows,$BackendThemes){[pscustomobject]@{packPrepared=$script:Prepared;packRepairRequired=$script:Repair;packError='';foreignActiveCount=0;backendReloadRequired=$false}}
function Invoke-RestMethod {
 param($Uri,$Method,$Body,$ContentType,$TimeoutSec)
 $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json
 $script:RpcReads.Add($request.method)
 if($request.method -eq 'fetch_theme_path'){return [pscustomobject]@{success=$true;res=$script:ThemeRoot}}
 if($request.method -eq 'get_themes'){return [pscustomobject]@{success=$true;res=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})}}
 throw 'READ_ONLY_RPC_ESCAPED'
}
function Write-JsonResult($Value){if(!$Value.ok){$Value|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'dispatch-failure.json') -Encoding UTF8};$script:ResultJson=$Value}
function Start-Sleep {throw 'VERIFY_ONLY_CANNOT_POLL_OR_DELAY'}
$cases=New-Object Collections.Generic.List[string]
function Assert($Condition,$Name){if(!$Condition){throw $Name};$cases.Add($Name)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
function Reset-Case {$script:Editing=$false;$script:EditAfterRender=$false;$script:Repair=$false;$script:Prepared=$true;$script:Rendered=$true;$script:ChangedSession=$false;$script:SessionReads=0;$script:DomReads=0;$script:AppearanceHelperPhase=0;$script:AppearanceHelperFlags=0;$script:AppearanceFailureCode=0}
$baseline=Get-ThemeRevision @(Get-ThemeRows)
$status=Get-AppearanceStatus
Assert ($status.backendReady -and $status.configuredMatchesBackend -and $status.themes.Count -eq 7) 'Read-only Status uses the actual production planner and all seven owned theme configurations'
Assert (($script:RpcReads -join ',') -ceq 'fetch_theme_path,get_themes') 'Read-only Status performs only the two permitted RPC reads and no transaction/hint write'
foreach($action in @('Ensure','UpdateOwnedResources','Apply','Refresh','Reload','Prepare','Reset','Restore','Reconcile')){Assert (Reject {Invoke-AppearanceControl $action}) ('Read-only dispatch blocks mutating action '+$action)}
foreach($method in @('set_theme_state','reset','dummy_function')){Assert (Reject {Invoke-LoaderRpc $method}) ('Read-only RPC rejects '+$method+' before reaching transport')}
$sentinel=Join-Path $Out 'unchanged.txt';[IO.File]::WriteAllText($sentinel,'unchanged')
Assert ((Reject {Write-ConfigAtomic $sentinel 'changed'}) -and [IO.File]::ReadAllText($sentinel) -ceq 'unchanged') 'Read-only file writer cannot replace even an owned sentinel'
Reset-Case;$script:Repair=$true
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:DomReads -eq 0 -and $script:AppearanceFailureCode -eq 3) 'Known resource drift cannot earn resident confirmation or authorize an automatic repair'
Reset-Case;$script:Prepared=$false
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:DomReads -eq 0) 'An unprepared preset cannot become Ready through verification'
Reset-Case;$script:Editing=$true
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:DomReads -eq 0 -and $script:AppearanceFailureCode -eq 15) 'Desktop editor owns the backend before any DOM observation'
Reset-Case;$script:EditAfterRender=$true
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:AppearanceFailureCode -eq 15) 'Editor opened during DOM verification wins before the final receipt'
Reset-Case;$script:Rendered=$false
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:AppearanceFailureCode -eq 6) 'A missing real render acknowledgement cannot be promoted to success'
Reset-Case;$script:ChangedSession=$true
Assert ((Reject {Invoke-AppearanceControl 'Idle'}) -and $script:AppearanceFailureCode -eq 9) 'A late Steam birth change invalidates a previously successful DOM query'
Reset-Case
$base='Local\YMCC.CssUse.'+[guid]::NewGuid().ToString('N');$ReadyEvent=$base+'.ready';$IdleEvent=$base+'.idle'
$ownedReady=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$ReadyEvent)
$ownedIdle=[Threading.EventWaitHandle]::new($true,[Threading.EventResetMode]::ManualReset,$IdleEvent)
$ownedMap=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(($base+'.result'),32)
$view=$ownedMap.CreateViewAccessor(0,32)
try{
 $Action='Automatic';$VerifyOnly=$true;$StartBackend=$false;$GateTimeoutMs=0;$script:AutomaticResultMap=$null;$script:ResultJson=$null
 . ([scriptblock]::Create($dispatch[0].Extent.Text))
 Assert ($ownedReady.WaitOne(0) -and $script:ResultJson.ok -and $script:ResultJson.resident) 'The exact Automatic VerifyOnly dispatcher completes through its named events without initialization/reapply'
 $values=@(0..7|ForEach-Object{$view.ReadInt32($_*4)})
 Assert ($values[0] -eq 0x594D4353 -and $values[1] -eq 2 -and $values[2] -eq 8 -and $values[3] -eq 0 -and $values[4] -eq 31 -and $values[5] -eq 1) 'The actual helper publishes the complete 32-byte resident result after read-only verification'
 Assert ((Get-ThemeRevision @(Get-ThemeRows)) -ceq $baseline) 'Every user-style active flag and parameter survives the complete read-only path byte-for-byte'
}finally{$view.Dispose();$ownedMap.Dispose();$ownedIdle.Dispose();$ownedReady.Dispose()}
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=$cases.ToArray();rpcReads=$script:RpcReads.ToArray();hintReads=$script:HintReads;realSteamOperations=0;realCssOperations=0;productionThemeWrites=0;productionHelpersLaunched=0;sourceFunctions=$script:AppearanceFixtureInitialization.sourceFunctions;scope='Exact production functions and Automatic dispatch; private named events/map and owned copied configs; process/DOM/HTTP endpoints are explicit memory ports'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) read-only manual observation checks"
