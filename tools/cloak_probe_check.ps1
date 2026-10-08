$ErrorActionPreference = 'Stop'
$cli = 'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'
Write-Output ("CLI exists: " + (Test-Path -LiteralPath $cli))
$out = & $cli --dev-all 2>&1 | Out-String
$matches = $out -split "`n" | Select-String -Pattern '045E','028E','Flydigi'
foreach ($m in $matches) { Write-Output $m.Line }