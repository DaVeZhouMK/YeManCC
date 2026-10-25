# Read-only binding of the dedicated-memory reader revision to Native export bytes.
# The existing source/payload hashes remain the proof of exact build inputs and package identity.
function Assert-GpuDedicatedMemoryNativeExport([string]$ProjectRoot,[string]$NativeExe) {
  $header=Join-Path $ProjectRoot 'native/gpu_dedicated_memory_runtime.h'
  if(-not [IO.File]::Exists($header)) { return [ordered]@{status='not-applicable';reason='reader-source-absent'} }
  $source=[IO.File]::ReadAllText($header)
  $match=[regex]::Match($source,'inline constexpr const char\* readerRevision = "(ymcc-gpu-memory-[a-z0-9-]+)";')
  if(-not $match.Success) { throw 'EXPORT_GPU_MEMORY_REVISION_MISSING_FROM_SOURCE' }
  if(-not [IO.File]::Exists($NativeExe)) { throw 'EXPORT_GPU_MEMORY_NATIVE_MISSING' }
  $revision=$match.Groups[1].Value
  $bytes=[IO.File]::ReadAllBytes($NativeExe)
  if($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'EXPORT_GPU_MEMORY_NATIVE_INVALID_PE' }
  $offset=[BitConverter]::ToInt32($bytes,0x3c)
  if($offset -lt 0 -or $offset -gt $bytes.Length-26 -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$offset+4) -ne 0x8664) { throw 'EXPORT_GPU_MEMORY_NATIVE_INVALID_PE' }
  $ascii=[Text.Encoding]::ASCII.GetString($bytes)
  foreach($required in @($revision,'readerRevision','exclusionReason','kernel-adapter-type')) {
    if($ascii.IndexOf($required,[StringComparison]::Ordinal) -lt 0) { throw "EXPORT_GPU_MEMORY_NATIVE_REVISION_MISMATCH: missing $required; rebuild Native" }
  }
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose() }
  return [ordered]@{status='passed';readerRevision=$revision;nativeSha256=$hash;scope='compiled-revision-marker-plus-existing-source-payload-binding';hardwareOrProductExecuted=$false}
}
function Assert-GpuDedicatedMemoryExportReceipt($Recorded,$Current) {
  if($Current.status -eq 'not-applicable') { return }
  if(-not $Recorded -or $Recorded.status -ne 'passed' -or $Recorded.readerRevision -ne $Current.readerRevision -or $Recorded.nativeSha256 -ne $Current.nativeSha256) {
    throw 'EXPORT_GPU_MEMORY_RECEIPT_MISMATCH: rebuild before packaging'
  }
}
