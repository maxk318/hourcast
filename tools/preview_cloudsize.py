#!/usr/bin/env python3
"""DESIGN MOCK ONLY — compare current vs proposed (smaller) precip cloud over
sun and moon, crown-clipped like the watch. Output: tools/preview/cloudsize_preview.png
Grid: rows = Current / Proposed ; cols = Sun / Moon."""
import os, math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CAPS = os.path.join(HERE, "caps")
OUT = os.path.normpath(os.path.join(HERE, "..", "resources", "icons"))
PREVIEW = os.path.join(HERE, "preview"); os.makedirs(PREVIEW, exist_ok=True)
box = (160, 98, 192, 130)

def cropk(code):
    im = Image.open(f"{CAPS}/cap_{code}.png").convert("RGB").crop(box).convert("RGBA")
    p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if r + g + b < 30: p[x, y] = (0, 0, 0, 0)
    return im

def to_gray(im, f):
    im = im.copy(); p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if a: p[x, y] = (int(r * f), int(g * f), int(b * f), a)
    return im

plain = cropk(8); dark_cl = to_gray(plain, 0.43)
_p = dark_cl.crop((0, 0, dark_cl.width, int(dark_cl.height * 0.66)))
_bb = _p.getbbox(); puff = _p.crop(_bb) if _bb else _p
ASPECT = puff.height / puff.width
# flatter crop for the proposed cloud (shorter so it can sit lower and still
# leave room for marks below)
_pf = dark_cl.crop((0, 0, dark_cl.width, int(dark_cl.height * 0.56)))
_bf = _pf.getbbox(); puff_flat = _pf.crop(_bf) if _bf else _pf
ASPECT_FLAT = puff_flat.height / puff_flat.width

def cloud_current(S):
    c = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = int(S * 0.92); h = int(w * ASPECT); cl = puff.resize((w, h), Image.LANCZOS)
    x = (S - w) // 2; top_y = int(S * 0.14); shift = int(S * 0.16)
    c.alpha_composite(cl, (x, top_y)); c.alpha_composite(cl, (x, top_y + shift))
    return c, top_y + shift + h, top_y

def cloud_new(S):
    # FULL cloud shape (rounded bottom + texture preserved), just SQUASHED
    # vertically so it's short -> sits low, reveals the moon top, stays wide.
    c = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = int(S * 0.92); h = int(S * 0.47)        # squashed height (vs proportional ~0.61S)
    cl = puff.resize((w, h), Image.LANCZOS)      # puff = full 0.66 crop (keeps texture)
    x = (S - w) // 2; bottom = int(S * 0.80); shift = int(S * 0.10)
    lower_y = bottom - h; upper_y = lower_y - shift
    c.alpha_composite(cl, (x, upper_y)); c.alpha_composite(cl, (x, lower_y))
    return c, bottom, upper_y

# Clip the celestial below `cut_y` (icon coords) so it tucks behind the cloud
# and nothing shows in the precip band -> no fixed-fraction gap.
def crown(cel, cut_y):
    out = cel.copy(); p = out.load()
    for y in range(max(0, cut_y), out.height):
        for x in range(out.width):
            r, g, b, a = p[x, y]
            if a: p[x, y] = (r, g, b, 0)
    return out

def bolts(canvas, S, cb, n=2):
    d = ImageDraw.Draw(canvas)
    pts = [(0.52, 0), (0.18, 0.56), (0.46, 0.56), (0.30, 1.0), (0.84, 0.40), (0.54, 0.40), (0.70, 0)]
    top = cb - S * 0.06; bh = (S - 2) - top; bw = S * 0.26
    xs = [S * 0.5] if n == 1 else [S * (0.30 + 0.40 * i / (n - 1)) for i in range(n)]
    ow = max(2, int(S * 0.03))
    for cx in xs:
        poly = [((cx - bw / 2) + px * bw, top + py * bh) for px, py in pts]
        d.polygon(poly, fill=(255, 205, 35, 255), outline=(0, 0, 0, 255), width=ow)

S = 56; Z = 6
sun = Image.open(f"{OUT}/wx_clear_day_lg.png").convert("RGBA")
moon = Image.open(f"{OUT}/moon_ctr_05.png").convert("RGBA")
if sun.size != (S, S): sun = sun.resize((S, S), Image.LANCZOS)
if moon.size != (S, S): moon = moon.resize((S, S), Image.LANCZOS)

def tile(builder, cel):
    cloud, cb, top_y = builder(S)
    t = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    t.alpha_composite(crown(cel, cb))    # clip celestial at this cloud's bottom
    t.alpha_composite(cloud)
    bolts(t, S, cb, 2)
    return t, top_y

rows = [("Current", cloud_current), ("Proposed", cloud_new)]
cols = [("Sun", sun), ("Moon", moon)]
GAP = 10
sheet = Image.new("RGBA", (2 * (S * Z + GAP) + GAP, 2 * (S * Z + GAP) + GAP), (0, 0, 0, 255))
for r, (_, builder) in enumerate(rows):
    for cc, (_, cel) in enumerate(cols):
        t, top_y = tile(builder, cel)
        sheet.alpha_composite(t.resize((S * Z, S * Z), Image.NEAREST),
                              (GAP + cc * (S * Z + GAP), GAP + r * (S * Z + GAP)))
        print(f"{rows[r][0]:9} {cols[cc][0]:4} cloud-top at {top_y}/{S} = {top_y/S:.0%}")
sheet.save(os.path.join(PREVIEW, "cloudsize_preview.png"))
print("puff aspect:", round(ASPECT, 3), "-> tools/preview/cloudsize_preview.png (rows: current/proposed, cols: sun/moon)")
