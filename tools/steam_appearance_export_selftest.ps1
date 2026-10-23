param([string]$Out='G:\YeManCC-Work\Build\Validation\SteamPresetChain-20261013-172935\export')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'steam-appearance-export.ps1')
$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'
$target=Join-Path $Out ('PowerControl-'+[guid]::NewGuid().ToString('N'))
$files=@(Get-SteamAppearanceExportFiles $source)
foreach($relative in $files){$dest=Join-Path $target $relative;New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest)|Out-Null;Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $dest}
$cases=New-Object Collections.Generic.List[string]
$receipt=Assert-SteamAppearanceExport $source $target
if($receipt.defaultsEnabled -ne 8){throw 'default mismatch'};$cases.Add('Complete script/modules and 8/8 reference preset pass the export gate')
$retired=Join-Path $target 'YeManSteam\CssLoader-Standalone-Headless.txt';New-Item -ItemType Directory -Path (Split-Path -Parent $retired) -Force|Out-Null;[IO.File]::WriteAllText($retired,'C:\SOFT\CSSLoader Desktop\CssLoader-Standalone-Headless.exe')
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*Retired CSSLoader linked preset*'}
if(!$rejected){throw 'Retired CSS linked preset was exported'};$cases.Add('Packaging refuses a reintroduced legacy CSS linked-launch preset')
Remove-Item -LiteralPath $retired -Force
$scriptPath=Join-Path $target 'steam-appearance.ps1'
[IO.File]::WriteAllText($scriptPath,"param([string]`$Action,[string]`$Payload)`n")
$rejected=$false;try{Assert-SteamAppearanceExport $source $target|Out-Null}catch{$rejected=$true}
if(!$rejected){throw 'mixed old script was accepted'};$cases.Add('New frontend payload cannot pass with an old script that lacks BackendPath')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
$oldAutomatic=[IO.File]::ReadAllText($scriptPath).Replace('[string]$ReadyEvent=', '[string]$MissingReadyEvent=')
[IO.File]::WriteAllText($scriptPath,$oldAutomatic,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*missing ReadyEvent*'}
if(!$rejected){throw 'Old automatic helper protocol was accepted'};$cases.Add('Packaging refuses a script without the single-helper ready-event protocol')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
$withoutResult=[IO.File]::ReadAllText($scriptPath).Replace('$script:AutomaticResultProtocol=2','$script:AutomaticResultProtocol=0')
[IO.File]::WriteAllText($scriptPath,$withoutResult,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*automatic result protocol missing*'}
if(!$rejected){throw 'Missing automatic result protocol was accepted'};$cases.Add('Packaging refuses a helper without the bounded automatic result protocol')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
$oldUpgrade=[IO.File]::ReadAllText($scriptPath).Replace('Invoke-OwnedPresetResourceUpdate $current','Write-Output $current')
[IO.File]::WriteAllText($scriptPath,$oldUpgrade,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*owned resource update protocol missing*'}
if(!$rejected){throw 'Theme pack with old Ensure-only startup was accepted'};$cases.Add('Packaging rejects a controller that cannot synchronize owned resource upgrades before loading the theme')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
$oldRunningBranch=[IO.File]::ReadAllText($scriptPath).Replace("Invoke-AppearanceControl 'UpdateOwnedResources'","Invoke-AppearanceControl 'Status'")
[IO.File]::WriteAllText($scriptPath,$oldRunningBranch,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*owned resource update protocol missing*'}
if(!$rejected){throw 'Already-running mode without resource update was accepted'};$cases.Add('Packaging requires the already-running Automatic branch to update resources without starting another backend')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
[IO.File]::WriteAllText($scriptPath,[IO.File]::ReadAllText($scriptPath).Replace('Invoke-AutomaticAppearanceRefresh|Out-Null','# old helper only observed flags'),(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*automatic reapply protocol missing*'}
if(!$rejected){throw 'Automatic helper without actual reapply was accepted'};$cases.Add('Packaging refuses an automatic helper that never reconciles the existing document')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance.ps1') -Destination $scriptPath -Force
$idlePath=Join-Path $target 'steam-appearance\idle.ps1'
$oldIdle=[IO.File]::ReadAllText($idlePath).Replace('function Assert-CssDesktopNotEditing','function LegacyNoDesktopProtection')
[IO.File]::WriteAllText($idlePath,$oldIdle,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*Desktop editing protection missing*'}
if(!$rejected){throw 'Idle module without Desktop editing protection was accepted'};$cases.Add('Packaging refuses an Idle module missing Desktop editing protection')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\idle.ps1') -Destination $idlePath -Force
[IO.File]::WriteAllText($idlePath,([IO.File]::ReadAllText($idlePath).Replace('$script:AppearanceRenderProtocol=4','$script:AppearanceRenderProtocol=0')),(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*render receipt protocol missing*'}
if(!$rejected){throw 'Old style-count-only verifier was exported'};$cases.Add('Packaging rejects a render verifier without the per-theme receipt protocol')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\idle.ps1') -Destination $idlePath -Force
[IO.File]::WriteAllText($idlePath,([IO.File]::ReadAllText($idlePath).Replace('$script:AppearanceContextProtocol=1','$script:AppearanceContextProtocol=0')),(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*independent context protocol missing*'}
if(!$rejected){throw 'Main-document-only render verifier was exported'};$cases.Add('Packaging rejects a renderer without independent MainMenu and QuickAccess context capability')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\idle.ps1') -Destination $idlePath -Force
$manifestPath=Join-Path $target 'steam-appearance\manifest.json';$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$receiptPath=Join-Path $target 'steam-appearance\themes\Round\shared.css'
[IO.File]::WriteAllText($receiptPath,([IO.File]::ReadAllText($receiptPath).Replace('--ymcc-receipt-round: 1;','--legacy-round: 1;')),(New-Object Text.UTF8Encoding($false)))
$entry=$manifest.files|Where-Object path -eq 'Round/shared.css';$entry.sha256=Get-SteamAppearanceFileHash $receiptPath;$entry.bytes=(Get-Item -LiteralPath $receiptPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*render receipt missing*'}
if(!$rejected){throw 'Hash-valid theme without its render receipt was exported'};$cases.Add('Packaging rejects a hash-valid managed theme missing its CSS receipt')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Round\shared.css') -Destination $receiptPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$patchPath=Join-Path $target 'steam-appearance\themes\Round\intensity\10px.css'
$property=Get-SteamAppearanceFileReceiptProperty 'Round/intensity/10px.css'
[IO.File]::WriteAllText($patchPath,([IO.File]::ReadAllText($patchPath).Replace($property+': 1;','--legacy-patch: 1;')),(New-Object Text.UTF8Encoding($false)))
$entry=$manifest.files|Where-Object path -eq 'Round/intensity/10px.css';$entry.sha256=Get-SteamAppearanceFileHash $patchPath;$entry.bytes=(Get-Item -LiteralPath $patchPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*file receipt missing*'}
if(!$rejected){throw 'Hash-valid optional CSS without its file receipt was exported'};$cases.Add('Packaging rejects an optional patch missing its per-file receipt even when checksums are valid')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Round\intensity\10px.css') -Destination $patchPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$heroPath=Join-Path $target 'steam-appearance\themes\Art Hero\shared.css'
[IO.File]::WriteAllText($heroPath,([IO.File]::ReadAllText($heroPath).Replace('--met-hero-art-z-order: 1;','--met-hero-art-z-order: 2;')))
$entry=$manifest.files|Where-Object path -eq 'Art Hero/shared.css';$entry.sha256=Get-SteamAppearanceFileHash $heroPath;$entry.bytes=(Get-Item -LiteralPath $heroPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*variable baseline mismatch*'}
if(!$rejected){throw 'Hash-valid CSS baseline drift was exported'};$cases.Add('Packaging rejects a changed CSS fallback even when its manifest checksum was updated')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Art Hero\shared.css') -Destination $heroPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$configPath=Join-Path $target 'steam-appearance\themes\Round\config_USER.json'
$config=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json;$config.active=$false
[IO.File]::WriteAllText($configPath,($config|ConvertTo-Json -Compress))
$entry=$manifest.files|Where-Object path -eq 'Round/config_USER.json';$entry.sha256=Get-SteamAppearanceFileHash $configPath;$entry.bytes=(Get-Item -LiteralPath $configPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*enable all managed*'}
if(!$rejected){throw 'a hash-valid personal selection was accepted as default'};$cases.Add('Even a valid checksum manifest cannot ship personal disabled flags as factory defaults')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Round\config_USER.json') -Destination $configPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force
# A file can keep the old path receipt while its actual rule changes.
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$contentPath=Join-Path $target 'steam-appearance\themes\Round\round-outline.css'
$originalContent=[IO.File]::ReadAllText($contentPath)
[IO.File]::WriteAllText($contentPath,($originalContent.Replace('animation: none !important;','animation: none;')),(New-Object Text.UTF8Encoding($false)))
$entry=$manifest.files|Where-Object path -eq 'Round/round-outline.css';$entry.sha256=Get-SteamAppearanceFileHash $contentPath;$entry.bytes=(Get-Item -LiteralPath $contentPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*content revision mismatch*'}
if(!$rejected){throw 'Hash-valid changed CSS retaining its old content marker was exported'};$cases.Add('Packaging recomputes content identity after a CSS rule changes even when ordinary checksum is updated')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Round\round-outline.css') -Destination $contentPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
[IO.File]::WriteAllText($contentPath,($originalContent.Replace('/* YMCC content receipt: exact bundled file revision. */','/* Content marker removed. */')),(New-Object Text.UTF8Encoding($false)))
$entry=$manifest.files|Where-Object path -eq 'Round/round-outline.css';$entry.sha256=Get-SteamAppearanceFileHash $contentPath;$entry.bytes=(Get-Item -LiteralPath $contentPath).Length
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 12))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*content marker missing or repeated*'}
if(!$rejected){throw 'CSS without a valid content marker was exported'};$cases.Add('Packaging refuses a missing content footer even when ordinary checksum is updated')
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\themes\Round\round-outline.css') -Destination $contentPath -Force
Copy-Item -LiteralPath (Join-Path $source 'steam-appearance\manifest.json') -Destination $manifestPath -Force

# Field regression: capability strings survive while a malformed regex tears the
# renderer function apart. Check the real parser gate, not marker recognition.
$rendererOriginal=[IO.File]::ReadAllText($idlePath)
$brokenRenderer=$rendererOriginal.Replace("function ConvertTo-AppearanceVariableValue([string]"+'$Value'+") {", "function ConvertTo-AppearanceVariableValue([string]"+'$Value'+") {"+[Environment]::NewLine+"if("+'$Value'+" -match 'broken")
if($brokenRenderer -ceq $rendererOriginal){throw 'Field renderer mutation was not applied'}
[IO.File]::WriteAllText($idlePath,$brokenRenderer,(New-Object Text.UTF8Encoding($true)))
$rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*renderer has syntax errors*'}
if(!$rejected){throw 'Marker-valid syntax-broken renderer was exported'};$cases.Add('Packaging rejects a syntax-broken renderer even when render3 capability markers remain')
[IO.File]::WriteAllText($idlePath,$rendererOriginal,(New-Object Text.UTF8Encoding($true)))
foreach($module in @('steam-appearance\preset.ps1','steam-appearance\resident.ps1')){
    $modulePath=Join-Path $target $module;$original=[IO.File]::ReadAllText($modulePath)
    [IO.File]::WriteAllText($modulePath,($original+[Environment]::NewLine+'function BrokenFieldModule {'),(New-Object Text.UTF8Encoding($true)))
    $rejected=$false;try{Get-SteamAppearanceExportFiles $target|Out-Null}catch{$rejected=$_.Exception.Message -like '*module has syntax errors*'}
    if(!$rejected){throw ('Syntax-broken module was exported: '+$module)};$cases.Add('Packaging parses the dot-sourced module before export: '+$module)
    [IO.File]::WriteAllText($modulePath,$original,(New-Object Text.UTF8Encoding($true)))
}
# Execute the exact production preflight statements with an owned sentinel standing
# in for the installed file. Nothing from the full deploy script is launched.
$deployPath=Join-Path $PSScriptRoot 'deploy-installed.ps1';$deployTokens=$null;$deployErrors=$null
$deployAst=[Management.Automation.Language.Parser]::ParseInput([IO.File]::ReadAllText($deployPath,[Text.Encoding]::UTF8),[ref]$deployTokens,[ref]$deployErrors)
if($deployErrors.Count){throw 'Deployment script parse failed'}
$preflight=@($deployAst.EndBlock.Statements|Where-Object {$_.Extent.Text -ceq 'Get-SteamAppearanceExportFiles $sourcePowerControl | Out-Null'})
$copyCall=@($deployAst.EndBlock.Statements|Where-Object {$_.Extent.Text -ceq 'Copy-InstalledPowerControlMainAssets $sourcePowerControl $installedPowerControl'})
$installedMutation=@($deployAst.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Move-Item' -and $n.Extent.Text -match 'destination'},$true))
if($preflight.Count -ne 1 -or $copyCall.Count -ne 1 -or !$installedMutation.Count -or $preflight[0].Extent.StartOffset -ge $copyCall[0].Extent.StartOffset -or $preflight[0].Extent.StartOffset -ge $installedMutation[0].Extent.StartOffset){throw 'Appearance preflight is not before installed mutations'}
$invokePreflight=[scriptblock]::Create($preflight[0].Extent.Text)
$sentinel=Join-Path $target 'installed-sentinel.txt';[IO.File]::WriteAllText($sentinel,'healthy installed component')
[IO.File]::WriteAllText($idlePath,$brokenRenderer,(New-Object Text.UTF8Encoding($true)))
$sourcePowerControl=$target;$blocked=$false
try{&$invokePreflight;[IO.File]::WriteAllText($sentinel,'bad replacement')}catch{$blocked=$_.Exception.Message -like '*renderer has syntax errors*'}
if(!$blocked -or [IO.File]::ReadAllText($sentinel) -cne 'healthy installed component'){throw 'Invalid source modified the owned installed sentinel'}
$cases.Add('Actual deployment preflight blocks a broken renderer before the installed sentinel is touched')
[IO.File]::WriteAllText($idlePath,$rendererOriginal,(New-Object Text.UTF8Encoding($true)))
&$invokePreflight;[IO.File]::WriteAllText($sentinel,'validated replacement')
if([IO.File]::ReadAllText($sentinel) -cne 'validated replacement'){throw 'Valid deployment was blocked'}
$cases.Add('Actual deployment preflight admits a complete matching appearance payload')
foreach($tool in @('package-release.ps1','deploy-installed.ps1')){if([IO.File]::ReadAllText((Join-Path $PSScriptRoot $tool)) -notmatch 'Assert-SteamAppearanceExport'){throw "Export gate missing: $tool"}};$cases.Add('Release packaging and installed deployment both invoke the appearance export gate')
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) appearance export checks"
