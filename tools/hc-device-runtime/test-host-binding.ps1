[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$StageRoot,[Parameter(Mandatory=$true)][string]$EvidenceRoot,[Parameter(Mandatory=$true)][string]$ExpectedHostSha256,[Parameter(Mandatory=$true)][string]$ExpectedRuntimeManifestSha256)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'verify-installed-host-binding.ps1')
$root=[IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
if(Test-Path -LiteralPath $root){throw 'Fresh binding test evidence required'}
[IO.Directory]::CreateDirectory($root)|Out-Null
$fixture=Join-Path $root 'fixture';Copy-Item -LiteralPath $StageRoot -Destination $fixture -Recurse
function File([string]$Rel){Join-Path $fixture $Rel}
function Hash([string]$Rel){(Get-FileHash -LiteralPath (File $Rel) -Algorithm SHA256).Hash.ToLowerInvariant()}
function ReadJson([string]$Rel){Get-Content -LiteralPath (File $Rel) -Raw -Encoding UTF8|ConvertFrom-Json}
function WriteJson([string]$Rel,$Value){[IO.File]::WriteAllText((File $Rel),($Value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
function Verify {Assert-YmccInstalledHostBinding -Root $fixture -ExpectedHostSha256 $ExpectedHostSha256 -ExpectedRuntimeManifestSha256 $ExpectedRuntimeManifestSha256}
$rows=[Collections.Generic.List[object]]::new();$backups=@{}
$names=@('YeManCC/update-manifest.json','YeManCC/export-identity.json','PowerControl/fan-host-v2/YeManFanHost.authorization.md','PowerControl/fan-host-v2/YeManFanHost.payload.json','PowerControl/fan-host-v2/YeManFanBackend.dll')
foreach($rel in $names){$backups[$rel]=[IO.File]::ReadAllBytes((File $rel))}
function Restore {foreach($rel in $names){[IO.File]::WriteAllBytes((File $rel),$backups[$rel])}}
function Reject([string]$Name,[string]$Reason,[scriptblock]$Mutation){
 Restore
 & $Mutation
 $errorText=$null
 try{Verify|Out-Null}catch{$errorText=$_.Exception.Message}
 $pass=$errorText -and $errorText.Contains($Reason)
 $rows.Add([pscustomobject]@{name=$Name;passed=[bool]$pass;observed=$errorText})
 if(-not $pass){throw ('BINDING_NEGATIVE_FAILED: '+$Name+': '+$errorText)}
}
$v=Verify;$rows.Add([pscustomobject]@{name='complete current chain';passed=($v.status -eq 'PASS')})
Reject 'actual previous runtime update pin85d rejected with runtime6389' 'HOST_BINDING_RUNTIME_MANIFEST_MISMATCH' {
 $u=ReadJson 'YeManCC/update-manifest.json';$u.rules.fanHostPayload.runtimeManifestSha256='85D75DE442300296DE056B487D68D7DB9AB9123EFA5D51D5245E0C32B2DDF032';WriteJson 'YeManCC/update-manifest.json' $u
}
Reject 'old payload manifest binding rejected' 'HOST_BINDING_PAYLOAD_MANIFEST_MISMATCH' {
 $u=ReadJson 'YeManCC/update-manifest.json';$u.rules.fanHostPayload.manifestSha256='1E30910FD41F3480A7D5E6EEBCBFE7483CCA213F63D7CCFBEFF08A4473521506';WriteJson 'YeManCC/update-manifest.json' $u
}
Reject 'self-consistent hashes cannot approve old Host authorization' 'HOST_BINDING_AUTHORIZATION_MISMATCH' {
 $rel='PowerControl/fan-host-v2/YeManFanHost.authorization.md';$auth=[IO.File]::ReadAllText((File $rel));$auth=[regex]::Replace($auth,'(?m)^approvedHostDllSha256: [A-Fa-f0-9]{64}','approvedHostDllSha256: 5E94170105C65BB0503751460082DB2A0827061B075C55AFE92F7EF9B3B91993');[IO.File]::WriteAllText((File $rel),$auth,[Text.UTF8Encoding]::new($false))
 $m=ReadJson 'PowerControl/fan-host-v2/YeManFanHost.payload.json';($m.files|Where-Object path -eq 'YeManFanHost.authorization.md').sha256=Hash $rel;WriteJson 'PowerControl/fan-host-v2/YeManFanHost.payload.json' $m
 $u=ReadJson 'YeManCC/update-manifest.json';$u.rules.fanHostPayload.manifestSha256=Hash 'PowerControl/fan-host-v2/YeManFanHost.payload.json';WriteJson 'YeManCC/update-manifest.json' $u
}
Reject 'wrong Backend bytes rejected by member verification' 'HOST_BINDING_MEMBER_HASH_MISMATCH' {
 $rel='PowerControl/fan-host-v2/YeManFanBackend.dll';$bytes=[IO.File]::ReadAllBytes((File $rel));$bytes[-1]=$bytes[-1] -bxor 1;[IO.File]::WriteAllBytes((File $rel),$bytes)
}
Reject 'missing export inventory Host row rejected' 'HOST_BINDING_EXPORT_ROW_MISMATCH' {
 $e=ReadJson 'YeManCC/export-identity.json';$e.payload=@($e.payload|Where-Object path -ne 'PowerControl/fan-host-v2/YeManFanHost.dll');WriteJson 'YeManCC/export-identity.json' $e
}
Reject 'runtime traversal path rejected' 'HOST_BINDING_UNSAFE_PATH' {
 $u=ReadJson 'YeManCC/update-manifest.json';$u.rules.fanHostPayload.runtimeDirectory='../outside';WriteJson 'YeManCC/update-manifest.json' $u
}
Restore;$v=Verify;$rows.Add([pscustomobject]@{name='unmodified chain passes after all fault injections';passed=($v.status -eq 'PASS')})
$report=[ordered]@{status='PASS';checks=$rows;checksPassed=$rows.Count;hardwareIo=$false;hostStarted=$false;installedTreeWritten=$false}
[IO.File]::WriteAllText((Join-Path $root 'result.json'),($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ('HOST_BINDING_SELFTEST PASS '+$rows.Count)