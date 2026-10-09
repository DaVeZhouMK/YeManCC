#!/usr/bin/env python3
# Generate a simple YeManCC .ico (64x64, PNG-compressed) with a lightning bolt.
import struct, zlib, os

W = H = 64
bg = (11, 14, 19, 255)       # #0b0e13
bolt = (245, 185, 61, 255)   # #f5b93d

# lightning bolt polygon (in 64x64 grid)
poly = [(40, 8), (24, 34), (34, 34), (28, 56), (46, 30), (36, 30), (42, 8)]

def inside(x, y):
    inside_flag = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi) + xi):
            inside_flag = not inside_flag
        j = i
    return inside_flag

raw = bytearray()
for y in range(H):
    raw.append(0)  # filter byte
    for x in range(W):
        raw += bytes(bolt if inside(x, y) else bg)

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
png += chunk(b"IEND", b"")

# ICO container with PNG image
icondir = struct.pack("<HHH", 0, 1, 1)
entry = struct.pack("<BBBBHHII",
                    W, H, 0, 0, 1, 32,
                    len(png), 6 + 16)
ico = icondir + entry + png

out = os.path.join(os.path.dirname(__file__), "..", "native", "app.ico")
out = os.path.abspath(out)
with open(out, "wb") as f:
    f.write(ico)
print("wrote", out, len(ico), "bytes")
