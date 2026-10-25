function Test-HWiNFOSharedMemory {
    param([int]$ExpectedIntervalMs = 0, [ref]$ProducerProof)
    $strictProof = $null -ne $ProducerProof
    if ($strictProof) { $ProducerProof.Value = $null }
    $mmf = $null
    $accessor = $null
    try {
        foreach ($name in @('Global\HWiNFO_SENS_SM2', 'HWiNFO_SENS_SM2', 'Global\HWiNFO_SENS_SM', 'HWiNFO_SENS_SM')) {
            try {
                # Health reads never require write access to the producer.
                $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
                    $name, [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
                if ($mmf) { break }
            } catch { $mmf = $null }
        }
        if (-not $mmf) { return $false }

        $accessor = $mmf.CreateViewAccessor(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
        if ($accessor.Capacity -lt 44) { return $false }
        if ($accessor.ReadUInt32(0) -ne 0x53695748) { return $false }
        $version = $accessor.ReadUInt32(4)
        $revision = $accessor.ReadUInt32(8)
        $sensorOffset = [uint64]$accessor.ReadUInt32(20)
        $sensorSize = $accessor.ReadUInt32(24)
        $sensorCount = $accessor.ReadUInt32(28)
        $offset = [uint64]$accessor.ReadUInt32(32)
        $size = [uint64]$accessor.ReadUInt32(36)
        $count = [uint64]$accessor.ReadUInt32(40)
        if ($size -lt 316 -or $count -eq 0 -or $count -gt 100000) { return $false }
        $end = $offset + ($size * $count)
        if ($offset -ge [uint64]$accessor.Capacity -or $end -gt [uint64]$accessor.Capacity) { return $false }

        $periodKnown = $version -eq 2 -and $sensorOffset -ge 48 -and $accessor.Capacity -ge 48
        $period = 0
        if ($periodKnown) { $period = $accessor.ReadUInt32(44) }
        if ($ExpectedIntervalMs -ne 0) {
            # Same verified v2 extension gate as the existing native reader.
            # An unknown layout cannot prove an explicitly requested interval.
            if ($ExpectedIntervalMs -notin @(1000, 1500, 2000, 3000)) { return $false }
            if (-not $periodKnown -or $sensorOffset -gt $offset -or $period -ne $ExpectedIntervalMs) { return $false }
        }
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            $pollTime = $accessor.ReadInt64(12)
            if ($pollTime -le 0 -or $pollTime -gt 9223372036854775) { return $false }
        }

        # Read the existing region; do not infer health from non-zero sensor values.
        [void]$accessor.ReadByte([long]$offset)
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            # A receipt owns one coherent header and one producer identity key.
            if ($accessor.ReadUInt32(0) -ne 0x53695748 -or $accessor.ReadUInt32(4) -ne $version -or
                $accessor.ReadUInt32(8) -ne $revision -or $accessor.ReadUInt32(20) -ne $sensorOffset -or
                $accessor.ReadUInt32(24) -ne $sensorSize -or $accessor.ReadUInt32(28) -ne $sensorCount -or
                $accessor.ReadUInt32(32) -ne $offset -or $accessor.ReadUInt32(36) -ne $size -or
                $accessor.ReadUInt32(40) -ne $count -or $accessor.ReadInt64(12) -ne $pollTime -or
                ($periodKnown -and $accessor.ReadUInt32(44) -ne $period)) { return $false }
        }
        if ($strictProof) {
            # Match the native producer-age budget, using the same existing header.
            $budget = 5000L
            if ($periodKnown -and $period -gt 0) { $budget = [Math]::Max(5000L, [Math]::Min(60000L, [Math]::Max(500L, [long]$period)) + 3000L) }
            $age = (Get-HWiNFOClockMs) - $pollTime * 1000L
            if ($age -lt -1000L -or $age -ge $budget) { return $false }
            $key = @($name, $version, $revision, $sensorOffset, $sensorSize, $sensorCount, $offset, $size, $count, $periodKnown, $period) -join '|'
            $ProducerProof.Value = [pscustomobject]@{ PollTime = $pollTime; Layout = $key }
        }
        return $true
    } catch {
        return $false
    } finally {
        if ($accessor) { try { $accessor.Dispose() } catch { } }
        if ($mmf) { try { $mmf.Dispose() } catch { } }
    }
}

