// Consume the existing YMCC snapshot only. These limits mirror the native
// monitor_snapshot_contract.h document-byte limits; no IO, timer or new source.
export const MONITOR_TOP_TEXT_MAX_BYTES = 16 * 1024;
export const MONITOR_FPS_TEXT_MAX_BYTES = 16 * 1024;
export const MONITOR_COMBINED_TEXT_MAX_BYTES = 24 * 1024;

// Count without allocating a TextEncoder buffer. A lone surrogate uses the
// UTF-8 replacement character (3 bytes), like the wire's UTF-8 encoder.
function boundedUtf8Bytes(raw: string, maxBytes: number): number {
  let bytes = 0;
  for (let i = 0; i < raw.length; ++i) {
    const code = raw.charCodeAt(i);
    if (code < 0x80) ++bytes;
    else if (code < 0x800) bytes += 2;
    else if (code >= 0xd800 && code <= 0xdbff && i + 1 < raw.length &&
      raw.charCodeAt(i + 1) >= 0xdc00 && raw.charCodeAt(i + 1) <= 0xdfff) {
      bytes += 4; ++i;
    } else bytes += 3;
    if (bytes > maxBytes) return -1;
  }
  return bytes;
}

export function createMonitorSnapshotBudget(): (top: unknown, fps: unknown) => boolean {
  // One accepted large-text pair only. Repeated 1Hz top text is not scanned
  // again on 100ms/200ms frame publications. Rejected input is never retained.
  let lastTop: string | null = null, lastFps: string | null = null;
  let lastTopBytes = 0, lastFpsBytes = 0;
  return (top: unknown, fps: unknown): boolean => {
    if ((top != null && typeof top !== 'string') || (fps != null && typeof fps !== 'string')) return false;
    const topText = typeof top === 'string' ? top : '';
    const fpsText = typeof fps === 'string' ? fps : '';
    // UTF-8 has at least one byte, at most three bytes per UTF-16 code unit.
    // Normal small documents need only length checks, with no scan/allocation.
    if (topText.length > MONITOR_TOP_TEXT_MAX_BYTES || fpsText.length > MONITOR_FPS_TEXT_MAX_BYTES ||
      topText.length + fpsText.length > MONITOR_COMBINED_TEXT_MAX_BYTES) return false;
    if (topText.length * 3 <= MONITOR_TOP_TEXT_MAX_BYTES && fpsText.length * 3 <= MONITOR_FPS_TEXT_MAX_BYTES &&
      (topText.length + fpsText.length) * 3 <= MONITOR_COMBINED_TEXT_MAX_BYTES) return true;
    const topBytes = topText === lastTop ? lastTopBytes : boundedUtf8Bytes(topText, MONITOR_TOP_TEXT_MAX_BYTES);
    if (topBytes < 0) return false;
    const fpsBytes = fpsText === lastFps ? lastFpsBytes : boundedUtf8Bytes(fpsText, MONITOR_FPS_TEXT_MAX_BYTES);
    if (fpsBytes < 0 || topBytes + fpsBytes > MONITOR_COMBINED_TEXT_MAX_BYTES) return false;
    lastTop = topText; lastTopBytes = topBytes; lastFps = fpsText; lastFpsBytes = fpsBytes;
    return true;
  };
}
