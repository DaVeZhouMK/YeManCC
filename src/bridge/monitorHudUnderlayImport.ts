// Explicit import only. Preserve supplied alpha, trim transparent margins and
// normalize to a small static grayscale raster outside the 1 Hz sensor path.
export async function prepareHudUnderlayFile(file: File): Promise<string> {
  if (file.size > 12 * 1024 * 1024) throw Error('请选择小于 12 MB 的图片');
  const header = new Uint8Array(await file.slice(0, 12).arrayBuffer());
  const png = header[0] === 137 && header[1] === 80 && header[2] === 78 && header[3] === 71 && header[4] === 13 && header[5] === 10 && header[6] === 26 && header[7] === 10;
  const jpeg = header[0] === 255 && header[1] === 216 && header[2] === 255;
  const webp = header[0] === 82 && header[1] === 73 && header[2] === 70 && header[3] === 70 && header[8] === 87 && header[9] === 69 && header[10] === 66 && header[11] === 80;
  if (!png && !jpeg && !webp) throw Error('请选择 PNG、JPG 或 WebP 图片');
  let bitmap: ImageBitmap;
  try { bitmap = await createImageBitmap(file); }
  catch { throw Error('无法读取这张图片，请换一张'); }
  try {
    if (bitmap.width * bitmap.height > 24 * 1024 * 1024) throw Error('图片分辨率过大，请先缩小图片');
    // Bounded scratch surface keeps alpha inspection small even for large PNGs.
    const ratio = Math.min(1, 1536 / Math.max(bitmap.width, bitmap.height));
    const scratch = document.createElement('canvas');
    scratch.width = Math.max(1, Math.round(bitmap.width * ratio)); scratch.height = Math.max(1, Math.round(bitmap.height * ratio));
    const sourceCtx = scratch.getContext('2d'); if (!sourceCtx) throw Error('图片转换不可用');
    sourceCtx.drawImage(bitmap, 0, 0, scratch.width, scratch.height);
    const source = sourceCtx.getImageData(0, 0, scratch.width, scratch.height);
    let left = scratch.width, top = scratch.height, right = -1, bottom = -1;
    // Trim only alpha=0. Opaque backgrounds and soft alpha edges are retained.
    for (let y = 0; y < scratch.height; y++) for (let x = 0; x < scratch.width; x++) {
      if (source.data[(y * scratch.width + x) * 4 + 3] > 0) {
        left = Math.min(left, x); right = Math.max(right, x); top = Math.min(top, y); bottom = Math.max(bottom, y);
      }
    }
    if (right < left || bottom < top) throw Error('这张图片完全透明，没有可显示的内容');
    const sourceWidth = right - left + 1, sourceHeight = bottom - top + 1;
    const canvas = document.createElement('canvas');
    const targetRatio = Math.min(1, 480 / Math.max(sourceWidth, sourceHeight));
    canvas.width = Math.max(1, Math.round(sourceWidth * targetRatio)); canvas.height = Math.max(1, Math.round(sourceHeight * targetRatio));
    const ctx = canvas.getContext('2d'); if (!ctx) throw Error('图片转换不可用');
    ctx.drawImage(scratch, left, top, sourceWidth, sourceHeight, 0, 0, canvas.width, canvas.height);
    const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height);
    for (let i = 0; i < pixels.data.length; i += 4) {
      const gray = Math.round(pixels.data[i] * .2126 + pixels.data[i + 1] * .7152 + pixels.data[i + 2] * .0722);
      pixels.data[i] = pixels.data[i + 1] = pixels.data[i + 2] = gray;
    }
    ctx.putImageData(pixels, 0, 0);
    // A bounded fallback also accepts complex PNGs without growing native frames.
    for (const maxEdge of [480, 384, 320, 256]) {
      const surface = maxEdge >= Math.max(canvas.width, canvas.height) ? canvas : document.createElement('canvas');
      if (surface !== canvas) {
        const resize = maxEdge / Math.max(canvas.width, canvas.height);
        surface.width = Math.max(1, Math.round(canvas.width * resize)); surface.height = Math.max(1, Math.round(canvas.height * resize));
        const resizeCtx = surface.getContext('2d'); if (!resizeCtx) throw Error('图片转换不可用');
        resizeCtx.drawImage(canvas, 0, 0, surface.width, surface.height);
      }
      for (const quality of [.78, .58, .38, .18]) {
        const image = surface.toDataURL('image/webp', quality);
        if (image.startsWith('data:image/webp;base64,') && image.length <= 45000) return image;
      }
    }
    throw Error('图片细节太多，请选择较小或更简洁的图片');
  } finally { bitmap.close(); }
}
