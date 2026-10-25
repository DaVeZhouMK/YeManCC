import { dialog, shell } from './api';

const MAX_BYTES = 12 * 1024 * 1024;
// Base64 stays below shell.run's existing 8 MiB output cap. Import-only reads.
const READ_BYTES = 4 * 1024 * 1024;
function base64(bytes: Uint8Array): string {
  let binary = '';
  for (let start = 0; start < bytes.length; start += 8192) binary += String.fromCharCode(...bytes.subarray(start, start + 8192));
  return btoa(binary);
}
function encodedCommand(script: string): string {
  const bytes = new Uint8Array(script.length * 2);
  for (let i = 0; i < script.length; i++) { const code = script.charCodeAt(i); bytes[i * 2] = code & 255; bytes[i * 2 + 1] = code >>> 8; }
  return base64(bytes);
}

// Native A has no browser user activation. Reuse the existing Windows picker
// and shell reader; encode the selected path as data rather than PowerShell code.
export async function pickNativeHudUnderlayFile(): Promise<File | null> {
  const picked = await dialog.openFile([{ name: '图片', extensions: ['png', 'jpg', 'jpeg', 'webp'] }]);
  if (!picked) return null;
  const pathData = base64(new TextEncoder().encode(picked));
  let size = 0, stamp = '', bytes = new Uint8Array(0), offset = 0;
  do {
    const script = `
$ErrorActionPreference = 'Stop'
try {
  $imagePath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('${pathData}'))
  $imageStream = [IO.File]::OpenRead($imagePath)
  try {
    $imageStamp = [IO.File]::GetLastWriteTimeUtc($imagePath).Ticks.ToString()
    if ($imageStream.Length -gt ${MAX_BYTES}) { throw 'image-too-large' }
    if (${offset} -gt 0 -and ($imageStream.Length -ne ${size} -or $imageStamp -ne '${stamp}')) { throw 'image-changed' }
    $imageCount = [int][Math]::Min(${READ_BYTES}, $imageStream.Length - ${offset})
    $imageBytes = New-Object byte[] $imageCount
    $imageStream.Position = ${offset}
    $imageOffset = 0
    while ($imageOffset -lt $imageCount) {
      $imageRead = $imageStream.Read($imageBytes, $imageOffset, $imageCount - $imageOffset)
      if ($imageRead -eq 0) { throw 'image-read-incomplete' }
      $imageOffset += $imageRead
    }
    [Console]::Out.Write($imageStream.Length.ToString() + '|' + $imageStamp + '|' + [Convert]::ToBase64String($imageBytes))
  } finally { $imageStream.Dispose() }
} catch {
  if ($_.Exception.Message -eq 'image-too-large') { [Console]::Error.Write('image-too-large') }
  elseif ($_.Exception.Message -eq 'image-changed') { [Console]::Error.Write('image-changed') }
  else { [Console]::Error.Write('image-read-failed') }
  exit 1
}`;
    const result = await shell.run('powershell.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', encodedCommand(script)], 15000);
    if (result.exitCode !== 0) throw Error(result.stderr.includes('image-too-large') ? '请选择小于 12 MB 的图片' : result.stderr.includes('image-changed') ? '图片读取时发生变化，请重新选择' : '无法读取这张图片，请重新选择');
    const parts = result.stdout.trim().split('|');
    if (parts.length !== 3 || !/^\d+$/.test(parts[0]) || !/^\d+$/.test(parts[1])) throw Error('图片读取结果异常，请重新选择');
    const currentSize = Number(parts[0]), raw = parts[2];
    if (!Number.isSafeInteger(currentSize) || currentSize < 0 || currentSize > MAX_BYTES || raw.length > Math.ceil(READ_BYTES / 3) * 4 || raw.length % 4 !== 0 || !/^[A-Za-z0-9+/]*={0,2}$/.test(raw)) throw Error('图片读取结果异常，请重新选择');
    if (offset === 0) { size = currentSize; stamp = parts[1]; bytes = new Uint8Array(size); }
    else if (currentSize !== size || parts[1] !== stamp) throw Error('图片读取时发生变化，请重新选择');
    const binary = atob(raw);
    if (binary.length !== Math.min(READ_BYTES, size - offset)) throw Error('图片读取不完整，请重新选择');
    for (let i = 0; i < binary.length; i++) bytes[offset + i] = binary.charCodeAt(i);
    offset += binary.length;
  } while (offset < size);
  // The shared importer validates the actual header, trims alpha and normalizes.
  return new File([bytes], picked.split(/[\\/]/).pop() || 'picture', { type: 'application/octet-stream' });
}
