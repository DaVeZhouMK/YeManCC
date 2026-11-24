[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='',[ValidateSet('All','ValidationLoops','IndexFreshness')][string]$Case='All')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
if(!$ComponentRoot){$ComponentRoot=Join-Path $repo 'PowerControl'}
$pack=Join-Path $ComponentRoot 'steam-appearance'
. (Join-Path $pack 'idle.ps1')
$manifest=Get-Content -LiteralPath (Join-Path $pack 'manifest.json') -Raw|ConvertFrom-Json
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Label){if(!$Value){throw $Label};$cases.Add($Label)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
function Copy-Json($Value){$Value|ConvertTo-Json -Depth 32 -Compress|ConvertFrom-Json}
function Assert-MenuProjection($Plan,[string]$Name,$Definition,$Config,[string]$Label){
 foreach($menu in 'MainMenu','QuickAccess'){
  $direct=Get-AppearanceThemeRenderPlan $Name $Definition $Config -Target $menu
  Assert (($Plan.contexts.$menu|ConvertTo-Json -Depth 32 -Compress) -ceq ($direct|ConvertTo-Json -Depth 32 -Compress)) ('Projected '+$menu+' matches a direct plan: '+$Label)
 }
}
if($Case -eq 'IndexFreshness'){
 $original=$script:AppearanceContentManifestFile
 $script:AppearanceContentManifestFile=Join-Path $Out 'index-freshness-fixture.json'
 $owned=$script:AppearanceContentManifestFile
 [IO.File]::Copy($original,$owned)
 $item=[IO.FileInfo]::new($owned);$length=$item.Length;$creation=$item.CreationTimeUtc;$mtime=$item.LastWriteTimeUtc
 $first=Get-AppearanceContentIndex
 Assert ([object]::ReferenceEquals($first,(Get-AppearanceContentIndex))) 'Identical content reuses the parsed table'
 $raw=[IO.File]::ReadAllText($owned);$changed=$raw -replace '("renderContentProtocol"\s*:\s*)1','${1}2'
 Assert ($raw -cne $changed) 'Fixture locates the original index version'
 [IO.File]::WriteAllText($owned,$changed,[Text.UTF8Encoding]::new($false))
 [IO.File]::SetCreationTimeUtc($owned,$creation);[IO.File]::SetLastWriteTimeUtc($owned,$mtime)
 $item=[IO.FileInfo]::new($owned)
 Assert ($item.Length -eq $length -and $item.CreationTimeUtc -eq $creation -and $item.LastWriteTimeUtc -eq $mtime) 'Replacement retains all previous cache metadata'
 $failure='';try{Get-AppearanceContentIndex|Out-Null}catch{$failure=$_.Exception.Message}
 Assert ($failure -ceq 'render-content-index-invalid') 'Same-metadata replacement cannot reuse the stale table'
 [IO.File]::Copy($original,$owned,$true)
 Assert ([object]::ReferenceEquals($first,(Get-AppearanceContentIndex))) 'Restoring the exact bytes recovers the valid table'
 $validPath='Round/shared.css'
 Assert ($first.ContainsKey($validPath)) 'Fixture contains the managed CSS path'
 foreach($badPath in 'Unknown/shared.css','Round//shared.css','Round/part/../shared.css','Round/part/./shared.css','Round/C:/shared.css'){
  $badIndex=$raw.Replace('"'+$validPath+'"','"'+$badPath+'"')
  Assert ($badIndex -cne $raw) ('Fixture replaced the CSS path: '+$badPath)
  [IO.File]::WriteAllText($owned,$badIndex,[Text.UTF8Encoding]::new($false))
  $failure='';try{Get-AppearanceContentIndex|Out-Null}catch{$failure=$_.Exception.Message}
  Assert ($failure -ceq 'render-file-path-invalid') ('Index rejects unsafe CSS path: '+$badPath)
 }
 [IO.File]::Copy($original,$owned,$true)
 Assert ([object]::ReferenceEquals($first,(Get-AppearanceContentIndex))) 'Rejected paths do not replace the valid index'
 Assert ($null -eq (Get-AppearanceFileReceipt 'Round' 'shared.css' -ValidateOnly)) 'Path-only validation does not emit an unused receipt'
 Assert ((Get-AppearanceFileReceipt 'Round' 'shared.css') -ceq '--ymcc-file-f2a3b738282d5024') 'The plan still gets the exact file receipt'
 [pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);selectedCase=$Case;realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
 Write-Output "PASS: $($cases.Count) bundled index freshness checks"
 exit 0
}
if($Case -eq 'ValidationLoops'){
 $defined=@{};$configs=@{};$outcomes=New-Object Collections.Generic.List[object]
 foreach($theme in $manifest.themes){$dir=Join-Path (Join-Path $pack 'themes') $theme.name;$defined[$theme.name]=[IO.File]::ReadAllText((Join-Path $dir 'theme.json'))|ConvertFrom-Json;$configs[$theme.name]=[IO.File]::ReadAllText((Join-Path $dir 'config_USER.json'))|ConvertFrom-Json}
 function Observe-LoopPlan([string]$Name,$Definition,$Config,[string]$Label,[string]$ExpectedFailure=''){
  $sourceBefore=($Definition|ConvertTo-Json -Depth 64 -Compress)+'|'+($Config|ConvertTo-Json -Depth 64 -Compress)
  $plan=$null;$failure='';try{$plan=Get-AppearanceThemeRenderPlan $Name $Definition $Config}catch{$failure=$_.Exception.Message}
  Assert ($failure -ceq $ExpectedFailure) ('Exact loop validation result: '+$Label)
  $validationOutput=$null;$validationFailure=''
  try{$validationOutput=Get-AppearanceThemeRenderPlan $Name $Definition $Config -ValidateOnly}catch{$validationFailure=$_.Exception.Message}
  Assert ($null -eq $validationOutput -and $validationFailure -ceq $failure) ('One-pass validation reports the same error and never emits a plan: '+$Label)
  Assert ($sourceBefore -ceq (($Definition|ConvertTo-Json -Depth 64 -Compress)+'|'+($Config|ConvertTo-Json -Depth 64 -Compress))) ('Planning preserves both parsed inputs: '+$Label)
  if(!$failure){Assert-MenuProjection $plan $Name $Definition $Config $Label}
  $outcomes.Add([pscustomobject]@{name=$Label;error=$failure;plan=$plan})
 }
 foreach($variant in @('null','scalar','integer','boolean','nested','too-many','empty','one','sixteen')){
  $j=Copy-Json $defined['Round'];$v=$null
  switch($variant){null {$v=$null};scalar {$v='SP'};integer {$v=@('SP',42)};boolean {$v=@('SP',$true)};nested {$v=@(@('SP','MainMenu'),42)};too-many {$v=@(1..17|ForEach-Object{'SP'})};empty {$v=@()};one {$v=@('SP')};sixteen {$v=@(1..16|ForEach-Object{'SP'})}}
  $j.inject.'shared.css'=$v;$ExpectedFailure=if($variant -in @('empty','one','sixteen')){''}else{'render-inject-targets-invalid'}
  Observe-LoopPlan 'Round' $j $configs['Round'] ('file-'+$variant) $ExpectedFailure
 }
 foreach($variant in @('null','scalar','one','integer','boolean','nested','too-many','sixteen','first-not-target','case-sensitive')){
  $j=Copy-Json $defined['Art Hero'];$v=$null
  switch($variant){null {$v=$null};scalar {$v='1'};one {$v=@('1')};integer {$v=@('1',42)};boolean {$v=@('1',$false)};nested {$v=@('1',@('SP','MainMenu'))};too-many {$v=@('1')+@(1..16|ForEach-Object{'SP'})};sixteen {$v=@('1')+@(1..15|ForEach-Object{'SP'})};first-not-target {$v=@('SP','NO-TARGET')};case-sensitive {$v=@('1','sp','mainmenu','quickaccess')}}
  $j.inject|Add-Member -NotePropertyName '--met-hero-art-z-order' -NotePropertyValue $v -Force
  $ExpectedFailure=if($variant -in @('sixteen','first-not-target','case-sensitive')){''}else{'render-variable-definition-invalid'}
  Observe-LoopPlan 'Art Hero' $j $configs['Art Hero'] ('variable-'+$variant) $ExpectedFailure
 }
 $j=Copy-Json $defined['Art Hero']
 $j.inject|Add-Member -NotePropertyName '--met-hero-art-z-order' -NotePropertyValue @('1','SP','MainMenu',$false) -Force
 Observe-LoopPlan 'Art Hero' $j $configs['Art Hero'] 'variable-checks-destinations-after-match' 'render-variable-definition-invalid'
 $j.inject.'--met-hero-art-z-order'=@('1;','SP','MainMenu',$false)
 Observe-LoopPlan 'Art Hero' $j $configs['Art Hero'] 'variable-destination-error-precedes-value-error' 'render-variable-definition-invalid'
 $j.inject.'--met-hero-art-z-order'=@('1;','SP','MainMenu','QuickAccess')
 Observe-LoopPlan 'Art Hero' $j $configs['Art Hero'] 'variable-rejects-invalid-value-with-valid-destinations' 'render-variable-value-invalid'
 foreach($variant in @('integer','boolean','missing','wrong-case','default')){
  $c=Copy-Json $configs['Round'];switch($variant){integer {$c.Intensity=10};boolean {$c.Intensity=$true};missing {$c.Intensity='unknown'};wrong-case {$c.Intensity='10PX'};default {$c.PSObject.Properties.Remove('Intensity')}}
  Observe-LoopPlan 'Round' $defined['Round'] $c ('selection-'+$variant) $(if($variant -eq 'default'){''}else{'render-patch-selection-invalid'})
 }
 foreach($document in @('SP','MainMenu','QuickAccess')){
  $j=Copy-Json $defined['Mini Carousel'];$c=Copy-Json $configs['Mini Carousel']
  $j.inject|Add-Member -NotePropertyName '--scale-value' -NotePropertyValue @('0.8',$document)
  $j.patches.Size.values.'0.7'.'--scale-value'=@('0.7',$document)
  Observe-LoopPlan 'Mini Carousel' $j $c ('conflict-'+$document) 'render-variable-selection-conflict'
  $j.patches.Size.values.'0.7'.'--scale-value'=@('0.8',$document)
  Observe-LoopPlan 'Mini Carousel' $j $c ('same-value-'+$document)
  $j.patches.Size.values.'0.7'.'--scale-value'=@('0.7',$document)
  $c.active=$false
  Observe-LoopPlan 'Mini Carousel' $j $c ('inactive-'+$document)
  if($document -cne 'SP'){
   $c.active=$true
   $j.patches.Size.values.'0.9'.'nine.css'=@($false)
   Observe-LoopPlan 'Mini Carousel' $j $c ('invalid-entry-before-menu-diagnostic-'+$document) 'render-inject-targets-invalid'
  }
 }
 $j=Copy-Json $defined['Mini Carousel'];$c=Copy-Json $configs['Mini Carousel']
 $j.patches.Size.values.'0.7'.'--scale-value'=@('0.9','MainMenu','quickaccess')
 $j.patches.Size.values.'0.8'.'--scale-value'=@('0.8','QuickAccess')
 Observe-LoopPlan 'Mini Carousel' $j $c 'selected-main-menu-and-unselected-quick-access-variable'
 $plan=$outcomes[$outcomes.Count-1].plan
 Assert ($plan.variables[0].value -ceq '0.7' -and $plan.contexts.MainMenu.variables[0].value -ceq '0.9' -and
         $plan.contexts.QuickAccess.variables[0].enabled -and $plan.contexts.QuickAccess.variables[0].value -ceq '0.7') 'Menu override is isolated; an unselected QuickAccess option contributes only the static default; target casing is exact'
 $j=Copy-Json $defined['Round'];$c=Copy-Json $configs['Round']
 $j.inject|Add-Member -NotePropertyName 'Intensity\10PX.CSS' -NotePropertyValue @('QuickAccess')
 $j.patches.Intensity.values.'10px'.'intensity/10px.css'=@('MainMenu','sp')
 Observe-LoopPlan 'Round' $j $c 'case-and-path-equivalent-file'
 $plan=$outcomes[$outcomes.Count-1].plan
 Assert (@($plan.files|Where-Object {$_.path -ceq 'Intensity/10PX.CSS' -and !$_.enabled}).Count -eq 1 -and
         @($plan.contexts.MainMenu.files|Where-Object {$_.path -ceq 'Intensity/10PX.CSS' -and $_.enabled}).Count -eq 1 -and
         @($plan.contexts.QuickAccess.files|Where-Object {$_.path -ceq 'Intensity/10PX.CSS' -and $_.enabled}).Count -eq 1) 'Equivalent paths share one row while selections still depend on each exact document name'
 [pscustomobject]@{ok=$true;selectedCase=$Case;passed=$cases.Count;cases=@($cases);outcomes=$outcomes.ToArray();realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 64|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
 Write-Output "PASS: $($cases.Count) original render validation loop boundary checks"
 return
}

$rows=@();$defined=@{};$configurations=@{};$all=@{}
foreach($theme in $manifest.themes){
 $name=$theme.name;$folder=Join-Path (Join-Path $pack 'themes') $name
 $j=Get-Content -LiteralPath (Join-Path $folder 'theme.json') -Raw|ConvertFrom-Json;$c=Get-Content -LiteralPath (Join-Path $folder 'config_USER.json') -Raw|ConvertFrom-Json
 $defined[$name]=$j;$configurations[$name]=$c
 $plan=Get-AppearanceThemeRenderPlan $name $j $c
 Assert-MenuProjection $plan $name $j $c ($name+' factory')
 $rows+=[pscustomobject]@{name=$name;installed=$true;configured=[bool]$c.active;error='';renderPlan=$plan}
 foreach($file in $plan.files){$key=$name+'/'+$file.path;if($all.ContainsKey($key)){throw 'duplicate plan file'};$all[$key]=$file.property}
}
$expect=@(Get-AppearanceRenderExpectation ([pscustomobject]@{themes=$rows}))
$css=@($manifest.files|Where-Object {$_.path.EndsWith('.css',[StringComparison]::OrdinalIgnoreCase)})
Assert ($all.Count -eq 43 -and $css.Count -eq 43 -and @($expect|Where-Object kind -eq file).Count -eq 43) 'Every one of the 43 bundled static CSS files appears exactly once in the render plan'
Assert (@($expect|Where-Object {$_.kind -eq 'file' -and $_.enabled}).Count -eq 15) 'The exact factory configuration selects 15 shared and optional files'
foreach($file in $css){
 Assert ($all.ContainsKey([string]$file.path)) ('Bundled CSS is declared by a main-page injection: '+$file.path)
 $text=[IO.File]::ReadAllText((Join-Path (Join-Path $pack 'themes') $file.path));$property=$all[[string]$file.path]
 Assert ($text -match ([regex]::Escape($property)+':\s*1\s*;')) ('CSS carries the exact independently hashed receipt: '+$file.path)
}
Assert (@($expect|Where-Object kind -eq variable).Count -eq 14) 'Every managed dynamic variable is represented once alongside the 43 files'
Assert ((ConvertTo-AppearanceVariableValue '.3') -ceq (ConvertTo-AppearanceVariableValue '0.30')) 'Equivalent scalar token spellings compare equally'
Assert ((ConvertTo-AppearanceVariableValue '-0vh') -ceq (ConvertTo-AppearanceVariableValue '0vh')) 'Equivalent signed zero dimensions compare equally'
Assert ((ConvertTo-AppearanceVariableValue '#FFFFFF00   60%') -ceq '#ffffff00 60%') 'Gradient whitespace and hexadecimal case normalize without changing meaning'
$variantCount=0
foreach($theme in $manifest.themes){
 $name=$theme.name;$j=$defined[$name];$c=$configurations[$name]
 foreach($patch in $j.patches.PSObject.Properties){
  foreach($option in $patch.Value.values.PSObject.Properties){
   $variant=Copy-Json $c;$variant|Add-Member -NotePropertyName $patch.Name -NotePropertyValue $option.Name -Force
   $plan=Get-AppearanceThemeRenderPlan $name $j $variant
   Assert-MenuProjection $plan $name $j $variant ($name+' / '+$patch.Name+'='+$option.Name)
   $active=@($plan.files|Where-Object enabled|ForEach-Object path)
   foreach($item in $option.Value.PSObject.Properties){if(!$item.Name.StartsWith('--') -and 'SP' -cin @($item.Value)){Assert ($item.Name.Replace('\','/') -cin $active) ($name+' / '+$patch.Name+'='+$option.Name+' selects '+$item.Name)}}
   foreach($item in $option.Value.PSObject.Properties){if($item.Name.StartsWith('--') -and 'SP' -cin @($item.Value|Select-Object -Skip 1)){Assert (@($plan.variables|Where-Object {$_.property -ceq $item.Name -and $_.enabled -and $_.value -ceq $item.Value[0]}).Count -eq 1) ($name+' / '+$patch.Name+'='+$option.Name+' selects dynamic '+$item.Name)}}
   $variantCount++
  }
 }
 $off=Copy-Json $c;$off.active=$false;$plan=Get-AppearanceThemeRenderPlan $name $j $off
 Assert-MenuProjection $plan $name $j $off ($name+' off')
 Assert (@($plan.files|Where-Object enabled).Count -eq 0) ('OFF expects no stale shared or optional file for '+$name)
 Assert (@($plan.variables|Where-Object {$_.enabled -or $_.value -cne ''}).Count -eq 0) ('OFF expects no retained dynamic variable override for '+$name)
}
$round=Copy-Json $configurations['Round'];$round.PSObject.Properties.Remove('Intensity');$plan=Get-AppearanceThemeRenderPlan 'Round' $defined['Round'] $round
Assert (@($plan.files|Where-Object {$_.path -eq 'intensity/10px.css' -and $_.enabled}).Count -eq 1) 'An absent parameter uses its declared default without changing user configuration'
$round=Copy-Json $configurations['Round'];$round.Intensity='30px';$plan=Get-AppearanceThemeRenderPlan 'Round' $defined['Round'] $round
Assert (@($plan.files|Where-Object {$_.path -eq 'intensity/30px.css' -and $_.enabled}).Count -eq 1 -and @($plan.files|Where-Object {$_.path -eq 'intensity/10px.css' -and $_.enabled}).Count -eq 0) 'Changing one option expects its new file and explicitly rejects the old option residue'
$round.Intensity='unknown';Assert (Reject {Get-AppearanceThemeRenderPlan 'Round' $defined['Round'] $round}) 'Unknown configured option is unconfirmed and never silently rewritten'
Assert (Reject {Get-AppearanceFileReceipt 'Round' '../shared.css'}) 'Receipt paths cannot escape a managed theme'
Assert (Reject {Get-AppearanceFileReceipt 'Round' 'C:\arbitrary.css'}) 'Absolute paths cannot enter a receipt plan'
Assert (Reject {Get-AppearanceFileReceipt 'Unknown Theme' 'shared.css'}) 'Unmanaged themes never become automatic correction targets'
$bad=Copy-Json $defined['Round'];$bad.inject|Add-Member -NotePropertyName '../outside.css' -NotePropertyValue @('SP')
Assert (Reject {Get-AppearanceThemeRenderPlan 'Round' $bad $configurations['Round']}) 'Malformed injection paths are rejected while planning before file access or CDP'
$badRows=Copy-Json $rows;$badRows[0].renderPlan.files[0].property='--arbitrary'
Assert (Reject {Get-AppearanceRenderExpectation ([pscustomobject]@{themes=@($badRows)})}) 'Forged properties cannot enter the page expression'
$badRows=Copy-Json $rows;$badRows[0].renderPlan.files+=@($badRows[0].renderPlan.files[0])
Assert (Reject {Get-AppearanceRenderExpectation ([pscustomobject]@{themes=@($badRows)})}) 'Duplicate receipt entries are rejected before querying the renderer'
$hero=Get-AppearanceThemeRenderPlan 'Art Hero' $defined['Art Hero'] $configurations['Art Hero']
Assert (@($hero.variables|Where-Object {$_.property -ceq '--met-hero-art-z-order' -and $_.value -ceq '1'}).Count -eq 1) 'A selected option with no dynamic override expects the canonical shared fallback'
Assert (@($hero.variables|Where-Object {$_.property -ceq '--met-game-carousel-font-height' -and $_.value -ceq '-3.5em'}).Count -eq 1) 'Returning to default removes the previous dynamic font-height override'
foreach($mutation in @('missing','duplicate','unknown','unsafe','off-stale','old-protocol')){
 $badRows=Copy-Json $rows;$dynamic=$badRows|Where-Object name -eq 'Mini Carousel'
 switch($mutation){
  missing {$dynamic.renderPlan.variables=@()}
  duplicate {$dynamic.renderPlan.variables+=@($dynamic.renderPlan.variables[0])}
  unknown {$dynamic.renderPlan.variables[0].property='--foreign-variable'}
  unsafe {$dynamic.renderPlan.variables[0].value='0.5; color:red'}
  off-stale {$dynamic.configured=$false;foreach($f in $dynamic.renderPlan.files){$f.enabled=$false};$dynamic.renderPlan.variables[0].enabled=$false}
  old-protocol {$dynamic.renderPlan.protocol=2}
 }
 Assert (Reject {Get-AppearanceRenderExpectation ([pscustomobject]@{themes=@($badRows)})}) ('Unsafe or incomplete variable plans are refused: '+$mutation)
}
Assert (@($expect|Where-Object kind -eq content).Count -eq 43) 'Every file has an independent exact-content expectation'
$indexBefore=Get-AppearanceContentIndex
$watchIndex=Get-AppearanceContentIndex
Assert ([object]::ReferenceEquals($indexBefore,$watchIndex)) 'Repeated plans reuse the same validated in-memory manifest index'
$documentStatus=[pscustomobject]@{themes=$rows;revision=('a'*64);targetId=('b'*16)}
$memo=@{}
foreach($row in $rows){$memo[$row.name]=@{index=$indexBefore;plan=$row.renderPlan}}
$compiled=Get-AppearanceDocumentPlan $documentStatus $indexBefore $memo
Assert ([object]::ReferenceEquals($compiled,(Get-AppearanceDocumentPlan $documentStatus $indexBefore $memo))) 'Unchanged automatic plans reuse the same compiled document query'
Assert (![object]::ReferenceEquals($compiled,(Get-AppearanceDocumentPlan $documentStatus $indexBefore))) 'An independent request does not inherit the automatic cache'
$replacement=Copy-Json $documentStatus
($replacement.themes|Where-Object name -eq Round).renderPlan.contextProtocol=2
Assert (Reject {Get-AppearanceDocumentPlan $replacement $indexBefore $memo}) 'A substituted plan cannot inherit a compiled query even with the same revision'
$reordered=[pscustomobject]@{themes=@($rows);revision=$documentStatus.revision;targetId=$documentStatus.targetId}
[array]::Reverse($reordered.themes)
$recompiled=Get-AppearanceDocumentPlan $reordered $indexBefore $memo
Assert (![object]::ReferenceEquals($compiled,$recompiled)) 'Changed row order is not borrowed from an older status'
Assert (($compiled.plans|ConvertTo-Json -Depth 20 -Compress) -ceq ($recompiled.plans|ConvertTo-Json -Depth 20 -Compress)) 'Rebuilding for a different row order retains the same three document requests'
foreach($mutation in @('missing-content','invalid-content','duplicate-content','old-v3')){
 $badRows=Copy-Json $rows;$round=$badRows|Where-Object name -eq Round
 switch($mutation){
  missing-content {$round.renderPlan.files[0].PSObject.Properties.Remove('contentProperty')}
  invalid-content {$round.renderPlan.files[0].contentProperty='--foreign-content'}
  duplicate-content {$round.renderPlan.files[1].contentProperty=$round.renderPlan.files[0].contentProperty}
  old-v3 {$round.renderPlan.protocol=3}
 }
 Assert (Reject {Get-AppearanceRenderExpectation ([pscustomobject]@{themes=$badRows})}) ('Missing or unsafe exact-content plan is refused: '+$mutation)
}
foreach($good in @([int]0,[long]0,[int]16384,[long]16384)){Assert (Test-AppearanceReceiptInteger $good 0 16384) ('Bounded native JSON integer accepted: '+$good.GetType().Name)}
foreach($badInteger in @($true,$false,[decimal]1,[double]1,'1',[long]16385,[long]-1,$null)){Assert (!(Test-AppearanceReceiptInteger $badInteger 0 16384)) 'Boolean, noninteger and out-of-range DOM fields remain invalid'}
# A whole three-document plan retains its exact results with one pure hash
# per distinct normalized file shared by the three targets in this invocation.
$script:ReceiptCalls=0
$script:OriginalReceiptBody=(Get-Item Function:\Get-AppearanceFileReceipt).ScriptBlock
function Get-AppearanceFileReceipt([string]$Name,[string]$Relative){$script:ReceiptCalls++;& $script:OriginalReceiptBody $Name $Relative}
try {
 foreach($theme in $manifest.themes){
  $name=$theme.name;$script:ReceiptCalls=0
  $fresh=Get-AppearanceThemeRenderPlan $name $defined[$name] $configurations[$name]
  Assert ($script:ReceiptCalls -eq @($fresh.files).Count) ('One validated file receipt per distinct path across all targets: '+$name)
  $prior=@($rows|Where-Object name -ceq $name)[0].renderPlan
  Assert (($fresh|ConvertTo-Json -Depth 32 -Compress) -ceq ($prior|ConvertTo-Json -Depth 32 -Compress)) ('Receipt reuse keeps the complete three-document plan exact: '+$name)
 }
 $bad=Copy-Json $defined['Round'];$bad.inject|Add-Member -NotePropertyName 'shared.css/../shared.css' -NotePropertyValue @('SP')
 Assert (Reject {Get-AppearanceThemeRenderPlan 'Round' $bad $configurations['Round']}) 'A repeated basename cannot bypass normalized path validation'
 $bad=Copy-Json $defined['Round'];$bad.patches|Add-Member -NotePropertyName 'ReceiptDuplicateTargetCheck' -NotePropertyValue ([pscustomobject]@{default='Default';values=[pscustomobject]@{Default=[pscustomobject]@{'SHARED.CSS'=@($true)}}})
 Assert (Reject {Get-AppearanceThemeRenderPlan 'Round' $bad $configurations['Round']}) 'Repeated case-equivalent file still validates every target array'
}finally{Set-Item Function:\Get-AppearanceFileReceipt -Value $script:OriginalReceiptBody}

$watch=[Diagnostics.Stopwatch]::StartNew();for($n=0;$n -lt 5;$n++){foreach($theme in $manifest.themes){$null=Get-AppearanceThemeRenderPlan $theme.name $defined[$theme.name] $configurations[$theme.name]}};$watch.Stop()
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);patchValuesExercised=$variantCount;bundledCssFiles=$css.Count;activeDefaultFiles=@($expect|Where-Object enabled).Count;fiveFullPlanPassesMs=$watch.ElapsedMilliseconds;realSteamOperations=0;realCssOperations=0;extraCssResourceReadsDuringPlanning=0;contentIndexSource='One small manifest cached per identity; no CSS file reads during planning';scope='Exact production planning from parsed source manifests/configs; no physical Steam or CSS process'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) file render planning checks across $variantCount patch values"
