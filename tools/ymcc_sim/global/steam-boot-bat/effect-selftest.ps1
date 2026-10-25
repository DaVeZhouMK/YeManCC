param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$text=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###';$body=$text.Substring($text.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors)
foreach($name in @('Close-BootXbox','Write-BootLog','Start-BootYMCC','Get-BootAddons','Start-BootAddon','Request-Steam')){
 $fn=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false))[0];. ([scriptblock]::Create($fn.Extent.Text))
}
$script:session=42;$script:lastClose=@{};$script:calls=@();$script:logs=@();$script:code=0;$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw $Message}}
function Write-BootLog {param($E,$D)$script:logs+=($E+':'+$D)}
function Start-Quiet {param($Exe,$Arguments,[switch]$Capture)$script:calls+=[pscustomobject]@{exe=$Exe;arguments=$Arguments;capture=[bool]$Capture};if($Capture){[pscustomobject]@{Code=$script:code;Text=$(if($script:code){'Access is denied'}else{'SUCCESS'})}}}
# Xbox assertions moved to cleanup-selftest.ps1 and exact native-terminal gate tests.
$script:steamExe='C:\inert only\Steam.exe';$script:calls=@();Request-Steam $false;Request-Steam $true
Assert ($script:calls.Count -eq 2) 'two explicit Steam test requests'
Assert ($script:calls[0].arguments -eq '-bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui') 'original Steam cold flags preserved'
Assert ($script:calls[1].arguments -eq 'steam://open/gamepadui') 'existing Steam request retains original URI'
$script:calls=@();$script:steamExe='';Request-Steam $false
Assert ($script:calls.Count -eq 0) 'missing install path no arbitrary fallback'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual unchanged Steam request';effects='memory-only';targetApplicationsStartedOrStopped=$false}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'effect-result.json')