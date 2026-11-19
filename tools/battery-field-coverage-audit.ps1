param(
    [Parameter(Mandatory)][string]$LogRoot,
    [Parameter(Mandatory)][string]$Out
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath $LogRoot).ProviderPath
$archives = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter 'YeManCC-logs-*.zip' | Sort-Object FullName)
$groups = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
$candidates = [System.Collections.Specialized.OrderedDictionary]::new([StringComparer]::Ordinal)
$copiedLineIdentityConflict = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$unlinked = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$linked = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$invalidPid = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$invalidIdentityText = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$modelConflict = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$afterStop = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$archiveHashes = [System.Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
$sampleArchives = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$contributingArchives = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$errors = [System.Collections.Generic.List[object]]::new()
$hostMembers = 0

function Get-LineHash([string]$Text) {
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text))).ToLowerInvariant()
}

function Select-LoggedFields($Source, [string[]]$Names) {
    $projection = [ordered]@{}
    if ($Source -is [Collections.IDictionary]) {
        foreach ($name in $Names) {
            if (!$Source.Contains($name)) { continue }
            $value = $Source[$name]
            if ($null -eq $value -or $value -is [string] -or $value -is [ValueType]) { $projection[$name] = $value }
        }
    }
    $projection
}

function Get-LoggedCapabilityAttribution($Detail) {
    if ($Detail -isnot [Collections.IDictionary] -or
        (!$Detail.Contains('capabilityRuleId') -and !$Detail.Contains('capabilityCatalogSha256'))) { return }
    $rule = $Detail['capabilityRuleId']
    $digest = $Detail['capabilityCatalogSha256']
    # This is a logged claim, not a match against the current catalog or a provider receipt.
    if ($rule -is [string] -and $rule -cmatch '\Abattery\.rule\.[a-z0-9-]+\.[A-Za-z0-9_]+\z' -and
        $digest -is [string] -and $digest -cmatch '\A[0-9a-f]{64}\z') {
        return [ordered]@{status='logged-pair'; capabilityRuleId=$rule; capabilityCatalogSha256=$digest}
    }
    return [ordered]@{status='unresolved'}
}

