# Fresh compile directories plus diagnostic validation prevent an MSVC internal
# compiler error from publishing a stale EXE when the driver reports exit zero.
function Assert-CustomSteamLibraryCompilation {
  param(
    [int]$ExitCode,
    [string[]]$OutputLines,
    [string]$CompilationDirectory,
    [string[]]$ExpectedExecutables
  )
  $errors = @($OutputLines | Where-Object { $_ -match '(?i)\b(?:fatal\s+)?error\s+(?:C\d+|LNK\d+|RC\d+|MSB\d+|D\d+)\b' })
  if ($ExitCode -ne 0 -or $errors.Count) {
    throw "Custom Steam Library compiler failed (exit=$ExitCode). Refusing stale artifacts. $($errors -join '; ')"
  }
  foreach ($exe in $ExpectedExecutables) {
    if (-not (Test-Path -LiteralPath (Join-Path $CompilationDirectory $exe) -PathType Leaf)) {
      throw "Custom Steam Library compiler did not produce a fresh executable: $exe"
    }
  }
}

# Keep the actual compiler inputs (including dependencies/resources) rather than
# hashing whatever happens to be on disk after compilation. A mid-build edit
# must fail closed instead of blessing a stale executable with a new fingerprint.
function Get-CustomSteamLibrarySourceIndex {
  param([string]$ProjectRoot, [ValidateSet('host','worker')][string]$Kind)
  $native = Join-Path $ProjectRoot 'native\custom-steam-library'
  $stem = if ($Kind -eq 'host') { 'workspace_host' } else { 'steam_artwork_lab' }
  $paths = @(
    "native/custom-steam-library/$stem.cpp",
    "native/custom-steam-library/$stem.rc",
    "native/custom-steam-library/build_$Kind.bat",
    "tools/build-custom-steam-library-$Kind.ps1",
    'tools/custom_steam_library_build_guard.ps1',
    'CustomSteamLibrary/assets/custom-steam-library.ico',
    'native/game_detection_blacklist_entries.h',
    'native/process_efficiency.h',
    'deps/json/json.hpp'
  )
  $paths += @(Get-ChildItem -LiteralPath $native -Filter '*.h' -File | ForEach-Object { 'native/custom-steam-library/' + $_.Name })
  if ($Kind -eq 'host') {
    $paths += @('native/opti_catalog_startup.h','native/webview_process_efficiency.h','native/shared_game_media.h','native/shared_game_media_capacity.h',
      'native/shared_game_media_download_queue.h','native/shared_game_media_download_worker.h','native/shared_game_media_download_resources.h',
      'native/shared_game_media_shortcut.h','native/shared_game_media_identity_worker.h','native/shared_game_media_video_request.h',
      'deps/webview2/build/native/include/WebView2.h',
      'deps/webview2/build/native/include/WebView2EnvironmentOptions.h',
      'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')
  }
  foreach ($relative in @($paths | Sort-Object -Unique)) {
    $path = Join-Path $ProjectRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $Kind build input: $relative" }
    [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
  }
}

function Assert-CustomSteamLibrarySourceIndex {
  param([string]$ProjectRoot, [ValidateSet('host','worker')][string]$Kind, [object[]]$SourceIndex)
  $current = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $ProjectRoot -Kind $Kind)
  $declared = @{}
  foreach ($entry in $SourceIndex) {
    $key = [string]$entry.path
    if ($declared.ContainsKey($key) -or [string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$') {
      throw "Invalid/duplicate $Kind source fingerprint: $key"
    }
    $declared[$key] = [string]$entry.sha256
  }
  if ($current.Count -ne $declared.Count) { throw "$Kind source input coverage drifted; rebuild the child from current mainline" }
  foreach ($entry in $current) {
    if (-not $declared.ContainsKey($entry.path) -or $declared[$entry.path] -ine $entry.sha256) {
      throw "$Kind source/build drift: $($entry.path); rebuild the child from current mainline"
    }
  }
}

function Assert-CustomSteamLibraryMainlineIdentity {
  param([string]$ProjectRoot)
  $root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
  $gitRoot = [string](& git -C $root rev-parse --show-toplevel 2>$null)
  if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($gitRoot).TrimEnd('\') -ine $root) {
    throw 'Custom Steam Library export must come from its actual Git repository root'
  }
  $branch = [string](& git -C $root branch --show-current)
  # The ordinary exporter explicitly marks local test packages as unaccepted.
  # Preserve the actual Git-root and canonical Mainline-path checks below;
  # this exception never applies to full acceptance or GitHub release jobs.
  $localTestExport = $env:YEMAN_TEST_EXPORT -eq '1' -and $env:YEMAN_FULL_ACCEPTANCE -ne '1' -and $env:GITHUB_ACTIONS -ne 'true'
  if ($LASTEXITCODE -ne 0 -or ($branch -ne 'main' -and -not $localTestExport -and -not ($env:GITHUB_ACTIONS -eq 'true' -and -not $branch))) {
    throw "Custom Steam Library export requires main (or the release CI detached checkout), not $branch"
  }
  if ($env:GITHUB_ACTIONS -ne 'true' -and $root -notmatch '[\\/]Mainline[\\/]YeManCC-source[\\/]YeManCC$') {
    throw "Custom Steam Library export must use the canonical Mainline worktree: $root"
  }
}

function Assert-CustomSteamLibrarySourceFreshness {
  param([string]$ProjectRoot, [string]$PackageRoot)
  $manifestPath = Join-Path $PackageRoot 'package-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([int]$manifest.backgroundMediaProtocol -ne 1 -or [int]$manifest.backgroundCatalogProtocol -ne 1 -or [int]$manifest.backgroundIdentityProtocol -ne 1 -or [int]$manifest.backgroundVideoQualityPolicy -lt 3 -or [int]$manifest.backgroundManualProtocol -ne 1 -or [int]$manifest.backgroundVideoSelectionProtocol -ne 1) { throw 'Child download/catalog/shortcut-identity capability is missing; rebuild the current library host before export' }
  $version = Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$manifest.packageVersion -ne [string]$version.version) { throw 'Child package version differs from current mainline' }
  foreach ($kind in @('host','worker')) {
    $name = if ($kind -eq 'host') { 'CustomSteamLibrary.exe' } else { 'SteamArtworkLab.exe' }
    $hash = (Get-FileHash -LiteralPath (Join-Path $PackageRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ine [string]$manifest.sourceBuild.$kind) { throw "Child $kind executable differs from its build fingerprint" }
    Assert-CustomSteamLibrarySourceIndex -ProjectRoot $ProjectRoot -Kind $kind -SourceIndex @($manifest.sourceBuild.($kind + 'Sources'))
  }
  return $manifest
}


# Same-chain worker production proof only; never a cross-source native cache.
function Get-CslStagePin([string]$Path) {
  $p=[IO.Path]::GetFullPath($Path)
  $item=Get-Item -LiteralPath $p -ErrorAction Stop
  if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'CSL_STAGE_NONREGULAR_INPUT'}
  return [ordered]@{path=$p;bytes=[int64]$item.Length;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-CslStagePin($Pin) {
  if(-not $Pin -or [string]$Pin.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'CSL_STAGE_PIN_INVALID'}
  if(-not (Test-Path -LiteralPath ([string]$Pin.path) -PathType Leaf)){throw 'CSL_STAGE_INPUT_MISSING'}
  $actual=Get-CslStagePin ([string]$Pin.path)
  if($actual.bytes -ne $Pin.bytes -or $actual.sha256 -cne $Pin.sha256){throw ('CSL_STAGE_INPUT_CHANGED: '+$Pin.path)}
}
function Write-CslStageJson([string]$Path,$Value) {
  New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($Path))|Out-Null
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 14))
  $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Write-CslCompilerTools([string]$Path) {
  $rows=@()
  foreach($name in @('cl.exe','link.exe','rc.exe')){
    $command=Get-Command $name -CommandType Application -ErrorAction Stop|Select-Object -First 1
    $rows+=Get-CslStagePin $command.Source
  }
  $rows+=Get-CslStagePin $env:ComSpec
  $process=[Diagnostics.Process]::GetCurrentProcess()
  try{$rows+=Get-CslStagePin $process.MainModule.FileName}finally{$process.Dispose()}
  Write-CslStageJson $Path ([ordered]@{schema=1;kind='CSL_SELECTED_COMPILER_CONTEXT';tools=$rows})
}
function Assert-CslWorkerStage($Proof,[string]$ProjectRoot,[string]$WorkspaceRoot,[switch]$BeforeHostPublish) {
  $project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');$work=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
  if($Proof.schema -ne 1 -or $Proof.kind -cne 'CSL_SAME_CHAIN_WORKER' -or $Proof.project -ine $project -or $Proof.workspace -ine $work -or [string]$Proof.nonce -notmatch '^[a-f0-9]{32}$'){throw 'CSL_STAGE_SCOPE_MISMATCH'}
  $expected=Join-Path $work ('Build/CustomSteamLibrary/premain-'+$Proof.nonce)
  if([IO.Path]::GetFullPath($Proof.output) -ine [IO.Path]::GetFullPath($expected)){throw 'CSL_STAGE_OUTPUT_SCOPE_MISMATCH'}
  Assert-CustomSteamLibrarySourceIndex -ProjectRoot $project -Kind worker -SourceIndex @($Proof.sources)
  foreach($p in @($Proof.tools)){Assert-CslStagePin $p}
  if(@($Proof.tools).Count -ne 5){throw 'CSL_STAGE_TOOL_TRACE_INCOMPLETE'}
  foreach($p in @($Proof.trace,$Proof.compilerLog,$Proof.builtWorker,$Proof.publishedWorker)){Assert-CslStagePin $p}
  if([IO.Path]::GetFullPath($Proof.builtWorker.path) -ine [IO.Path]::GetFullPath((Join-Path $expected 'SteamArtworkLab.exe')) -or [IO.Path]::GetFullPath($Proof.publishedWorker.path) -ine [IO.Path]::GetFullPath((Join-Path $project 'CustomSteamLibrary/SteamArtworkLab.exe')) -or $Proof.builtWorker.sha256 -cne $Proof.publishedWorker.sha256){throw 'CSL_STAGE_WORKER_PATH_OR_HASH'}
  $manifestPath=Join-Path $project 'CustomSteamLibrary/package-manifest.json'
  if($BeforeHostPublish){Assert-CslStagePin $Proof.manifest}
  $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
  $version=Get-Content -LiteralPath (Join-Path $project 'version.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $entry=@($manifest.fileIndex|Where-Object{$_.path -ceq 'SteamArtworkLab.exe'})
  if($entry.Count -ne 1 -or $entry[0].sha256 -cne $Proof.publishedWorker.sha256 -or $entry[0].bytes -ne $Proof.publishedWorker.bytes -or $manifest.sourceBuild.worker -cne $Proof.publishedWorker.sha256 -or $manifest.packageVersion -cne $version.version){throw 'CSL_STAGE_MANIFEST_WORKER_CHANGED'}
  Assert-CustomSteamLibrarySourceIndex -ProjectRoot $project -Kind worker -SourceIndex @($manifest.sourceBuild.workerSources)
}
function Reset-CslPriorPendingForNewBuild([string]$ProjectRoot,[string]$WorkspaceRoot,$ExpectedPending,[string]$NewNonce) {
  # Query current-thread ownership of the original named mutex without acquiring it.
  # Opening an existing mutex or successfully waiting would not prove prior ownership.
  if(-not ('YmccCslMutexQuery' -as [type])){
    Add-Type -TypeDefinition (@(
      'using System; using System.Runtime.InteropServices;',
      'public static class YmccCslMutexQuery {',
      ' [StructLayout(LayoutKind.Sequential)] struct Basic { public int Count; public byte OwnedByCaller; public byte Abandoned; }',
      ' [DllImport("ntdll.dll")] static extern int NtQueryMutant(IntPtr h,int kind,out Basic info,int length,out int returned);',
      ' public static bool Owned(IntPtr h) { Basic b; int r; int s=NtQueryMutant(h,0,out b,Marshal.SizeOf(typeof(Basic)),out r); if(s!=0)throw new InvalidOperationException("Mutex ownership query failed: "+s);return b.OwnedByCaller!=0; }',
      '}'
    ) -join "`n")
  }
  $key=Get-ExportTextHash ([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\').ToLowerInvariant());$mutex=$null
  try{
    $mutex=[Threading.Mutex]::OpenExisting('Local\YeManCC.Export.'+$key)
    if(-not [YmccCslMutexQuery]::Owned($mutex.SafeWaitHandle.DangerousGetHandle())){throw 'CSL_STAGE_ORIGINAL_LOCK_NOT_OWNED'}
    $directory=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain';$pending=Join-Path $directory 'PENDING.json'
    if($NewNonce -notmatch '^[a-f0-9]{32}$'){throw 'CSL_STAGE_NEW_NONCE_REQUIRED'}
    $replacement=[ordered]@{schema=1;nonce=$NewNonce;project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\');state='NEW_BUILD_NOT_YET_BOUND'}
    if(Test-Path -LiteralPath $pending){
      if(-not $ExpectedPending -or [IO.Path]::GetFullPath($ExpectedPending.path) -ine [IO.Path]::GetFullPath($pending)){throw 'CSL_STAGE_PRIOR_PENDING_CHANGED'}
      Assert-CslStagePin $ExpectedPending
      $bytes=[IO.File]::ReadAllBytes($pending);$text=[Text.Encoding]::UTF8.GetString($bytes);$p=$text|ConvertFrom-Json
      $names=@($p.PSObject.Properties.Name|Sort-Object)-join '|'
      if($names -cne 'nonce|project|schema|state|workspace' -or [regex]::Matches($text,'(?<!\\)"(?:schema|nonce|project|workspace|state)"\s*:').Count -ne 5 -or ($p.schema -isnot [int] -and $p.schema -isnot [long]) -or $p.schema -ne 1 -or [string]$p.nonce -notmatch '^[a-f0-9]{32}$' -or $p.project -ine [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\') -or $p.workspace -ine [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\') -or $p.state -cne 'NEW_BUILD_NOT_YET_BOUND'){throw 'CSL_STAGE_PRIOR_PENDING_SCOPE_MISMATCH'}
      $destination=Join-Path $directory ($p.nonce+'.aborted-'+[Guid]::NewGuid().ToString('N')+'.json')
      Assert-CslStagePin $ExpectedPending
      if([Convert]::ToBase64String([IO.File]::ReadAllBytes($pending)) -cne [Convert]::ToBase64String($bytes)){throw 'CSL_STAGE_PRIOR_PENDING_CHANGED'}
      $temporary=Join-Path $directory ($NewNonce+'.pending.tmp')
      Write-CslStageJson $temporary $replacement
      if([Convert]::ToBase64String([IO.File]::ReadAllBytes($pending)) -cne [Convert]::ToBase64String($bytes)){throw 'CSL_STAGE_PRIOR_PENDING_CHANGED'}
      # Archive the old marker and install its successor in one atomic replacement.
      # A failed replacement worker must never uncover an unbound legacy-looking Build.
      [IO.File]::Replace($temporary,$pending,$destination)
    }else{
      if($ExpectedPending){throw 'CSL_STAGE_PRIOR_PENDING_CHANGED'}
      Write-CslStageJson $pending $replacement
    }
  }finally{if($mutex){$mutex.Dispose()}}
}

function Get-CslWorkerManifestInvariant($Manifest) {
  $copy=$Manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json
  $hostEntry=@($copy.fileIndex|Where-Object{$_.path -ceq 'CustomSteamLibrary.exe'})
  if($hostEntry.Count -ne 1 -or -not $copy.sourceBuild){throw 'CSL_STAGE_HOST_MANIFEST_INVALID'}
  $copy.PSObject.Properties.Remove('generatedAt')
  $copy.sourceBuild.PSObject.Properties.Remove('host');$copy.sourceBuild.PSObject.Properties.Remove('hostSources')
  $hostEntry[0].PSObject.Properties.Remove('bytes');$hostEntry[0].PSObject.Properties.Remove('sha256')
  return Get-ExportTextHash ($copy|ConvertTo-Json -Depth 30 -Compress)
}
function Assert-CslHostPublishTransition($Proof,[string]$ProjectRoot) {
  $path=Join-Path $ProjectRoot 'CustomSteamLibrary/package-manifest.json';$m=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
  if((Get-CslWorkerManifestInvariant $m) -cne $Proof.manifestInvariant){throw 'CSL_STAGE_NONHOST_MANIFEST_CHANGED'}
  # Reuse original host/worker protocol, source and executable freshness checks.
  $null=Assert-CustomSteamLibrarySourceFreshness $ProjectRoot (Join-Path $ProjectRoot 'CustomSteamLibrary')
  $cslHostPayload=Get-CslStagePin (Join-Path $ProjectRoot 'CustomSteamLibrary/CustomSteamLibrary.exe')
  $entry=@($m.fileIndex|Where-Object{$_.path -ceq 'CustomSteamLibrary.exe'})
  if($entry.Count -ne 1 -or $entry[0].sha256 -cne $cslHostPayload.sha256 -or $entry[0].bytes -ne $cslHostPayload.bytes){throw 'CSL_STAGE_HOST_PAYLOAD_CHANGED'}
}
function Start-CslWorkerBeforeMain([string]$ProjectRoot,[string]$WorkspaceRoot) {
  $nonce=[Guid]::NewGuid().ToString('N')
  $pendingPath=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain/PENDING.json'
  $priorPending=if(Test-Path -LiteralPath $pendingPath){Get-CslStagePin $pendingPath}else{$null}
  Reset-CslPriorPendingForNewBuild $ProjectRoot $WorkspaceRoot $priorPending $nonce
  $out=Join-Path $WorkspaceRoot ('Build/CustomSteamLibrary/premain-'+$nonce)
  # One real producer, not a syntax probe followed by a second compile.
  & (Join-Path $ProjectRoot 'tools/build-custom-steam-library-worker.ps1') -OutputDirectory $out -Publish -RecordPremainTools|Out-Host
  $tools=Get-Content -LiteralPath (Join-Path $out 'PREMAIN-COMPILER-TOOLS.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $manifestPath=Join-Path $ProjectRoot 'CustomSteamLibrary/package-manifest.json'
  $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
  $p=[ordered]@{schema=1;kind='CSL_SAME_CHAIN_WORKER';nonce=$nonce;project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\');output=[IO.Path]::GetFullPath($out);sources=@($manifest.sourceBuild.workerSources);tools=@($tools.tools);trace=(Get-CslStagePin (Join-Path $out 'PREMAIN-COMPILER-TOOLS.json'));compilerLog=(Get-CslStagePin (Join-Path $out ([string]$tools.compileLog)));builtWorker=(Get-CslStagePin (Join-Path $out 'SteamArtworkLab.exe'));publishedWorker=(Get-CslStagePin (Join-Path $ProjectRoot 'CustomSteamLibrary/SteamArtworkLab.exe'));manifest=(Get-CslStagePin $manifestPath);manifestInvariant=(Get-CslWorkerManifestInvariant $manifest)}
  Assert-CslWorkerStage $p $ProjectRoot $WorkspaceRoot -BeforeHostPublish
  return $p
}
function Assert-CslNoPendingBuild([string]$WorkspaceRoot) {
  if(Test-Path -LiteralPath (Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain/PENDING.json')){throw 'CSL_STAGE_BUILD_BIND_PENDING'}
}
function Begin-CslWorkerBuildBinding($Proof,[string]$ProjectRoot,[string]$WorkspaceRoot) {
  Assert-CslWorkerStage $Proof $ProjectRoot $WorkspaceRoot -BeforeHostPublish
  $pending=Get-Content -LiteralPath (Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain/PENDING.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if($pending.schema -ne 1 -or $pending.nonce -cne $Proof.nonce -or $pending.project -ine $Proof.project -or $pending.workspace -ine $Proof.workspace -or $pending.state -cne 'NEW_BUILD_NOT_YET_BOUND'){throw 'CSL_STAGE_PENDING_SCOPE_MISMATCH'}
}
function Complete-CslWorkerBeforeMain($Proof,$BuildIdentity,[string]$ProjectRoot,[string]$WorkspaceRoot) {
  Assert-CslWorkerStage $Proof $ProjectRoot $WorkspaceRoot -BeforeHostPublish
  if([string]$BuildIdentity.buildId -notmatch '^[a-f0-9]{64}$' -or [string]$BuildIdentity.sourceTreeSha256 -notmatch '^[a-f0-9]{64}$' -or $BuildIdentity.preMainWorker){throw 'CSL_STAGE_BUILD_REQUIRED'}
  $directory=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain'
  $pending=Join-Path $directory 'PENDING.json';$pendingBytes=[IO.File]::ReadAllBytes($pending);$mark=[Text.Encoding]::UTF8.GetString($pendingBytes)|ConvertFrom-Json
  if($mark.schema -ne 1 -or $mark.nonce -cne $Proof.nonce -or $mark.project -ine $Proof.project -or $mark.workspace -ine $Proof.workspace -or $mark.state -cne 'NEW_BUILD_NOT_YET_BOUND'){throw 'CSL_STAGE_PENDING_SCOPE_MISMATCH'}
  $buildPath=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json'
  # This is the exact byte serialization used by the unchanged original writer.
  $expected=[Text.UTF8Encoding]::new($false).GetBytes(($BuildIdentity|ConvertTo-Json -Depth 7))
  if([Convert]::ToBase64String([IO.File]::ReadAllBytes($buildPath)) -cne [Convert]::ToBase64String($expected)){throw 'CSL_STAGE_BUILD_RECORD_CHANGED'}
  $Proof.buildId=$BuildIdentity.buildId;$Proof.sourceTreeSha256=$BuildIdentity.sourceTreeSha256
  $path=Join-Path $directory ($BuildIdentity.buildId+'-'+$Proof.nonce+'.json')
  Write-CslStageJson $path $Proof
  $pin=Get-CslStagePin $path;$pin['nonce']=$Proof.nonce
  $final=[ordered]@{};foreach($key in $BuildIdentity.Keys){$final[$key]=$BuildIdentity[$key]};$final['preMainWorker']=$pin
  $temp=Join-Path ([IO.Path]::GetDirectoryName($buildPath)) ('BUILD-IDENTITY.'+$Proof.nonce+'.tmp')
  Write-CslStageJson $temp $final
  # The original exporter workspace lock already serializes compliant writers.
  # Full-byte CAS immediately precedes atomic replace; a changed record is retained.
  if([Convert]::ToBase64String([IO.File]::ReadAllBytes($buildPath)) -cne [Convert]::ToBase64String($expected) -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($pending)) -cne [Convert]::ToBase64String($pendingBytes)){throw 'CSL_STAGE_BUILD_RECORD_CHANGED'}
  $backup=Join-Path $directory ($Proof.nonce+'.unbound-build.json')
  [IO.File]::Replace($temp,$buildPath,$backup)
  Move-Item -LiteralPath $pending -Destination (Join-Path $directory ($Proof.nonce+'.bound.json'))
  $BuildIdentity['preMainWorker']=$pin
}
function Get-CslWorkerForPackage($BuildIdentity,[string]$ProjectRoot,[string]$WorkspaceRoot) {
  Assert-CslNoPendingBuild $WorkspaceRoot
  $hasProof=if($BuildIdentity -is [Collections.IDictionary]){$BuildIdentity.Contains('preMainWorker')}else{$null -ne $BuildIdentity.PSObject.Properties['preMainWorker']}
  if(-not $hasProof){return $null} # Old Build retains its original worker compile path.
  $pin=$BuildIdentity.preMainWorker
  if(-not $pin -or [string]$pin.nonce -notmatch '^[a-f0-9]{32}$'){throw 'CSL_STAGE_PROOF_BINDING_REQUIRED'}
  $path=Join-Path $WorkspaceRoot ('Build/Validation/ExportIdentity/WorkerPremain/'+$BuildIdentity.buildId+'-'+$pin.nonce+'.json')
  if([IO.Path]::GetFullPath($pin.path) -ine [IO.Path]::GetFullPath($path)){throw 'CSL_STAGE_PROOF_BINDING_REQUIRED'}
  Assert-CslStagePin $pin
  $p=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
  if($p.buildId -cne $BuildIdentity.buildId -or $p.sourceTreeSha256 -cne $BuildIdentity.sourceTreeSha256 -or $p.nonce -cne $pin.nonce){throw 'CSL_STAGE_BUILD_BINDING_MISMATCH'}
  Assert-CslWorkerStage $p $ProjectRoot $WorkspaceRoot
  $manifest=Get-Content -LiteralPath (Join-Path $ProjectRoot 'CustomSteamLibrary/package-manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if((Get-CslWorkerManifestInvariant $manifest) -cne $p.manifestInvariant){throw 'CSL_STAGE_NONHOST_MANIFEST_CHANGED'}
  return [pscustomobject]@{proof=$p;receipt=(Get-CslStagePin $path);packageManifest=$null;packageHost=$null}
}
function Use-CslWorkerForPackage($Selection,[string]$ProjectRoot,[string]$WorkspaceRoot) {
  Assert-CslStagePin $Selection.receipt
  Assert-CslWorkerStage $Selection.proof $ProjectRoot $WorkspaceRoot
  Assert-CslHostPublishTransition $Selection.proof $ProjectRoot
  # A package-local snapshot only. Reentry always executes the original host Publish again.
  $Selection.packageManifest=Get-CslStagePin (Join-Path $ProjectRoot 'CustomSteamLibrary/package-manifest.json')
  $Selection.packageHost=Get-CslStagePin (Join-Path $ProjectRoot 'CustomSteamLibrary/CustomSteamLibrary.exe')
  return [ordered]@{schema=1;status='SHARED_REFERENCE';buildId=$Selection.proof.buildId;producerNonce=$Selection.proof.nonce;producerReceipt=$Selection.receipt;executedThisPackage=$false;workerExecutionsThisPackage=0}
}
function Assert-CslWorkerPackageReference($Selection,[string]$ProjectRoot,[string]$WorkspaceRoot) {
  Assert-CslStagePin $Selection.receipt
  Assert-CslWorkerStage $Selection.proof $ProjectRoot $WorkspaceRoot
  if(-not $Selection.packageManifest -or -not $Selection.packageHost){throw 'CSL_STAGE_PACKAGE_SNAPSHOT_REQUIRED'}
  Assert-CslStagePin $Selection.packageManifest
  Assert-CslStagePin $Selection.packageHost
  Assert-CslHostPublishTransition $Selection.proof $ProjectRoot
}
