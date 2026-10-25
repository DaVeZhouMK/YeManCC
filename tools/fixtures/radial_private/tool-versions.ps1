param([Parameter(Mandatory=$true)][string]$Manifest,[Parameter(Mandatory=$true)][string]$Repo)
$ErrorActionPreference='Stop'
$data=Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$rows=foreach($name in @('compiler','linker','powershell','cmd','staticLoader')) {
 $selected=$data.selected.$name
 $file=if($name -eq 'staticLoader'){Join-Path $Repo $selected}else{$selected}
 $item=Get-Item -LiteralPath $file
 [pscustomobject]@{role=$name;path=$item.FullName;fileVersion=$item.VersionInfo.FileVersion;productVersion=$item.VersionInfo.ProductVersion;sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[pscustomobject]@{pass=$true;tools=$rows;msvcVersion=$data.selected.msvcVersion;sdkVersion=$data.selected.sdkVersion;webviewSdk=$data.webviewSdk;registryRead=$false} | ConvertTo-Json -Depth 8
