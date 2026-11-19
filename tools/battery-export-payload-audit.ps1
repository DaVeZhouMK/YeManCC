param(
    [Parameter(Mandatory)][string]$ExportReceipt,
    [Parameter(Mandatory)][string]$LocalReleaseReceipt,
    [Parameter(Mandatory)][string]$InstallerReceipt,
    [Parameter(Mandatory)][string]$TargetedPackage,
    [Parameter(Mandatory)][string]$TargetedSha256,
    [string]$Out
)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
function Pin([string]$Path) {
    $f=Get-Item -LiteralPath $Path
    [ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName).Hash.ToLowerInvariant()}
}
function Hash-Bytes([byte[]]$Bytes) {
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}
function Json-Bytes([byte[]]$Bytes) {
    [Text.Encoding]::UTF8.GetString($Bytes).TrimStart([char]0xfeff) | ConvertFrom-Json -NoEnumerate
}
function Entry([IO.Compression.ZipArchive]$Zip,[string]$Name,[switch]$Optional) {
    $m=@($Zip.Entries | Where-Object {$_.FullName.Replace('\','/') -ieq $Name})
    if($Optional -and $m.Count -eq 0){return}
    if($m.Count -ne 1){throw "ZIP member missing or ambiguous: $Name"}
    $m[0]
}
function Entry-Bytes([IO.Compression.ZipArchiveEntry]$Entry) {
    if($Entry.Length -lt 1 -or $Entry.Length -gt 250MB){throw "Out-of-scope ZIP member size: $($Entry.FullName)"}
    $src=$Entry.Open()
    $mem=[IO.MemoryStream]::new([int]$Entry.Length)
    try{$src.CopyTo($mem);if($mem.Length -ne $Entry.Length){throw 'ZIP member length mismatch'};return ,$mem.ToArray()}
    finally{$src.Dispose();$mem.Dispose()}
}
function Entry-Hash([IO.Compression.ZipArchiveEntry]$Entry) {
    $s=$Entry.Open()
    try{[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($s)).ToLowerInvariant()}
    finally{$s.Dispose()}
}
function Resource([Reflection.PortableExecutable.PEReader]$Pe,[string]$Name) {
    $m=[Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($Pe)
    $r=@(foreach($h in $m.ManifestResources){$row=$m.GetManifestResource($h);if($m.GetString($row.Name) -ceq $Name){$row}})
    if($r.Count -ne 1 -or !$r[0].Implementation.IsNil){throw "Local PE resource missing or ambiguous: $Name"}
    $dir=$Pe.PEHeaders.CorHeader.ResourcesDirectory
    $section=$Pe.GetSectionData($dir.RelativeVirtualAddress)
    $offset=[int]$r[0].Offset
    $reader=$section.GetReader($offset,$section.Length-$offset)
    $size=$reader.ReadInt32()
    if($size -le 0 -or $size -gt $reader.RemainingBytes -or ([long]$offset+4+$size) -gt $dir.Size){throw "Invalid PE resource length: $Name"}
    return ,$reader.ReadBytes($size)
}
function Route-Key($Row){"$($Row.route)/$($Row.identity.deviceClass)"}
function Diff-Rules($Rows,$Reference) {
    $present=[ordered]@{}
    $expected=[ordered]@{}
    foreach($r in $Rows){$k=Route-Key $r;if($present.Contains($k)){throw "Duplicate packaged route key: $k"};$present[$k]=$r}
    foreach($r in $Reference){$k=Route-Key $r;if($expected.Contains($k)){throw "Duplicate mainline route key: $k"};$expected[$k]=$r}
    [ordered]@{
        missingFromPackage=@($expected.Keys | Where-Object {!$present.Contains($_)})
        onlyInPackage=@($present.Keys | Where-Object {!$expected.Contains($_)})
        changed=@(foreach($k in $expected.Keys){
            if($present.Contains($k) -and ($present[$k] | ConvertTo-Json -Depth 15 -Compress) -cne ($expected[$k] | ConvertTo-Json -Depth 15 -Compress)) {
                [ordered]@{key=$k;package=$present[$k];mainline=$expected[$k]}
            }
        })
    }
}
$routesFile=Pin (Join-Path $repo 'native/BatteryHost/battery-routes.json')
$current=Get-Content -Raw -LiteralPath $routesFile.path | ConvertFrom-Json -NoEnumerate
$currentContractFile=Pin (Join-Path $repo 'native/BatteryHost/runtime-contract.json')
$currentContract=Get-Content -Raw -LiteralPath $currentContractFile.path | ConvertFrom-Json

function Inspect([IO.Compression.ZipArchive]$Zip,[string]$Role,[string]$ExpectedNative) {
    $prefix='PowerControl/battery-host/'
    $manifestEntry=Entry $Zip ($prefix+'battery-host.payload.json')
    $manifest=Json-Bytes (Entry-Bytes $manifestEntry)
    if($manifest.component -cne 'YeManBatteryHost' -or $manifest.schema -ne 1){throw "Unexpected payload manifest: $Role"}
    $files=foreach($f in $manifest.files){
        if($f.path -cnotmatch '^[a-zA-Z0-9_.-]+$'){throw 'Battery payload member path is not a filename'}
        $e=Entry $Zip ($prefix+$f.path)
        $hash=Entry-Hash $e
        if($e.Length -ne $f.bytes -or $hash -ine $f.sha256){throw "Battery payload manifest mismatch: $Role / $($f.path)"}
        [ordered]@{name=$f.path;bytes=$e.Length;sha256=$hash}
    }
    if(@($files).Count -ne 9 -or @($files.name | Sort-Object -Unique).Count -ne 9 -or 'YeManBatteryHost.dll' -cnotin $files.name) {
        throw "Unexpected battery payload manifest roster: $Role"
    }
    $dll=[IO.MemoryStream]::new((Entry-Bytes (Entry $Zip ($prefix+'YeManBatteryHost.dll'))),$false)
    $pe=[Reflection.PortableExecutable.PEReader]::new($dll)
    try{
        $rb=Resource $pe 'YeManBatteryHost.battery-routes.json'
        $cb=Resource $pe 'YeManBatteryHost.runtime-contract.json'
        $m=[Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
        $moduleId=$m.GetGuid($m.GetModuleDefinition().Mvid).ToString()
    } finally {$pe.Dispose();$dll.Dispose()}
    $routeHash=Hash-Bytes $rb
    $rows=Json-Bytes $rb
    $c=Json-Bytes $cb
    foreach($key in @('protocol','sourceId','implementation','component')){
        if($c.$key -cne $manifest.contract.$key){throw "Battery PE/manifest contract mismatch: $Role / $key"}
    }
    $native=Entry $Zip 'YeManCC/YeManCC.exe'
    $nativeHash=Entry-Hash $native
    if($ExpectedNative -and $nativeHash -ine $ExpectedNative){throw "Native EXE does not match its receipt: $Role"}
    $nativeIdentity=Entry $Zip 'YeManCC/native-build-identity.json' -Optional
    if($nativeIdentity -and (Json-Bytes (Entry-Bytes $nativeIdentity)).nativeSha256 -ine $nativeHash){throw "Native ZIP identity mismatch: $Role"}
    $ascii=[Text.Encoding]::ASCII.GetString((Entry-Bytes $native))
    $unicode=[Text.Encoding]::Unicode.GetString((Entry-Bytes $native))
    $sourceToken=$ascii.Contains([string]$c.sourceId,[StringComparison]::Ordinal) -or $unicode.Contains([string]$c.sourceId,[StringComparison]::Ordinal)
    if(!$sourceToken){throw "Native EXE does not contain the battery contract source ID: $Role"}
    [ordered]@{role=$Role;routesSha256=$routeHash;rules=@($rows).Count;providerRoutes=@($rows.route | Sort-Object -Unique).Count;
        batteryAssemblyMvid=$moduleId;manifestSha256=(Entry-Hash $manifestEntry);manifestFiles=$files;
        contractSha256=(Hash-Bytes $cb);sourceId=$c.sourceId;protocol=$c.protocol;implementation=$c.implementation;
        nativeSha256=$nativeHash;nativeContainsContractSourceId=$sourceToken;matchesCurrentRoutes=($routeHash -ceq $routesFile.sha256);
        matchesCurrentContract=($c.sourceId -ceq $currentContract.sourceId);
        x2MiniPro=@($rows | Where-Object route -CEQ 'oxp-x2-mini-pro-direct-ec');
        diff=(Diff-Rules $rows $current)}
}
function Inspect-Archive([string]$Path,[string]$Sha,[string]$Role,[string]$Native) {
    $p=Pin $Path
    if($p.sha256 -ine $Sha){throw "Archive does not match its declared hash: $Role"}
    $z=[IO.Compression.ZipFile]::OpenRead($Path)
    try{$report=Inspect $z $Role $Native;$report.archive=$p;return $report}
    finally{$z.Dispose()}
}
$ordinaryRecord=Get-Content -Raw -LiteralPath $ExportReceipt | ConvertFrom-Json
$releaseRecord=Get-Content -Raw -LiteralPath $LocalReleaseReceipt | ConvertFrom-Json
$installerRecord=Get-Content -Raw -LiteralPath $InstallerReceipt | ConvertFrom-Json
$ordinary=Inspect-Archive $ordinaryRecord.package.path $ordinaryRecord.package.sha256 'regular-pipeline test export' $ordinaryRecord.native.sha256
$ordinary.receipt=Pin $ExportReceipt
$ordinary.formalReleaseAccepted=$ordinaryRecord.formalReleaseAccepted
if($ordinaryRecord.snapshot){$ordinary.snapshotRouteSource=Pin (Join-Path $ordinaryRecord.snapshot 'native/BatteryHost/battery-routes.json')}
$release=Inspect-Archive (Join-Path (Split-Path -Parent $LocalReleaseReceipt) $releaseRecord.package) $releaseRecord.sha256 'local release directory package' $releaseRecord.nativeSha256
$release.receipt=Pin $LocalReleaseReceipt
$release.releaseMode=$releaseRecord.releaseMode
$targeted=Inspect-Archive $TargetedPackage $TargetedSha256 'X2 Mini Pro targeted retest package' ''

$ip=Pin $installerRecord.CurrentArtifact
if($ip.sha256 -ine $installerRecord.Sha256 -or $ip.bytes -ne $installerRecord.Bytes){throw 'Installer identity does not match its receipt'}
$stream=[IO.File]::OpenRead($ip.path)
$pe=[Reflection.PortableExecutable.PEReader]::new($stream)
try{$coreBytes=Resource $pe 'core.payload'}finally{$pe.Dispose();$stream.Dispose()}
$coreHash=Hash-Bytes $coreBytes
$coreMem=[IO.MemoryStream]::new($coreBytes,$false)
$coreZip=[IO.Compression.ZipArchive]::new($coreMem,[IO.Compression.ZipArchiveMode]::Read)
try{$programBytes=Entry-Bytes (Entry $coreZip 'program.zip')}finally{$coreZip.Dispose();$coreMem.Dispose();$coreBytes=$null}
$programHash=Hash-Bytes $programBytes
if($programHash -ine $installerRecord.ProgramSha256){throw 'Embedded installer program.zip does not match the receipt'}
$programMem=[IO.MemoryStream]::new($programBytes,$false)
$programZip=[IO.Compression.ZipArchive]::new($programMem,[IO.Compression.ZipArchiveMode]::Read)
try{$installer=Inspect $programZip 'delivered installer embedded program archive' $installerRecord.NativeExeSha256}
finally{$programZip.Dispose();$programMem.Dispose()}
$installer.artifact=$ip
$installer.corePayloadSha256=$coreHash
$installer.embeddedProgramSha256=$programHash
$installer.receipt=Pin $InstallerReceipt

$result=[ordered]@{schemaVersion=1;scope='Read-only ZIP and PE metadata comparison. No assembly or installer loaded or executed. Native source ID token presence is a static check, not exercised transport.';
    tool=(Pin $PSCommandPath);mainline=[ordered]@{routes=$routesFile;contract=$currentContractFile;sourceId=$currentContract.sourceId;rules=@($current).Count;providerRoutes=@($current.route | Sort-Object -Unique).Count};
    artifacts=@($release,$installer,$ordinary,$targeted)}
if($Out){$result | ConvertTo-Json -Depth 19 | Set-Content -LiteralPath $Out -Encoding utf8}
$result.artifacts | ForEach-Object {[pscustomobject]@{Role=$_.role;Rules=$_.rules;Routes=$_.providerRoutes;X2MiniPro=$_.x2MiniPro.Count;CurrentTable=$_.matchesCurrentRoutes;CurrentContract=$_.matchesCurrentContract;Missing=$_.diff.missingFromPackage -join ', '}} | Format-Table -Wrap
