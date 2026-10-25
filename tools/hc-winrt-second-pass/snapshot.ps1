[CmdletBinding()]
param([string]$RepositoryRoot='G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC',[string]$EvidenceRoot='D:/Codex/2026-10-18/kai-s/work/hc-third-trim/winrt')
$ErrorActionPreference='Stop'
$runtime=Join-Path $RepositoryRoot 'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'
$shared=Join-Path $RepositoryRoot 'deps/ymcc-shared-runtime'
$rows=@()
foreach($source in @(@{Kind='runtime';Root=$runtime;Destination=(Join-Path $EvidenceRoot 'baseline/runtime')},@{Kind='shared';Root=$shared;Destination=(Join-Path $EvidenceRoot 'baseline/shared')})) {
 foreach($file in Get-ChildItem -LiteralPath $source.Root -File -Recurse) {
  $rel=[IO.Path]::GetRelativePath($source.Root,$file.FullName)
  $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  $dst=Join-Path $source.Destination $rel
  New-Item -ItemType Directory -Path (Split-Path -Parent $dst) -Force | Out-Null
  if(Test-Path -LiteralPath $dst){if((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash){throw "Snapshot already differs: $rel"}}
  else {Copy-Item -LiteralPath $file.FullName -Destination $dst}
  $rows+=@{kind=$source.Kind;path=$rel;source=$file.FullName;sha256=$hash;bytes=$file.Length;snapshot=$dst}
 }
}
$sdk=@($rows | Where-Object { $_.kind -eq 'runtime' -and $_.path -eq 'Microsoft.Windows.SDK.NET.dll' })[0]
if($sdk.sha256 -ne '0da19d45d98c20b46b08ec282bd8a818e5600d482912e2289ba0da591e19b7e0' -or $sdk.bytes -ne 1568256){throw 'SDK baseline differs from user pin'}
$manifestChecks=@()
foreach($pair in @(@{Manifest=(Join-Path $runtime 'HandheldCompanion.runtime.json');Root=$runtime},@{Manifest=(Join-Path $shared 'manifest.json');Root=(Join-Path $shared 'bin')})){
 $m=Get-Content -LiteralPath $pair.Manifest -Raw | ConvertFrom-Json
 foreach($f in $m.files){$p=Join-Path $pair.Root ([IO.Path]::GetFileName($f.path)); if($pair.Root -eq $runtime){$p=Join-Path $pair.Root $f.path}; $actual=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant(); $manifestChecks+=@{path=$p;expected=$f.sha256;actual=$actual;passed=($actual -eq $f.sha256)}}
}
@{schema=1;created=(Get-Date -Format o);runtimeFiles=@($rows|Where-Object kind -eq 'runtime').Count;sharedBinFiles=@($rows|Where-Object { $_.kind -eq 'shared' -and $_.path -like 'bin*' }).Count;manifestChecks=$manifestChecks;files=$rows;networkUsed=$false} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'BASELINE-LOCK.json') -Encoding utf8
if(@($manifestChecks|Where-Object passed -eq $false).Count){throw 'Manifest mismatch; snapshot retained'}
Write-Output "Locked $(@($rows|Where-Object kind -eq 'runtime').Count) runtime files and $(@($rows|Where-Object { $_.kind -eq 'shared' -and $_.path -like 'bin*' }).Count) shared consumer roots"
