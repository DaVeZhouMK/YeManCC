import math
from PIL import Image, ImageDraw

BG = (14, 19, 28, 255)        # #0e131c dark navy tile
Y_COLOR = (233, 238, 245, 255)  # #e9eef5 near-white Y

def make(size):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(1, int(size * 0.22))
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=BG)

    def P(x, y):
        return (x / 24 * size, y / 24 * size)

    left_top = P(8, 7.5)
    right_top = P(16, 7.5)
    split = P(12, 13.5)
    bottom = P(12, 16.5)

    w = max(1, int(1.6 / 24 * size))
    cap = w / 2.0
    for a, b in ((left_top, split), (right_top, split), (split, bottom)):
        d.line([a, b], fill=Y_COLOR, width=w)
    for p in (left_top, right_top, bottom, split):
        d.ellipse([p[0] - cap, p[1] - cap, p[0] + cap, p[1] + cap], fill=Y_COLOR)
    return img

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
# ICO writer emits multiple entries by downsampling a single high-res source
# via the `sizes` parameter (append_images is ignored for ICO).
out = r'C:\SOFT\YeMan\YeManCC4\YeManCC3\native\app.ico'
base = make(256)
base.save(out, format='ICO', sizes=[(s, s) for s in sizes])
print('wrote', out, base.size)
