# Exact tuple validation for lighting-build-v4. Existing v2/v3 contracts stay separate.
function Assert-YmccLightingBuildDelivery {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)]$Plan)
 $ErrorActionPreference='Stop';Set-StrictMode -Version Latest
 function Need([bool]$ok,[string]$code){if(-not $ok){throw ('LIGHTING_V4_'+$code)}}
 function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
 function Safe([string]$root,[string]$rel){
  Need ($rel -and -not[IO.Path]::IsPathRooted($rel) -and $rel -notmatch '(^|[\\/])\.\.?([\\/]|$)|:') 'PATH'
  $base=[IO.Path]::GetFullPath($root).TrimEnd('\');$p=[IO.Path]::GetFullPath((Join-Path $base $rel));Need ($p.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) 'PATH'
  $cursor=$p;while($cursor.Length -ge $base.Length){if(Test-Path -LiteralPath $cursor){Need (-not((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'REPARSE'};if($cursor -ieq $base){break};$cursor=Split-Path -Parent $cursor};return $p
 }
 Need ($Plan.kind -ceq 'lighting-build-v4') 'KIND'
 $manifestFile=[IO.Path]::GetFullPath($Plan.buildDelivery.path);$bundle=Split-Path -Parent $manifestFile
 Need ((Hash $manifestFile) -ceq $Plan.buildDelivery.sha256) 'MANIFEST_BYTES'
 $m=Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($m.kind -ceq 'ymcc-lighting-build-delivery-v1' -and $m.schemaVersion -eq 1 -and $m.hardwareIo -eq $false) 'MANIFEST'
 Need (@($m.members).Count -gt 100 -and @($m.members).Count -lt 4096 -and @($m.members|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'MEMBERS'
 foreach($row in $m.members){$f=Safe $bundle $row.path;Need ((Test-Path -LiteralPath $f -PathType Leaf) -and (Hash $f) -ceq $row.sha256 -and (Get-Item -LiteralPath $f).Length -eq $row.bytes) 'MEMBER_BYTES'}
 $physical=@(Get-ChildItem -LiteralPath $bundle -Recurse -File|ForEach-Object {$_.FullName.Substring($bundle.Length+1).Replace('\','/')}|Where-Object {$_ -cne 'LIGHTING-BUILD-DELIVERY.json'})
 Need ($physical.Count -eq @($m.members).Count -and -not(Compare-Object @($physical|Sort-Object) @($m.members.path|Sort-Object))) 'MEMBER_SET'
 function Recorded([string]$original,[string]$sum){
  $rows=@($m.recordedFiles|Where-Object {$_.originalPath -ieq $original -and $_.sha256 -ceq $sum});Need ($rows.Count -eq 1) 'RECORDED_BINDING';$file=Safe $bundle $rows[0].path;Need ((Hash $file) -ceq $sum) 'RECORDED_BYTES';return $file
 }
 function Proof($ref){$p=Safe $bundle $ref.path;Need ((Hash $p) -ceq $ref.sha256) 'PROOF_BYTES';return Get-Content -LiteralPath $p -Raw -Encoding UTF8|ConvertFrom-Json}
 $r=Proof $m.helperApproval
 Need ($r.kind -ceq 'ymcc-lighting-build-candidate-v1' -and $r.status -ceq 'PASS' -and $r.hardwareIo -eq $false -and $r.helperMainInvoked -eq $false -and @($r.sourceInputs).Count -eq 18) 'HELPER_APPROVAL'
 $h=$r.helperSha256;$core=$r.coreSha256
 Need ($h -ceq $Plan.newHelperSha256 -and $core -ceq $Plan.preservedCoreSha256 -and $m.tuple.helper -ceq $h -and $m.tuple.core -ceq $core) 'HELPER_CORE_TUPLE'
 foreach($row in $r.sourceInputs){Recorded (Join-Path $m.originalProjectRoot $row.path) $row.sha256|Out-Null}
 foreach($row in $r.toolInputs){
  $original=Join-Path $m.originalProjectRoot $row.path;Recorded $original $row.sha256|Out-Null
  $maintained=Join-Path (Split-Path -Parent $PSScriptRoot) ([IO.Path]::GetFileName($row.path));Need ((Hash $maintained) -ceq $row.sha256) 'ISSUER_TOOL_CHANGED'
 }
 $pdb=Proof $m.helperSourceProof
 Need ($pdb.pairVerified -eq $true -and $pdb.helperSha256 -ceq $h -and $pdb.compileSourceInputs -eq 12 -and $pdb.generatedDocuments -eq 3 -and @($pdb.documents).Count -eq 15) 'HELPER_PDB'
 Recorded (Join-Path $m.originalHelperWorkspace $pdb.pdb.path) $pdb.pdb.sha256|Out-Null
 foreach($doc in $pdb.documents){$base=if($doc.generated){$m.originalHelperWorkspace}else{$m.originalProjectRoot};Recorded (Join-Path $base $doc.path) $doc.sha256|Out-Null}
 Need ($r.globalResult.sha256 -ceq $m.globalResult.sha256 -and $r.signalResult.sha256 -ceq $m.signalResult.sha256 -and $r.sourceProof.sha256 -ceq $m.helperSourceProof.sha256) 'ORIGINAL_PROOF_LINKS'
 $outer=Proof $m.globalResult;$signal=Proof $m.signalResult
 Need ($outer.verdict.status -ceq 'PASS_SELECTED_SCOPE' -and $outer.verdict.exitCode -eq 0 -and @($outer.results).Count -eq 1 -and $outer.candidateInputs.expectedHelperSha256 -ceq $h) 'GLOBAL'
 $case=$outer.results[0];Need ($case.id -ceq 'lighting.signal-consumer' -and $case.status -ceq 'PASS' -and $case.exitCode -eq 0 -and -not$case.sourceChanged -and -not$case.candidateArtifactChanged -and -not$case.timedOut -and -not$case.requiredEvidenceMissing -and -not$case.requiredEvidenceFailed) 'GLOBAL_CASE'
 foreach($row in $case.sourceInputs){Recorded (Join-Path $m.originalProjectRoot $row.path) $row.sha256|Out-Null}
 foreach($row in $case.candidateArtifacts){Recorded $row.path $row.sha256|Out-Null}
 foreach($row in $signal.sourceHashes.PSObject.Properties){Recorded (Join-Path $m.originalProjectRoot $row.Name) $row.Value|Out-Null}
 Need (@($case.structuredEvidence|Where-Object {$_.sha256 -ceq $m.signalResult.sha256 -and -not$_.failed}).Count -eq 1) 'GLOBAL_SIGNAL_BINDING'
 $actual=$signal.compatibility.payloadCandidate
 Need ($signal.status -ceq 'PASS' -and $actual.actual -ceq $h -and $actual.behavior.actualSha256 -ceq $h -and $actual.behavior.passed -eq $true -and $actual.behavior.checksPassed -ge 282 -and $actual.behavior.mainEntryInvoked -eq $false -and @($actual.behavior.nativeAttempts).Count -eq 0 -and @($actual.behavior.dependencyAttempts).Count -eq 0 -and @($actual.behavior.ayaneoObservations).Count -eq 16) 'ACTUAL_HELPER'
 Need ($actual.catalogBehavior.artifactMode -eq $true -and $actual.catalogBehavior.assemblySha256 -ceq $h -and $actual.catalogBehavior.status -ceq 'PASS' -and $actual.catalogBehavior.checks -eq 549 -and @($actual.catalogBehavior.failures).Count -eq 0) 'ACTUAL_CATALOG'
 foreach($name in @('msiLightingContract','onexLightingContract','onexRequestCacheContract','boundHidLightingContract','ayaneoLightingContract')){Need ($signal.$name.passed -eq $true) 'TRANSPORT_CONTRACT'}
 $native=Proof $m.nativeBuild;$npdb=Proof $m.nativePdb;$consumer=Proof $m.nativeConsumer;$ns=Proof $m.nativeSources
 Need ($native.process.exitCode -eq 0 -and $native.process.naturalExit -eq $true -and $native.nativeExecuted -eq $false -and $native.binary.sha256 -ceq $m.tuple.native -and $m.tuple.native -ceq $Plan.preservedNativeSha256) 'NATIVE_TUPLE'
 Need ($ns.kind -ceq 'ymcc-lighting-native-source-v1' -and $npdb.pairVerified -eq $true -and $npdb.status -ceq 'PASS' -and $npdb.nativeSha256 -ceq $native.binary.sha256 -and $npdb.pdbSha256 -ceq $native.pdb.sha256 -and $native.sourceManifest.sha256 -ceq $m.nativeSources.sha256 -and $npdb.nativeSourceManifestSha256 -ceq $m.nativeSources.sha256) 'NATIVE_PDB_BINDING'
 foreach($row in $ns.files){Recorded (Join-Path $native.sourceRoot $row.path) $row.sha256|Out-Null}
 foreach($row in $npdb.verifiedSourceDocuments){Need ($row.checksumType -eq 3 -or $row.compilerChecksumUnavailable -eq $true) 'NATIVE_PDB_CHECKSUM';Need (@($ns.files|Where-Object {$_.path -ceq $row.path -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'NATIVE_PDB_SOURCE'}
 foreach($rel in @('native/main.cpp','native/oem_lighting_operations.h','native/lighting_device_core_binding.h','capabilities/hc-device-catalog.generated.h')){Need (@($npdb.verifiedSourceDocuments|Where-Object {$_.path -ceq $rel -and $_.checksumType -eq 3}).Count -eq 1) 'NATIVE_LIGHTING_SOURCE'}
 Recorded $native.binary.path $native.binary.sha256|Out-Null;Recorded $native.pdb.path $native.pdb.sha256|Out-Null
 Need ($consumer.status -ceq 'PASS' -and $consumer.exitCode -eq 0 -and $consumer.nativeBinarySha256 -ceq $native.binary.sha256 -and $consumer.nativeMainSha256 -ceq $native.mainSha256 -and $consumer.helperSha256 -ceq $h -and $consumer.coreSha256 -ceq $core -and $consumer.catalogSha256 -ceq $m.tuple.catalog -and @($consumer.cases).Count -eq 22 -and @($consumer.cases|Where-Object passed -NE $true).Count -eq 0 -and $consumer.processApiLinked -eq $false -and $consumer.hardwareIo -eq $false) 'NATIVE_CONSUMER'
 foreach($ref in @($consumer.inputs,$consumer.compiledConsumer,$consumer.fixtureSource,$consumer.actualHelperBehavior)){Recorded $ref.path $ref.sha256|Out-Null}
 $programRow=@($r.sourceInputs|Where-Object path -CEQ 'FanLab/LightSetter/Program.cs')[0]
 $programText=Get-Content -LiteralPath (Recorded (Join-Path $m.originalProjectRoot $programRow.path) $programRow.sha256) -Raw -Encoding UTF8
 $hcPin=[regex]::Match($programText,'ExpectedYmccHcCoreSha256\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
 Need ($hcPin -ceq $m.tuple.hc -and $hcPin.Length -eq 64) 'HC_PROFILE_TUPLE'
 Need (@($case.candidateArtifacts|Where-Object {$_.path -like '*HandheldCompanion.runtime.json' -and $_.sha256 -ceq $m.tuple.runtimeManifest}).Count -eq 1) 'PROFILE_MANIFEST_TUPLE'
 Need (@($m.installInventory).Count -ge 20 -and @($m.installInventory).Count -le 512 -and @($m.installInventory|Group-Object relative|Where-Object Count -NE 1).Count -eq 0) 'INSTALL_INVENTORY'
 $nativeHeaderRow=@($ns.files|Where-Object path -CEQ 'native/oem_lighting_operations.h')[0]
 $headerText=Get-Content -LiteralPath (Recorded (Join-Path $native.sourceRoot $nativeHeaderRow.path) $nativeHeaderRow.sha256) -Raw -Encoding UTF8
 $nativeCatalog=[regex]::Match($headerText,'kCatalogSha256\[\]\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
 $managedRow=@($r.sourceInputs|Where-Object path -CEQ 'capabilities/HcLightingCapabilityCatalog.generated.cs')[0]
 $managedText=Get-Content -LiteralPath (Recorded (Join-Path $m.originalProjectRoot $managedRow.path) $managedRow.sha256) -Raw -Encoding UTF8
 $managedCatalog=[regex]::Match($managedText,'CatalogSha256\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
 Need ($nativeCatalog.Length -eq 64 -and $nativeCatalog -ceq $managedCatalog -and $nativeCatalog -ceq $m.tuple.catalog) 'NATIVE_MANAGED_CATALOG'
 Need ($consumer.nativeBuildReceipt.sha256 -ceq $m.nativeBuild.sha256 -and $consumer.actualHelperBehavior.sha256 -ceq $m.signalResult.sha256) 'NATIVE_HELPER_PROOF_LINKS'
 $expectedPreserved=@($m.installInventory|Where-Object {$_.relative -cnotin @('PowerControl/light-setter/YeManLightSetter.dll','YeManCC/export-identity.json')})
 Need (@($Plan.preserved).Count -eq $expectedPreserved.Count -and @($Plan.preserved|Group-Object relative|Where-Object Count -NE 1).Count -eq 0) 'PRESERVED_SET'
 foreach($row in $expectedPreserved){Need (@($Plan.preserved|Where-Object {$_.relative -ceq $row.relative -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'PRESERVED_BINDING'}
 foreach($name in @('native','core','hc','runtimeManifest','fanHost','fanBackend','fanManifest')){
  $row=$m.tupleFiles.$name;Need ($row.sha256 -ceq $m.tuple.$name -and @($Plan.preserved|Where-Object {$_.relative -ceq $row.relative -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'INSTALL_TUPLE'
 }
 Need (@($Plan.changes).Count -eq 2) 'WRITE_SET'
 $runtime=@($r.runtimeFiles);Need ($runtime.Count -eq 6) 'RUNTIME_SET'
 foreach($row in $runtime){$path=Safe $bundle ('runtime/'+$row.name);Need ((Hash $path) -ceq $row.sha256) 'RUNTIME_BYTES';if($row.name -cne 'YeManLightSetter.dll'){Need (@($Plan.preserved|Where-Object {$_.relative -ceq ('PowerControl/light-setter/'+$row.name) -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'PRESERVED_RUNTIME_TUPLE'}}
 return [pscustomobject]@{status='PASS';kind='lighting-build-v4';helperSha256=$h;coreSha256=$core;nativeSha256=$m.tuple.native;catalogSha256=$m.tuple.catalog;actualArtifactChecks=$actual.behavior.checksPassed;actualCatalogChecks=$actual.catalogBehavior.checks;nativeConsumerCases=22;bundleManifestSha256=$Plan.buildDelivery.sha256;hardwareAcceptance=$false}
}