foreach ($archive in $archives) {
    $relative = [IO.Path]::GetRelativePath($root, $archive.FullName).Replace('\', '/')
    $archiveStream = $null
    try {
        $archiveStream = [IO.File]::Open($archive.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Read, $true)
        try {
            $members = @($zip.Entries | Where-Object { $_.Name -cin 'battery-charge-host.log', 'battery-charge-host.log.1' })
            $paths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            $duplicates = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($entry in $members) {
                if (!$paths.Add($entry.FullName) -and $duplicates.Add($entry.FullName)) {
                    $errors.Add([ordered]@{archive=$relative; member=$entry.FullName; type='duplicate-host-member'})
                }
            }
            # A sample locator uses the full member path, so none of this archive's rows can be published on ambiguity.
            if ($duplicates.Count) { continue }
            if ($members.Count) {
                # Hash the same file handle as the ZIP reader; reopening the path can pin a replacement.
                $position = $archiveStream.Position
                try {
                    $archiveStream.Position = 0
                    $archiveHashes.Add($relative, (Get-FileHash -InputStream $archiveStream -Algorithm SHA256).Hash.ToLowerInvariant())
                } finally { $archiveStream.Position = $position }
            }
            foreach ($entry in $members) {
                $hostMembers++
                # A rotated member does not authorize a PID join with the active member.
                $identities = @{}
                $stoppedPids = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
                $reader = [IO.StreamReader]::new($entry.Open())
                try {
                    $lineNumber = 0
                    while ($null -ne ($line = $reader.ReadLine())) {
                        $lineNumber++
                        if ($line -notmatch '"event"\s*:\s*"(host-start|in-process-start|host-stop|in-process-stop|identity|request-result|controller-read-error|msi-packet|firmware-admission-rejected)"') { continue }
                        try { $event = ConvertFrom-Json -InputObject $line -AsHashtable -DateKind String } catch {
                            $errors.Add([ordered]@{archive=$relative; member=$entry.FullName; line=$lineNumber; type='invalid-event-json'})
                            continue
                        }
                        $eventPid = $event.pid
                        if (($eventPid -isnot [int] -and $eventPid -isnot [long]) -or $eventPid -lt 1 -or $eventPid -gt [int]::MaxValue) {
                            [void]$invalidPid.Add($line)
                            continue
                        }
                        $pidKey = [string]$eventPid
                        $detail = $event.detail
                        if ($event.event -cin 'host-stop', 'in-process-stop') {
                            $identities.Remove($pidKey)
                            [void]$stoppedPids.Add($pidKey)
                            continue
                        }
                        if ($event.event -cin 'host-start', 'in-process-start') {
                            $identities.Remove($pidKey)
                            [void]$stoppedPids.Remove($pidKey)
                            if ($event.event -ceq 'host-start') { continue }
                        }
                        if ($stoppedPids.Contains($pidKey)) { [void]$afterStop.Add($line); continue }
                        if ($event.event -cin 'in-process-start', 'identity') {
                            $identities.Remove($pidKey)
                            $invalidText = $detail -isnot [Collections.IDictionary]
                            if (!$invalidText) {
                                foreach ($field in 'manufacturer', 'boardProduct', 'systemProduct', 'processor', 'bios', 'boardManufacturerRaw', 'boardProductRaw') {
                                    if ($null -ne $detail[$field] -and $detail[$field] -isnot [string]) {
                                        $invalidText = $true
                                        break
                                    }
                                }
                            }
                            if ($invalidText) { [void]$invalidIdentityText.Add($line); continue }
                            if ([string]::IsNullOrWhiteSpace($detail.manufacturer) -or
                                ([string]::IsNullOrWhiteSpace($detail.boardProduct) -and [string]::IsNullOrWhiteSpace($detail.systemProduct))) { continue }
                            $identityFields = [ordered]@{
                                manufacturer = [string]$detail.manufacturer
                                boardProduct = [string]$detail.boardProduct
                                systemProduct = [string]$detail.systemProduct
                                processor = [string]$detail.processor
                                bios = $detail.bios; ecMajor = $detail.ecMajor; ecMinor = $detail.ecMinor
                            }
                            foreach ($rawField in @('boardManufacturerRaw', 'boardProductRaw')) {
                                if ($detail[$rawField] -is [string]) { $identityFields[$rawField] = $detail[$rawField] }
                            }
                            $identities[$pidKey] = @{ fields=$identityFields; source=[ordered]@{
                                line=$lineNumber; eventLineSha256=(Get-LineHash $line)
                            }}
                        }
                        $parent = $identities[$pidKey]
                        if (!$parent) { [void]$unlinked.Add($line); continue }
                        $identity = $parent.fields
                        $reportedModel = $null
                        if ($event.event -ceq 'request-result' -and $detail.model -is [string] -and $detail.model.Trim()) {
                            $reportedModel = $detail.model
                            if (![string]::Equals($reportedModel.Trim(), $identity.boardProduct.Trim(), [StringComparison]::OrdinalIgnoreCase) -and
                                ![string]::Equals($reportedModel.Trim(), $identity.systemProduct.Trim(), [StringComparison]::OrdinalIgnoreCase)) {
                                [void]$modelConflict.Add($line)
                                continue
                            }
                        }
                        [void]$linked.Add($line)
                        if ($copiedLineIdentityConflict.Contains($line)) { continue }
                        $identityKey = ConvertTo-Json -InputObject $identity -Compress
                        if ($candidates.Contains($line)) {
                            if ($candidates[$line].identityKey -cne $identityKey) {
                                $candidates.Remove($line)
                                [void]$copiedLineIdentityConflict.Add($line)
                            }
                            continue
                        }
                        $fact = switch -Case ($event.event) {
                            'request-result' { [ordered]@{operation=$detail.operation; ok=$detail.ok; supported=$detail.supported; batteryPresent=$detail.batteryPresent; kind=$detail.kind; verification=$detail.verification; currentLimit=$detail.currentLimit; requestedLimit=$detail.requestedLimit; reason=$detail.reason} }
                            'controller-read-error' { [ordered]@{type=$detail.type; message=$detail.message; wmiStatus=$detail.wmiStatus; hresult=$detail.hresult} }
                            'msi-packet' { [ordered]@{method=$detail.method; block=$detail.block; flag=$detail.flag; raw=$detail.raw; chargeRaw=$detail.chargeRaw; length=$detail.length; shape=$detail.shape} }
                            'firmware-admission-rejected' {
                                $admission = Select-LoggedFields $detail @('route','deviceClass','allowed','providerOpened','scope')
                                if ($detail.observed -is [Collections.IDictionary]) {
                                    $admission.observed = Select-LoggedFields $detail.observed @('manufacturer','boardProduct','systemProduct','bios','ecMajor','ecMinor','ecMajorKnown','ecMinorKnown','source')
                                }
                                if ($detail.required -is [Collections.IDictionary]) {
                                    $admission.required = Select-LoggedFields $detail.required @('kind','majorMin','minorMin','bios','ecMajor','ecMinor','unknownValue')
                                }
                                $admission
                            }
                            default { [ordered]@{} }
                        }
                        if ($null -ne $reportedModel) { $fact.reportedModel = $reportedModel }
                        if ($event.event -ceq 'request-result') {
                            $attribution = Get-LoggedCapabilityAttribution $detail
                            if ($null -ne $attribution) { $fact.capabilityAttribution = $attribution }
                        }
                        $groupKey = ConvertTo-Json -InputObject @($identity, $event.event, $fact) -Depth 8 -Compress
                        $candidates.Add($line, @{identityKey=$identityKey; groupKey=$groupKey;
                            group=[ordered]@{identity=$identity; event=$event.event; fact=$fact; count=0; sample=[ordered]@{
                                archive=$relative; member=$entry.FullName; line=$lineNumber; eventLineSha256=(Get-LineHash $line); pid=$event.pid; time=$event.time
                                identityEvidence=$parent.source
                            }}})
                    }
                } finally { $reader.Dispose() }
            }
        } finally { $zip.Dispose() }
    } catch {
        $errors.Add([ordered]@{archive=$relative; type=$_.Exception.GetType().FullName; message=$_.Exception.Message})
    } finally {
        if ($null -ne $archiveStream) { $archiveStream.Dispose() }
    }
}

# A later archive can invalidate an earlier attribution. Only aggregate after all copies are checked.
foreach ($candidate in $candidates.Values) {
    $groupKey = $candidate.groupKey
    if (!$groups.ContainsKey($groupKey)) {
        $groups[$groupKey] = $candidate.group
        $groups[$groupKey]['contributors'] = [System.Collections.Generic.List[object]]::new()
        [void]$sampleArchives.Add($candidate.group.sample.archive)
    }
    $groups[$groupKey].contributors.Add($candidate.group.sample)
    [void]$contributingArchives.Add($candidate.group.sample.archive)
    $groups[$groupKey].count++
}
$unlinked.ExceptWith($linked)
function Select-ArchivePins($Names) {
    $pins = [System.Collections.Specialized.OrderedDictionary]::new([StringComparer]::Ordinal)
    foreach ($key in ($Names | Sort-Object)) { $pins.Add($key, $archiveHashes[$key]) }
    return ,$pins
}
$report = [ordered]@{
    schemaVersion = 1
    scope = 'Historical Host events from active and .1 rotation members only; identity within the same member and observed PID session. An exact JSON line with competing identity attributions is excluded. Each counted line has one contributor locator and a same-handle archive hash. Remaining unique lines do not count distinct transactions or establish current payload acceptance.'
    archives = $archives.Count; hostMembers = $hostMembers; unlinkedUniqueLines = $unlinked.Count
    replyLinkExclusions = [ordered]@{ invalidPidUniqueLines=$invalidPid.Count; modelConflictUniqueLines=$modelConflict.Count; afterStopUniqueLines=$afterStop.Count; copiedLineIdentityConflictUniqueLines=$copiedLineIdentityConflict.Count; invalidIdentityTextUniqueLines=$invalidIdentityText.Count }
    errors = $errors.ToArray(); pinnedSampleArchives = (Select-ArchivePins $sampleArchives)
    pinnedContributingArchives = (Select-ArchivePins $contributingArchives)
    groups = @($groups.Keys | Sort-Object | ForEach-Object { $groups[$_] })
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Out -Encoding utf8
Write-Output "Battery field coverage: $($archives.Count) archives, $hostMembers host members, $($groups.Count) fact groups, $($errors.Count) read/parse errors."
if ($errors.Count) { exit 1 }
