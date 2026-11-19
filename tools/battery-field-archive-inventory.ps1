param([Parameter(Mandatory)][string]$LogRoot)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath $LogRoot).ProviderPath
$archives = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.zip' | Sort-Object FullName)
$matches = [Collections.Generic.List[object]]::new()
$errors = [Collections.Generic.List[object]]::new()
$standardCount = 0
$extraCount = 0
$wanted = @('battery-charge-host.log', 'battery-charge-host.log.1', 'battery-charge-client.log',
    'battery-charge-client.log.1', 'battery-host.payload.json')
foreach ($archive in $archives) {
    if ($archive.Name -like 'YeManCC-logs-*.zip') { $standardCount++; continue }
    $extraCount++
    $relative = [IO.Path]::GetRelativePath($root, $archive.FullName).Replace('\', '/')
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($archive.FullName)
        try {
            $members = @($zip.Entries | Where-Object { $_.Name -cin $wanted })
            if (!$members.Count) { continue }
            $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            $ambiguous = $false
            foreach ($entry in $members) {
                if (!$paths.Add($entry.FullName)) {
                    $errors.Add([ordered]@{archive=$relative; member=$entry.FullName; type='duplicate-battery-member'})
                    $ambiguous = $true
                }
            }
            if (!$ambiguous) {
                $matches.Add([ordered]@{
                    archive=$relative
                    archiveSha256=(Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                    members=@($members | ForEach-Object { [ordered]@{ name=$_.FullName; bytes=$_.Length } })
                })
            }
        } finally { $zip.Dispose() }
    } catch {
        $errors.Add([ordered]@{archive=$relative; type='archive-inspection-failed'; exception=$_.Exception.GetType().Name})
    }
}
[ordered]@{
    schemaVersion=1
    scope='Battery member inventory for nonstandard outer ZIP names only; no extraction, nested archive recursion, session attribution or field admission.'
    zipCount=$archives.Count
    standardNameCount=$standardCount
    nonstandardNameCount=$extraCount
    matches=$matches.ToArray()
    errors=$errors.ToArray()
} | ConvertTo-Json -Depth 6
if ($errors.Count) { exit 1 }
