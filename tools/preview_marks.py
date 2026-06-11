#!/usr/bin/env python3
"""DESIGN MOCK ONLY — does not touch shipping assets.
Renders the proposed precip marks (rays/flakes/bolts) at counts 1-4 over the
dark precip cloud, outlined like the clock hands, so we can approve the look
before porting the drawing into the C watchface. Output: tools/preview/precip_marks_preview.png
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

plain = cropk(8)
dark_cl = to_gray(plain, 0.43)
PUFF_FRAC = 0.66
_p = dark_cl.crop((0, 0, dark_cl.width, int(dark_cl.height * PUFF_FRAC)))
_bb = _p.getbbox()
puff_src = _p.crop(_bb) if _bb else _p

def cloud_mass(S):
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = int(S * 0.92)
    h = int(w * puff_src.height / puff_src.width)
    cl = puff_src.resize((w, h), Image.LANCZOS)
    x = (S - w) // 2
    top_y = int(S * 0.14); shift = int(S * 0.16)
    canvas.alpha_composite(cl, (x, top_y))
    canvas.alpha_composite(cl, (x, top_y + shift))
    return canvas, top_y + shift + h

# ---- mark x-positions: n marks evenly spread inside a fixed safe window ----
def xs(S, n):
    lo, hi = 0.24, 0.76          # keep everything inside the icon, any n
    if n == 1:
        return [S * 0.5]
    return [S * (lo + (hi - lo) * i / (n - 1)) for i in range(n)]

BLACK = (0, 0, 0, 255)

def draw_rays(canvas, S, cb, n):
    d = ImageDraw.Draw(canvas)
    blue = (120, 190, 255, 255)
    ow = max(3, int(S * 0.085)); iw = max(2, ow - int(S * 0.045))
    run = int(S * 0.09)
    y0 = int(cb - S * 0.10); y1 = int(S - S * 0.02)
    for x in xs(S, n):
        d.line([(x, y0), (x - run, y1)], fill=BLACK, width=ow)
    for x in xs(S, n):
        d.line([(x, y0), (x - run, y1)], fill=blue, width=iw)

def draw_flakes(canvas, S, cb, n):
    # branched-tip snowflake, supersampled (this becomes a smooth stamped sprite)
    white = (235, 245, 255, 255)
    SS = 4
    big = Image.new("RGBA", (S * SS, S * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    R = S * SS * 0.11
    ow = max(3, int(R * 0.42)); iw = max(2, int(R * 0.22))
    cy = min(int((cb + S) / 2), S - int(R / SS) - 1) * SS
    barbs = [(0.68, 0.34)]
    def flake(cx, w, col):
        for k in range(6):
            a = math.radians(k * 60); ca, sa = math.cos(a), math.sin(a)
            d.line([(cx, cy), (cx + ca * R, cy + sa * R)], fill=col, width=w)
            for frac, blen in barbs:
                bx, by = cx + ca * R * frac, cy + sa * R * frac
                for s in (1, -1):
                    ba = a + s * math.radians(45)
                    d.line([(bx, by), (bx + math.cos(ba) * R * blen, by + math.sin(ba) * R * blen)],
                           fill=col, width=w)
    for cx in xs(S, n):
        cxs = cx * SS
        flake(cxs, ow, BLACK)
        flake(cxs, iw, white)
    canvas.alpha_composite(big.resize((S, S), Image.LANCZOS), (0, 0))

def draw_bolts(canvas, S, cb, n):
    d = ImageDraw.Draw(canvas)
    yellow = (255, 205, 35, 255)
    pts = [(0.52, 0.0), (0.18, 0.56), (0.46, 0.56),
           (0.30, 1.0), (0.84, 0.40), (0.54, 0.40), (0.70, 0.0)]
    bw = S * 0.32
    top = cb - S * 0.20; bot = S - 1; bh = bot - top
    ow = max(2, int(S * 0.03))
    for cx in xs(S, n):
        poly = [((cx - bw / 2) + px * bw, top + py * bh) for px, py in pts]
        d.polygon(poly, fill=yellow, outline=BLACK, width=ow)

DRAW = {"rain": draw_rays, "snow": draw_flakes, "storm": draw_bolts}

S = 64; Z = 5; GAP = 8
rows = ["rain", "snow", "storm"]
cols = [1, 2, 3, 4]
W = len(cols) * (S * Z + GAP) + GAP
H = len(rows) * (S * Z + GAP) + GAP
sheet = Image.new("RGBA", (W, H), (0, 0, 0, 255))
for r, kind in enumerate(rows):
    for c, n in enumerate(cols):
        cell, cb = cloud_mass(S)
        DRAW[kind](cell, S, cb, n)
        sheet.alpha_composite(cell.resize((S * Z, S * Z), Image.NEAREST),
                              (GAP + c * (S * Z + GAP), GAP + r * (S * Z + GAP)))
out = os.path.join(PREVIEW, "precip_marks_preview.png")
sheet.save(out)
print("rows: rain / snow / storm   cols: 1 / 2 / 3 / 4 marks ->", out)
