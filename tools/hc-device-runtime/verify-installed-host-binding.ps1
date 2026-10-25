function Assert-YmccInstalledHostBinding {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$ExpectedHostSha256,[Parameter(Mandatory=$true)][string]$ExpectedRuntimeManifestSha256)
 $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
 function BoundPath([string]$Relative){
  if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'HOST_BINDING_UNSAFE_PATH'}
  $file=[IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
  if(-not $file.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'HOST_BINDING_PATH_ESCAPE'}
  $cursor=$file
  while($cursor.Length -ge $rootPath.Length){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'HOST_BINDING_REPARSE_PATH'}};if($cursor -ieq $rootPath){break};$cursor=Split-Path -Parent $cursor}
  return $file
 }
 function BoundHash([string]$Relative){(Get-FileHash -LiteralPath (BoundPath $Relative) -Algorithm SHA256).Hash.ToLowerInvariant()}
 $update=Get-Content -LiteralPath (BoundPath 'YeManCC/update-manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $binding=$update.rules.fanHostPayload
 if($binding.schemaVersion -ne 2 -or $binding.directory -cne 'fan-host-v2' -or $binding.fileCount -ne 9){throw 'HOST_BINDING_SCHEMA_MISMATCH'}
 $runtimeRelative='PowerControl/'+$binding.runtimeDirectory.Replace('\','/')
 $runtimeManifestRelative=$runtimeRelative+'/HandheldCompanion.runtime.json'
 if((BoundHash $runtimeManifestRelative) -cne $ExpectedRuntimeManifestSha256.ToLowerInvariant() -or $binding.runtimeManifestSha256.ToLowerInvariant() -cne $ExpectedRuntimeManifestSha256.ToLowerInvariant()){throw 'HOST_BINDING_RUNTIME_MANIFEST_MISMATCH'}
 $prefix='PowerControl/fan-host-v2/';$manifestRelative=$prefix+'YeManFanHost.payload.json'
 $manifestHash=BoundHash $manifestRelative
 if($manifestHash -cne $binding.manifestSha256.ToLowerInvariant()){throw 'HOST_BINDING_PAYLOAD_MANIFEST_MISMATCH'}
 $runtime=Get-Content -LiteralPath (BoundPath $runtimeManifestRelative) -Raw -Encoding UTF8|ConvertFrom-Json
 $manifest=Get-Content -LiteralPath (BoundPath $manifestRelative) -Raw -Encoding UTF8|ConvertFrom-Json
 if($manifest.schemaVersion -ne 2 -or $manifest.runtimeId -cne $runtime.runtimeId -or @($manifest.files).Count -ne $binding.fileCount -or @($manifest.files.path|Select-Object -Unique).Count -ne $binding.fileCount){throw 'HOST_BINDING_PAYLOAD_CLOSURE_MISMATCH'}
 $linked=[IO.Path]::GetFullPath((Join-Path (BoundPath 'PowerControl/fan-host-v2') $manifest.runtimeManifest))
 if($linked -ine (BoundPath $runtimeManifestRelative)){throw 'HOST_BINDING_RUNTIME_PATH_MISMATCH'}
 $hostHash=BoundHash ($prefix+'YeManFanHost.dll')
 if($hostHash -cne $ExpectedHostSha256.ToLowerInvariant()){throw 'HOST_BINDING_HOST_IDENTITY_MISMATCH'}
 foreach($row in $manifest.files){if((BoundHash ($prefix+$row.path)) -cne $row.sha256.ToLowerInvariant()){throw ('HOST_BINDING_MEMBER_HASH_MISMATCH: '+$row.path)}}
 $auth=Get-Content -LiteralPath (BoundPath ($prefix+'YeManFanHost.authorization.md')) -Raw -Encoding UTF8
 foreach($pair in @(@('approvedHostDllSha256',$hostHash),@('approvedHostExeSha256',(BoundHash ($prefix+'YeManFanHost.exe'))),@('approvedHcSha256',(BoundHash ($runtimeRelative+'/HandheldCompanion.dll'))))){
  $found=[regex]::Matches($auth,'(?m)^'+[regex]::Escape($pair[0])+':\s*([a-fA-F0-9]{64})\s*$')
  if($found.Count -ne 1 -or $found[0].Groups[1].Value.ToLowerInvariant() -cne $pair[1]){throw ('HOST_BINDING_AUTHORIZATION_MISMATCH: '+$pair[0])}
 }
 $export=Get-Content -LiteralPath (BoundPath 'YeManCC/export-identity.json') -Raw -Encoding UTF8|ConvertFrom-Json
 foreach($relative in @('YeManCC/update-manifest.json',$manifestRelative,$runtimeManifestRelative)+@($manifest.files|ForEach-Object {$prefix+$_.path})){
  $rows=@($export.payload|Where-Object {$_.path -ceq $relative})
  if($rows.Count -ne 1 -or $rows[0].sha256.ToLowerInvariant() -cne (BoundHash $relative) -or $rows[0].bytes -ne (Get-Item -LiteralPath (BoundPath $relative)).Length){throw ('HOST_BINDING_EXPORT_ROW_MISMATCH: '+$relative)}
 }
 return [pscustomobject]@{status='PASS';hostDllSha256=$hostHash;hostManifestSha256=$manifestHash;runtimeManifestSha256=$ExpectedRuntimeManifestSha256.ToLowerInvariant();hostFiles=$binding.fileCount;authorizationVerified=$true;updateBindingVerified=$true;exportRowsVerified=$true;hardwareIo=$false}
}