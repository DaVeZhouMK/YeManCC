# Research only: collects standard SMBIOS/battery data and WMI class metadata.
# No vendor method invocation, driver installation, EC access or settings changes.
[CmdletBinding()]
param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$probeErrors = [System.Collections.Generic.List[string]]::new()
function Read-StandardCim([string]$ClassName) {
    try { @(Get-CimInstance -Namespace root/cimv2 -ClassName $ClassName -OperationTimeoutSec 10) }
    catch { $probeErrors.Add("${ClassName}: $($_.Exception.Message)"); @() }
}
$board = Read-StandardCim Win32_BaseBoard | Select-Object -First 1
$system = Read-StandardCim Win32_ComputerSystem | Select-Object -First 1
$bios = Read-StandardCim Win32_BIOS | Select-Object -First 1
$cpu = Read-StandardCim Win32_Processor | Select-Object -First 1
$batteries = @(Read-StandardCim Win32_Battery | ForEach-Object {
    [ordered]@{ name = $_.Name; status = $_.BatteryStatus; percent = $_.EstimatedChargeRemaining }
})
$maker = if ($board) { [string]$board.Manufacturer } else { '' }
$providerClass = if ($maker -match 'MICRO-STAR') { 'MSI_ACPI' }
    elseif ($maker -match '^LENOVO$') { 'LENOVO_OTHER_METHOD' }
    elseif ($maker -match 'ONE-NETBOOK') { 'SuRwECRegInterface' }
    else { $null }
$provider = $null
if ($providerClass) {
    try {
        $metadata = Get-CimClass -Namespace root/wmi -ClassName $providerClass -OperationTimeoutSec 10
        $provider = [ordered]@{ class = $providerClass; metadataFound = $true; methods = @($metadata.CimClassMethods | ForEach-Object Name); methodsInvoked = @() }
    } catch {
        $provider = [ordered]@{ class = $providerClass; metadataFound = $false; methods = @(); methodsInvoked = @() }
        $probeErrors.Add("${providerClass}: $($_.Exception.Message)")
    }
}
$result = [ordered]@{
    schemaVersion = 1
    observedAtUtc = [DateTime]::UtcNow.ToString('o')
    readOnly = $true
    identity = [ordered]@{
        manufacturer = $maker
        boardProduct = if ($board) { [string]$board.Product } else { $null }
        systemProduct = if ($system) { [string]$system.Model } else { $null }
        processor = if ($cpu) { [string]$cpu.Name } else { $null }
        firmware = [ordered]@{
            bios = if ($bios) { [string]$bios.SMBIOSBIOSVersion } else { $null }
            ecMajor = if ($bios) { $bios.EmbeddedControllerMajorVersion } else { $null }
            ecMinor = if ($bios) { $bios.EmbeddedControllerMinorVersion } else { $null }
        }
    }
    batteries = $batteries
    providerMetadata = $provider
    errors = @($probeErrors.ToArray())
    chargeLimitRead = $false
    hardwareWritePerformed = $false
    note = 'Provider presence and battery percentage do not prove charge-limit support. No battery serial numbers collected.'
}
$jsonText = $result | ConvertTo-Json -Depth 8
if ($OutputPath) {
    $jsonText | Out-File -LiteralPath $OutputPath -Encoding utf8 -NoClobber
    Write-Output "Read-only battery inventory saved: $OutputPath"
} else { $jsonText }
