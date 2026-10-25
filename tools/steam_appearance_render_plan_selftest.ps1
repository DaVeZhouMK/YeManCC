[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $repo 'PowerControl\steam-appearance\idle.ps1')
$pack=Join-Path $repo 'PowerControl\steam-appearance'
$manifest=Get-Content -LiteralPath (Join-Path $pack 'manifest.json') -Raw|ConvertFrom-Json
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Label){if(!$Value){throw $Label};$cases.Add($Label)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
function Copy-Json($Value){$Value|ConvertTo-Json -Depth 32 -Compress|ConvertFrom-Json}
$rows=@();$defined=@{};$configurations=@{};$all=@{}
foreach($theme in $manifest.themes){
 $name=$theme.name;$folder=Join-Path (Join-Path $pack 'themes') $name
 $j=Get-Content -LiteralPath (Join-Path $folder 'theme.json') -Raw|ConvertFrom-Json;$c=Get-Content -LiteralPath (Join-Path $folder 'config_USER.json') -Raw|ConvertFrom-Json
 $defined[$name]=$j;$configurations[$name]=$c
 $plan=Get-AppearanceThemeRenderPlan $name $j $c
 $rows+=[pscustomobject]@{name=$name;installed=$true;configured=[bool]$c.active;error='';renderPlan=$plan}
 foreach($file in $plan.files){$key=$name+'/'+$file.path;if($all.ContainsKey($key)){throw 'duplicate plan file'};$all[$key]=$file.property}
}
$expect=@(Get-AppearanceRenderExpectation ([pscustomobject]@{themes=$rows}))
$css=@($manifest.files|Where-Object {$_.path.EndsWith('.css',[StringComparison]::OrdinalIgnoreCase)})
Assert ($all.Count -eq 44 -and $css.Count -eq 44 -and @($expect|Where-Object kind -eq file).Count -eq 44) 'Every one of the 44 bundled static CSS files appears exactly once in the render plan'
Assert (@($expect|Where-Object {$_.kind -eq 'file' -and $_.enabled}).Count -eq 16) 'The exact factory configuration selects 16 shared and optional files'
foreach($file in $css){
 Assert ($all.ContainsKey([string]$file.path)) ('Bundled CSS is declared by a main-page injection: '+$file.path)
 $text=[IO.File]::ReadAllText((Join-Path (Join-Path $pack 'themes') $file.path));$property=$all[[string]$file.path]
 Assert ($text -match ([regex]::Escape($property)+':\s*1\s*;')) ('CSS carries the exact independently hashed receipt: '+$file.path)
}
Assert (@($expect|Where-Object kind -eq variable).Count -eq 18) 'Every managed dynamic variable is represented once alongside the 44 files'
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
   $active=@($plan.files|Where-Object enabled|ForEach-Object path)
   foreach($item in $option.Value.PSObject.Properties){if(!$item.Name.StartsWith('--') -and 'SP' -cin @($item.Value)){Assert ($item.Name.Replace('\','/') -cin $active) ($name+' / '+$patch.Name+'='+$option.Name+' selects '+$item.Name)}}
   foreach($item in $option.Value.PSObject.Properties){if($item.Name.StartsWith('--') -and 'SP' -cin @($item.Value|Select-Object -Skip 1)){Assert (@($plan.variables|Where-Object {$_.property -ceq $item.Name -and $_.enabled -and $_.value -ceq $item.Value[0]}).Count -eq 1) ($name+' / '+$patch.Name+'='+$option.Name+' selects dynamic '+$item.Name)}}
   $variantCount++
  }
 }
 $off=Copy-Json $c;$off.active=$false;$plan=Get-AppearanceThemeRenderPlan $name $j $off
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
Assert (@($expect|Where-Object kind -eq content).Count -eq 44) 'Every file has an independent exact-content expectation'
$indexBefore=Get-AppearanceContentIndex
$watchIndex=Get-AppearanceContentIndex
Assert ([object]::ReferenceEquals($indexBefore,$watchIndex)) 'Repeated plans reuse the same validated in-memory manifest index'
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
$watch=[Diagnostics.Stopwatch]::StartNew();for($n=0;$n -lt 5;$n++){foreach($theme in $manifest.themes){$null=Get-AppearanceThemeRenderPlan $theme.name $defined[$theme.name] $configurations[$theme.name]}};$watch.Stop()
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);patchValuesExercised=$variantCount;bundledCssFiles=$css.Count;activeDefaultFiles=@($expect|Where-Object enabled).Count;fiveFullPlanPassesMs=$watch.ElapsedMilliseconds;realSteamOperations=0;realCssOperations=0;extraCssResourceReadsDuringPlanning=0;contentIndexSource='One small manifest cached per identity; no CSS file reads during planning';scope='Exact production planning from parsed source manifests/configs; no physical Steam or CSS process'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) file render planning checks across $variantCount patch values"
