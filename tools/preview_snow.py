#!/usr/bin/env python3
"""DESIGN MOCK ONLY — snowflake style options over the dark cloud.
Rows: branched-tip / branched-mid+tip / plain asterisk(current).
Cols: 1..4 flakes.  Output: tools/preview/snow_styles_preview.png
"""
import os, math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CAPS = os.path.join(HERE, "caps")
PREVIEW = os.path.join(HERE, "preview"); os.makedirs(PREVIEW, exist_ok=True)
box = (160, 98, 192, 130)

def cropk(code):
    im = Image.open(f"{CAPS}/cap_{code}.png").convert("RGB").crop(box).convert("RGBA")
    p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if r + g + b < 30:
                p[x, y] = (0, 0, 0, 0)
    return im

def to_gray(im, f):
    im = im.copy(); p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if a:
                p[x, y] = (int(r * f), int(g * f), int(b * f), a)
    return im

plain = cropk(8); dark_cl = to_gray(plain, 0.43)
_p = dark_cl.crop((0, 0, dark_cl.width, int(dark_cl.height * 0.66)))
_bb = _p.getbbox(); puff_src = _p.crop(_bb) if _bb else _p

def cloud_mass(S):
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = int(S * 0.92); h = int(w * puff_src.height / puff_src.width)
    cl = puff_src.resize((w, h), Image.LANCZOS)
    x = (S - w) // 2; top_y = int(S * 0.14); shift = int(S * 0.16)
    canvas.alpha_composite(cl, (x, top_y)); canvas.alpha_composite(cl, (x, top_y + shift))
    return canvas, top_y + shift + h

def xs(S, n):
    lo, hi = 0.24, 0.76
    return [S * 0.5] if n == 1 else [S * (lo + (hi - lo) * i / (n - 1)) for i in range(n)]

BLACK = (0, 0, 0, 255); WHITE = (235, 245, 255, 255)

# SS supersample so the angled branches look like flakes, not noise
def flake(d, cx, cy, R, w, col, barbs):
    for k in range(6):
        a = math.radians(k * 60)
        ca, sa = math.cos(a), math.sin(a)
        tip = (cx + ca * R, cy + sa * R)
        d.line([(cx, cy), tip], fill=col, width=w)
        for frac, blen in barbs:                       # side branches along the arm
            bx, by = cx + ca * R * frac, cy + sa * R * frac
            for s in (1, -1):
                ba = a + s * math.radians(45)
                d.line([(bx, by), (bx + math.cos(ba) * R * blen, by + math.sin(ba) * R * blen)],
                       fill=col, width=w)

def draw_snow(canvas, S, cb, n, style):
    SS = 4
    big = Image.new("RGBA", (S * SS, S * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    R = S * SS * 0.11
    ow = max(3, int(R * 0.42)); iw = max(2, int(R * 0.22))
    cy = int(((cb + S) / 2)) * SS
    cy = min(cy, S * SS - int(R) - 1)
    barbs = {"tip": [(0.68, 0.34)],
             "mid": [(0.5, 0.30), (0.78, 0.30)],
             "ast": []}[style]
    for cx in xs(S, n):
        cxs = cx * SS
        flake(d, cxs, cy, R, ow, BLACK, barbs)
        flake(d, cxs, cy, R, iw, WHITE, barbs)
    canvas.alpha_composite(big.resize((S, S), Image.LANCZOS), (0, 0))

S = 64; Z = 6; GAP = 8
rows = [("tip", "branched (tip)"), ("mid", "branched (mid+tip)"), ("ast", "asterisk (current)")]
cols = [1, 2, 3, 4]
W = len(cols) * (S * Z + GAP) + GAP
H = len(rows) * (S * Z + GAP) + GAP
sheet = Image.new("RGBA", (W, H), (0, 0, 0, 255))
for r, (style, _) in enumerate(rows):
    for c, n in enumerate(cols):
        cell, cb = cloud_mass(S)
        draw_snow(cell, S, cb, n, style)
        sheet.alpha_composite(cell.resize((S * Z, S * Z), Image.NEAREST),
                              (GAP + c * (S * Z + GAP), GAP + r * (S * Z + GAP)))
out = os.path.join(PREVIEW, "snow_styles_preview.png")
sheet.save(out)
print("rows: branched-tip / branched-mid+tip / asterisk   cols: 1-4 ->", out)
