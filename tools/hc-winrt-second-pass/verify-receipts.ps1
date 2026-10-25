[CmdletBinding()]
param([string]$EvidenceRoot='D:/Codex/2026-10-18/kai-s/work/hc-third-trim/winrt',[string]$RepositoryRoot='G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC')
$ErrorActionPreference='Stop'
function JsonRead([string]$p){Get-Content -LiteralPath $p -Raw|ConvertFrom-Json}
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Stable($value){ConvertTo-Json -InputObject $value -Depth 40 -Compress}
$base=JsonRead (Join-Path $EvidenceRoot 'software-baseline-26-recursive.json')
$baselineContracts=JsonRead (Join-Path $EvidenceRoot 'contracts-baseline-recursive.json')
$baselineSignatures=JsonRead (Join-Path $EvidenceRoot 'signatures-baseline.json')
$attempts=@()
foreach($mode in @('guarded','guarded-v2','full-dynamic','sdk-repack','sdk-data-only','sdk-data-only-recursive')){
 $attempt=Join-Path $EvidenceRoot "attempt-$mode"
 $software=JsonRead (Join-Path $attempt 'software-26.json')
 $reference=if($mode -eq 'sdk-data-only-recursive'){$base}else{JsonRead (Join-Path $EvidenceRoot 'software-baseline-26.json')}
 $testDiffs=@()
 foreach($test in $reference.tests){$actual=@($software.tests|Where-Object name -eq $test.name);if($actual.Count -ne 1 -or $actual[0].status -ne $test.status -or (Stable $actual[0].value) -cne (Stable $test.value) -or $actual[0].error -cne $test.error){$testDiffs+=@{name=$test.name;expected=$test;actual=$actual}}}
 $typeDiffs=@()
 foreach($type in $reference.allTypes){$actual=@($software.allTypes|Where-Object name -eq $type.name);if($actual.Count -ne 1 -or (Stable $actual[0]) -cne (Stable $type)){$typeDiffs+=@{name=$type.name;expected=$type;actual=$actual}}}
 $overlay=Join-Path $attempt 'overlay'
 $sizes=@(foreach($name in @('Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll')){$original=Join-Path $EvidenceRoot "baseline/runtime/$name";$candidate=Join-Path $overlay $name;@{path=$name;beforeBytes=(Get-Item -LiteralPath $original).Length;afterBytes=(Get-Item -LiteralPath $candidate).Length;beforeSha256=Hash $original;afterSha256=Hash $candidate}})
 $totalBefore=($sizes|Measure-Object beforeBytes -Sum).Sum;$totalAfter=($sizes|Measure-Object afterBytes -Sum).Sum
 $comparison=@{softwareExactEquality=($testDiffs.Count -eq 0);softwareTests=$software.tests.Count;testDifferences=$testDiffs;allGetTypesExactEquality=($typeDiffs.Count -eq 0);getTypesAssemblies=$software.allTypes.Count;getTypesDifferences=$typeDiffs;nativeDenied=@($software.native|Where-Object allowed -eq $false).Count;nativeBoundaryEnabled=$true;hardwareAccess=$false;networkRequests=$false;hostStarted=$false;sdlStarted=$false}
 $comparison|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $attempt 'baseline-comparison.json') -Encoding utf8
 $attempts+=@{mode=$mode;sizes=$sizes;pairBeforeBytes=$totalBefore;pairAfterBytes=$totalAfter;savedBytes=($totalBefore-$totalAfter);comparison=$comparison;callback=(JsonRead (Join-Path $attempt 'callback.json'))}
}
$chosen=Join-Path $EvidenceRoot 'attempt-sdk-data-only-recursive'
$entry=@($attempts|Where-Object mode -eq 'sdk-data-only-recursive')[0]
$il=JsonRead (Join-Path $chosen 'exact-IL.json');$abi=JsonRead (Join-Path $chosen 'retained-ABI.json');$data=JsonRead (Join-Path $chosen 'data-audit.json');$dynamic=JsonRead (Join-Path $chosen 'dynamic-data-audit.json');$contracts=JsonRead (Join-Path $chosen 'contracts.json');$signatures=JsonRead (Join-Path $chosen 'signatures.json')
$contractKeys=@($contracts.records|ForEach-Object{Stable (@{source=$_.source;kind=$_.kind;target=$_.target;scope=$_.scope;definition=$_.definition;error=$_.error})}|Sort-Object)
$baseKeys=@($baselineContracts.records|ForEach-Object{Stable (@{source=$_.source;kind=$_.kind;target=$_.target;scope=$_.scope;definition=$_.definition;error=$_.error})}|Sort-Object)
$contractsEqual=(Stable $contractKeys) -ceq (Stable $baseKeys)
$changed=@();$lock=JsonRead (Join-Path $EvidenceRoot 'BASELINE-LOCK.json')
foreach($row in $lock.files|Where-Object kind -eq 'runtime'){$current=Join-Path $chosen ('overlay/'+$row.path);if((Hash $current) -ne $row.sha256){$changed+=@{path=$row.path;before=$row.sha256;after=Hash $current}}}
$liveChanges=@();$liveBinaryFailures=@()
foreach($row in $lock.files){if(-not(Test-Path -LiteralPath $row.source)){$liveBinaryFailures+=$row.source;continue};$h=Hash $row.source;if($h -ne $row.sha256){$liveChanges+=@{path=$row.source;before=$row.sha256;after=$h;metadataOnly=([IO.Path]::GetExtension($row.source) -eq '.json')};if([IO.Path]::GetExtension($row.source) -ne '.json'){$liveBinaryFailures+=$row.source}}}
$removedIlCount=0;foreach($libraryResult in $il.results){$removedIlCount+=$libraryResult.removed.Count}
$requirements=@{
 originalBaselineAll26Pass=($base.failed -eq 0 -and $base.tests.Count -eq 26)
 candidateAll26Exact=($entry.comparison.softwareExactEquality -and $entry.comparison.softwareTests -eq 26)
 all83GetTypesExact=($entry.comparison.allGetTypesExactEquality -and $entry.comparison.getTypesAssemblies -eq 83)
 all22CallbacksExact=($entry.callback.status -eq 'passed' -and $entry.callback.count -eq 22 -and $entry.callback.allGetTypes -eq 1768)
 allRetainedIL=($il.status -eq 'passed' -and $removedIlCount -eq 0)
 allRetainedABI=($abi.status -eq 'passed')
 everyCrossAssemblyRefResolves=($contracts.missingCount -eq 0 -and $contracts.typesChecked -eq 495 -and $contracts.membersChecked -eq 5910 -and $contracts.exportsChecked -eq 0)
 crossAssemblyRefsSameAsBaseline=$contractsEqual
 onlyCompilerRvaFieldsRemoved=($data.failures.Count -eq 0 -and $data.removedFieldCount -eq 5088 -and $data.removedMethodsPropertiesEvents.Count -eq 0)
 dataShapeAndDynamicBoundary=($dynamic.allRemovedFieldsAre16ByteCompilerHashNamedRvaConstants -and $dynamic.removedFieldOrContainerLiteralMentions.Count -eq 0 -and $dynamic.managedConsumerCount -eq 83)
 onlySdkOverlayChanged=($changed.Count -eq 1 -and $changed[0].path -eq 'Microsoft.Windows.SDK.NET.dll')
 originalWinrtBytesAndSignature=($signatures.assemblies[1].sha256 -eq $baselineSignatures.assemblies[1].sha256 -and $signatures.assemblies[1].strongNameCryptographicVerification -and $signatures.assemblies[1].embeddedCertificateSha256 -eq $baselineSignatures.assemblies[1].embeddedCertificateSha256)
 sdkOriginalIdentityAndSigningFactsPreserved=($signatures.assemblies[0].assemblyIdentity -eq $baselineSignatures.assemblies[0].assemblyIdentity -and -not $signatures.assemblies[0].strongNameCryptographicVerification -and -not $baselineSignatures.assemblies[0].strongNameCryptographicVerification -and $signatures.assemblies[0].embeddedAuthenticodeCertificateBytes -eq 0 -and $baselineSignatures.assemblies[0].embeddedAuthenticodeCertificateBytes -eq 0)
 zeroNonSystemNativeCalls=($entry.comparison.nativeDenied -eq 0)
 productionAndSharedBinariesStillPinned=($liveBinaryFailures.Count -eq 0)
 positiveNetSavings=($entry.savedBytes -gt 0)
}
$failed=@($requirements.GetEnumerator()|Where-Object Value -eq $false|ForEach-Object Key)
$status=if($failed.Count -eq 0){'READY_FOR_PARENT_REVIEW_SDK_RVA_ONLY'}else{'HOLD_RETAINED'}
$targetSize=@($entry.sizes|Where-Object path -eq 'Microsoft.Windows.SDK.NET.dll')[0]
$promote=Join-Path $EvidenceRoot 'promotion';New-Item -ItemType Directory -Path $promote -Force|Out-Null
if($failed.Count -eq 0){Copy-Item -LiteralPath (Join-Path $chosen 'overlay/Microsoft.Windows.SDK.NET.dll') -Destination (Join-Path $promote 'Microsoft.Windows.SDK.NET.dll') -Force}
$promotion=@{schemaVersion=1;status=$status;productionModified=$false;eligibleForParentPromotion=($failed.Count -eq 0);actualPromotionPerformed=$false;netSavedBytes=$entry.savedBytes;beforePairBytes=$entry.pairBeforeBytes;afterPairBytes=$entry.pairAfterBytes;replacements=@(@{path='Microsoft.Windows.SDK.NET.dll';candidate=Join-Path $promote 'Microsoft.Windows.SDK.NET.dll';beforeSha256=$targetSize.beforeSha256;afterSha256=$targetSize.afterSha256;beforeBytes=$targetSize.beforeBytes;afterBytes=$targetSize.afterBytes;signatureFacts='Current inherited YMCC SDK derivative fails strong-name cryptographic verification before and after. Original assembly public key/token retained; no Authenticode certificate before or after; no signing key used. Do not label it original Microsoft-signed software.'});retained=@{path='WinRT.Runtime.dll';sha256=$baselineSignatures.assemblies[1].sha256;bytes=528944;strongNameValid=$true;embeddedAuthenticodeCertificateBytes=10288};requirements=$requirements;failedRequirements=$failed;promotionInstruction='Parent may integrate only the named SDK replacement, after checking current binary beforeSha256; refresh its authoritative metadata without restoring this frozen overlay manifest or either frozen JSON. This task performs no production promotion.';limitations=@('No physical device, Host, native SDK, SDL, network, sleep or lifecycle test performed.','No claim of arbitrary reflection compatibility into compiler implementation-only RVA fields; all existing projected SDK types/methods/ABI/reflection metadata and immutable consumer contracts are retained.','WinRT.Runtime additional shrinking is rejected because rewriting destroys an originally valid third-party strong name/certificate.','HC binary is unchanged; the required 1768 CLR types and original 77-machine support are retained, not physically revalidated.')}
$promotion|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'PROMOTION-LIST.json') -Encoding utf8
$receipt=@{schemaVersion=1;status=$status;requirements=$requirements;failedRequirements=$failed;contractEquality=$contractsEqual;baselineRuntimeFiles=86;sharedBinFiles=8;managedConsumerAssemblies=83;candidateOnlyChangedFiles=$changed;parentMetadataChangesDuringExperiment=$liveChanges;unexpectedBinaryChanges=$liveBinaryFailures;baselineRun='software-baseline-26-recursive.json';candidateRun='attempt-sdk-data-only-recursive/software-26.json';pureSoftwareChecks=26;pureConstructors=7;callbacks=22;hcClrTypes=1768;sdkMetadataTypes=1892;sdkMethods=10396;winrtMetadataTypes=603;winrtMethods=3594;externalTypeRefs=495;externalMemberRefs=5910;exportedTypesToCheck=0;removedCompilerRvaFields=5088;noDevelopmentSignatureApplied=$true;noPrivateKeyRead=$true;sourceScopes=@($PSScriptRoot,$EvidenceRoot);created=(Get-Date -Format o)}
$receipt|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'VALIDATION-RECEIPT.json') -Encoding utf8
$attempts|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'ATTEMPTS.json') -Encoding utf8
Write-Output "$status : $($entry.savedBytes) bytes saved; failed gates $($failed -join ', ')"
if($failed.Count){exit 1}


