# Disk/package validation stays separate from the executing child's literal.
# Ordinary old packages have no receipt. That disables the upgrade exception;
# it must not stop the ordinary update or pretend to identify a running image.
function Get-YmccNativeBuildIdentity([string]$Executable,[string]$ReceiptPath) {
    if (![IO.Path]::IsPathRooted($Executable) -or ![IO.Path]::IsPathRooted($ReceiptPath)) { throw 'NATIVE_BUILD_ABSOLUTE_PATH_REQUIRED' }
    $receiptBytes=[IO.File]::ReadAllBytes($ReceiptPath)
    if($receiptBytes.Length -lt 1 -or $receiptBytes.Length -gt 4096){throw 'NATIVE_BUILD_RECEIPT_SIZE_INVALID'}
    $receipt=([Text.UTF8Encoding]::new($false,$true)).GetString($receiptBytes)|ConvertFrom-Json -ErrorAction Stop
    $keys=(@($receipt.PSObject.Properties.Name)|Sort-Object)-join ','
    if($receipt -isnot [pscustomobject] -or $keys -cne 'nativeBuildId,nativeBytes,nativeSha256,schema' -or
        $receipt.schema -cne 'ymcc.native-build-identity.v1' -or $receipt.nativeBuildId -cnotmatch '^[a-f0-9]{64}$' -or $receipt.nativeSha256 -cnotmatch '^[a-f0-9]{64}$' -or
        ($receipt.nativeBytes -isnot [int32] -and $receipt.nativeBytes -isnot [int64]) -or $receipt.nativeBytes -lt 64 -or $receipt.nativeBytes -gt 268435456){throw 'NATIVE_BUILD_RECEIPT_INVALID'}
    $stream=[IO.File]::Open($Executable,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if($stream.Length -ne $receipt.nativeBytes){throw 'NATIVE_BUILD_RECEIPT_MISMATCH'}
        $bytes=[byte[]]::new([int]$stream.Length);$offset=0
        while($offset -lt $bytes.Length){$count=$stream.Read($bytes,$offset,$bytes.Length-$offset);if(!$count){throw 'NATIVE_BUILD_SHORT_READ'};$offset+=$count}
        if($stream.ReadByte() -ne -1){throw 'NATIVE_BUILD_CHANGED'}
    }finally{$stream.Dispose()}
    if([BitConverter]::ToUInt16($bytes,0) -ne 0x5a4d){throw 'NATIVE_BUILD_PE_REQUIRED'}
    $pe=[BitConverter]::ToUInt32($bytes,60)
    if($pe -lt 64 -or $pe -gt $bytes.Length-24 -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664){throw 'NATIVE_BUILD_X64_PE_REQUIRED'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$digest=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    if($digest -cne $receipt.nativeSha256){throw 'NATIVE_BUILD_RECEIPT_MISMATCH'}
    $text=[Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $ids=@([regex]::Matches($text,'YMCC-NATIVE-BUILD:v1:([a-f0-9]{64})\x00')|ForEach-Object{$_.Groups[1].Value}|Select-Object -Unique)
    if($ids.Count -ne 1 -or $ids[0] -cne $receipt.nativeBuildId){throw 'NATIVE_BUILD_EXACT_MARKER_REQUIRED'}
    return $receipt
}
