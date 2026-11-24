#!/usr/bin/env python3
"""Draw the QUARTET icon as a 960x540 PNG using only the Python stdlib.

Motif: four glowing bowed strings over a warm cello-brown body, crossed by one
diagonal bow.  Reads at tile size; no text.

Run from the repository root:  python tools/make_icon.py
Writes firmware/quartet/icon.png
"""

import math
import struct
import zlib
from pathlib import Path

W, H = 960, 540
SS = 2
SW, SH = W * SS, H * SS

BG = (14, 10, 20)
PANEL = (35, 22, 30)
WALNUT = (104, 48, 32)
WALNUT_HI = (168, 86, 44)
GOLD = (242, 189, 82)
CREAM = (255, 240, 210)
VIOLET = (182, 150, 255)
BLUE = (86, 178, 255)
AVO = (162, 196, 99)
ROSIN = (255, 214, 150)


def blank():
    px = bytearray(SW * SH * 3)
    for y in range(SH):
        t = y / SH
        for x in range(SW):
            u = x / SW
            r = BG[0] + int(14 * t + 8 * math.sin(u * math.pi))
            g = BG[1] + int(8 * t)
            b = BG[2] + int(18 * (1 - t))
            i = (y * SW + x) * 3
            px[i] = max(0, min(255, r))
            px[i + 1] = max(0, min(255, g))
            px[i + 2] = max(0, min(255, b))
    return px


def put(px, x, y, c, a=1.0):
    if x < 0 or y < 0 or x >= SW or y >= SH or a <= 0:
        return
    i = (y * SW + x) * 3
    if a >= 1:
        px[i], px[i + 1], px[i + 2] = c
    else:
        px[i] = int(px[i] * (1 - a) + c[0] * a)
        px[i + 1] = int(px[i + 1] * (1 - a) + c[1] * a)
        px[i + 2] = int(px[i + 2] * (1 - a) + c[2] * a)


def disc(px, cx, cy, r, c, a=1.0):
    rr = r * r
    for y in range(max(0, int(cy - r - 1)), min(SH, int(cy + r + 2))):
        for x in range(max(0, int(cx - r - 1)), min(SW, int(cx + r + 2))):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if d <= rr:
                put(px, x, y, c, a * max(0.0, 1.0 - d / rr) ** 0.5)


def line(px, x0, y0, x1, y1, width, c, a=1.0):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    hw = width * 0.5
    for k in range(steps + 1):
        t = k / steps
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        r = int(hw + 1)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                d = math.hypot(dx, dy)
                if d <= hw:
                    put(px, int(x + dx), int(y + dy), c, a * max(0.0, 1.0 - d / (hw + 0.5)))


def body(px):
    # A stylised cello-ish arch: two warm lobes behind the strings.
    for y in range(int(SH * 0.30), int(SH * 0.92)):
        t = (y - SH * 0.30) / (SH * 0.62)
        half = SW * (0.10 + 0.29 * math.sin(math.pi * (0.12 + 0.76 * t)))
        for x in range(int(SW / 2 - half), int(SW / 2 + half)):
            u = abs(x - SW / 2) / max(1.0, half)
            shade = max(0.0, 1.0 - u * u)
            c = tuple(int(WALNUT[k] * (0.55 + 0.65 * shade) + WALNUT_HI[k] * 0.10 * shade)
                      for k in range(3))
            put(px, x, y, c, 0.55 + 0.35 * shade)


def draw():
    px = blank()
    body(px)

    # fingerboard shadow
    line(px, SW * 0.34, SH * 0.16, SW * 0.66, SH * 0.16, 15 * SS, (12, 8, 16), 0.85)

    # bridge, an amber arch
    for k in range(9):
        t = k / 8
        x = SW * (0.36 + 0.28 * t)
        y = SH * (0.66 - 0.045 * math.sin(math.pi * t))
        line(px, x, y, x + (x - SW * 0.5) * 0.10, SH * 0.70, 3 * SS, GOLD, 0.55)

    # four strings, slightly fanned
    cols = [GOLD, AVO, VIOLET, BLUE]
    for i in range(4):
        x0 = SW * (0.40 + i * 0.066)
        x1 = SW * (0.45 + i * 0.033)
        line(px, x0, SH * 0.10, x1, SH * 0.86, (3.4 - i * 0.4) * SS, cols[i], 0.95)
        disc(px, x1, SH * 0.86, 6 * SS, CREAM, 0.5)

    # bow: pale wood with a bright rosin edge
    line(px, SW * 0.10, SH * 0.78, SW * 0.92, SH * 0.30, 11 * SS, (78, 46, 30), 1.0)
    line(px, SW * 0.10, SH * 0.78, SW * 0.92, SH * 0.30, 4 * SS, ROSIN, 0.95)
    disc(px, SW * 0.90, SH * 0.31, 13 * SS, GOLD, 0.9)
    line(px, SW * 0.09, SH * 0.79, SW * 0.16, SH * 0.75, 16 * SS, (24, 14, 18), 1.0)

    # a few rosin flecks
    for i in range(24):
        t = i / 23
        x = SW * (0.15 + 0.72 * t) + math.sin(i * 7.1) * 9 * SS
        y = SH * (0.74 - 0.42 * t) + math.cos(i * 3.7) * 7 * SS
        disc(px, x, y, 1.4 * SS, ROSIN, 0.35)

    # downsample
    out = bytearray(W * H * 3)
    for y in range(H):
        for x in range(W):
            r = g = b = 0
            for dy in range(SS):
                for dx in range(SS):
                    i = ((y * SS + dy) * SW + (x * SS + dx)) * 3
                    r += px[i]; g += px[i + 1]; b += px[i + 2]
            n = SS * SS
            j = (y * W + x) * 3
            out[j] = r // n; out[j + 1] = g // n; out[j + 2] = b // n
    return out


def png(rgb):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw.extend(rgb[y * W * 3:(y + 1) * W * 3])
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main():
    out = Path(__file__).resolve().parents[1] / "firmware" / "quartet" / "icon.png"
    out.write_bytes(png(draw()))
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
