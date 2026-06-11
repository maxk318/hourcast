#!/usr/bin/env python3
"""DESIGN MOCK ONLY — per-hour tide square that sits BEHIND each inner-ring
element (number / hash / temp). Row 1: the 5 fill increments with a number on
top. Row 2: a 12-slot around-the-ring sequence so you can see the tide wave
emerge. Output: tools/preview/tide_preview.png"""
import os, math
from PIL import Image, ImageDraw, ImageFont

PREVIEW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview")
os.makedirs(PREVIEW, exist_ok=True)

CELL = 30          # real per-hour square size (px)
SS = 5             # supersample
BLUE = (60, 140, 230, 255)
EDGE = (90, 90, 90, 255)
AMP = 0.07
FREQ = 1.2
WHITE = (255, 255, 255, 255)
BLACK = (0, 0, 0, 255)

def _font(px):
    for path in ("/System/Library/Fonts/Helvetica.ttc",
                 "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                 "/Library/Fonts/Arial.ttf"):
        try:
            return ImageFont.truetype(path, px)
        except Exception:
            pass
    return ImageFont.load_default()

def cell(level, label, phase=0.4):
    W = H = CELL * SS
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    base = (1 - level / 100.0) * H
    amp = H * AMP
    pts = []
    for px in range(0, W + 1, 3):
        pts.append((px, base - amp * math.sin(2 * math.pi * FREQ * px / W + phase)))
    pts += [(W, H), (0, H)]
    d.polygon(pts, fill=BLUE)        # just the blue fill -- no box outline
    if label:
        f = _font(int(H * 0.62))
        bb = d.textbbox((0, 0), label, font=f)
        tx = (W - (bb[2] - bb[0])) / 2 - bb[0]
        ty = (H - (bb[3] - bb[1])) / 2 - bb[1]
        for dx in (-SS, 0, SS):
            for dy in (-SS, 0, SS):
                if dx or dy:
                    d.text((tx + dx, ty + dy), label, font=f, fill=BLACK)
        d.text((tx, ty), label, font=f, fill=WHITE)
    return im.resize((CELL, CELL), Image.LANCZOS)

Z = 6; GAP = 10
OUT = os.path.expanduser("~/Downloads")
LEVELS = [15, 35, 65, 85, 100]
# row 2: 12 hours, tide follows a gentle wave, bucketed to the 5 increments
def bucket(v):
    return min(LEVELS, key=lambda L: abs(L - v))
# clean rise-then-fall tide across the 12 hours; label each slot with its HOUR
# (not a temp) so it's obvious the blue fill = tide, the number = the slot.
seq = [15, 35, 65, 85, 100, 100, 85, 65, 35, 15, 15, 35]
hours = [str((h % 12) or 12) for h in range(12, 24)]

def render(rows, fname):
    ncols = max(len(r) for r in rows)
    W = ncols * (CELL * Z + GAP) + GAP
    H = len(rows) * (CELL * Z + GAP) + GAP
    sheet = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    for r, row in enumerate(rows):
        x = GAP
        for lv, lab in row:
            sheet.alpha_composite(cell(lv, lab).resize((CELL * Z, CELL * Z), Image.NEAREST),
                                  (x, GAP + r * (CELL * Z + GAP)))
            x += CELL * Z + GAP
    sheet.save(os.path.join(OUT, fname)); return os.path.join(OUT, fname)

row1 = [(lv, str(lv)) for lv in LEVELS]
row2 = [(seq[k], hours[k]) for k in range(12)]
print("wrote:")
print(" ", render([row1], "hourcast_tide_increments.png"), "(5 fill levels)")
print(" ", render([row2], "hourcast_tide_sequence.png"), "(12-hour ring run)")
