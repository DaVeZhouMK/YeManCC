import sys
from pathlib import Path
from PIL import Image
import numpy as np

SRC = Path(r"C:/Users/DaVe/.workbuddy/clipboard-images/clipboard-2026-07-31T01-30-40-793Z-2d6ec878.png")
OUT_PNG = Path(r"C:/SOFT/YeMan/YeManCC4/YeManCC3/public/icons/steam.png")
OUT_PNG.parent.mkdir(parents=True, exist_ok=True)

im = Image.open(SRC).convert("RGBA")
arr = np.array(im)
h, w = arr.shape[:2]
print("size", w, h)

# 背景色：用四角平均估计
corners = np.stack([
    arr[2, 2], arr[2, w-3], arr[h-3, 2], arr[h-3, w-3]
], axis=0).astype(float)
bg = corners.mean(axis=0)[:3]
print("bg approx", bg)

rgb = arr[:, :, :3].astype(float)
dist = np.sqrt(((rgb - bg) ** 2).sum(axis=2))  # 到背景色的欧氏距离

# 软阈值 -> alpha
low, high = 45, 130
alpha = np.clip((dist - low) / (high - low), 0, 1) * 255
alpha = alpha.astype(np.uint8)

# 找前景包围盒
ys, xs = np.where(alpha > 12)
if len(xs) == 0:
    print("NO FOREGROUND FOUND")
    sys.exit(1)
x0, x1 = xs.min(), xs.max()
y0, y1 = ys.min(), ys.max()
print("bbox", x0, y0, x1, y1)

# 裁剪为正方形（以最大边为基准，居中）
pad = 6
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
half = max(x1 - x0, y1 - y0) // 2 + pad
x0s, x1s = max(0, cx - half), min(w, cx + half)
y0s, y1s = max(0, cy - half), min(h, cy + half)

crop_rgb = arr[y0s:y1s, x0s:x1s, :3]
crop_a = alpha[y0s:y1s, x0s:x1s]

# 把前景统一成纯白（贴合线性白标风格），保留抠出的 alpha
out = np.zeros((crop_a.shape[0], crop_a.shape[1], 4), dtype=np.uint8)
out[:, :, 0] = 255
out[:, :, 1] = 255
out[:, :, 2] = 255
out[:, :, 3] = crop_a

# 轻微羽化边缘：对 alpha 做 1px 高斯模糊
from PIL import ImageFilter
out_im = Image.fromarray(out, "RGBA")
out_im = out_im.filter(ImageFilter.GaussianBlur(0.6))
out_im.save(OUT_PNG)
print("saved", OUT_PNG, "crop", out_im.size)
