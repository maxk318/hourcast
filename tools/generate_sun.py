#!/usr/bin/env python3
"""Symmetric 12-ray sun matching the original's colors. Writes the three sun
sizes used by the watchface: 42 (numerals/temps ring), 56 (center), 46 (hash)."""
import math, os
from PIL import Image, ImageDraw

HERE    = os.path.dirname(os.path.abspath(__file__))
OUT     = os.path.normpath(os.path.join(HERE, "..", "resources", "icons"))
PREVIEW = os.path.join(HERE, "preview"); os.makedirs(PREVIEW, exist_ok=True)
CREAM   = (245, 205, 80, 255)   # deeper golden yellow (stands out vs white text + gray clouds)
PINK    = (200, 85, 20, 255)   # dark orange outline (clear against the gold body)
SS      = 8
RAYS    = 12

def make_sun(S):
    s = S * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = s / 2.0
    Ro = s * 0.46      # ray tip radius
    Ri = s * 0.30      # valley radius (disc edge)
    outline = max(2, int(s * 0.022))
    # build the 24-point star
    pts = []
    for i in range(RAYS * 2):
        ang = math.pi * i / RAYS - math.pi / 2
        r = Ro if i % 2 == 0 else Ri
        pts.append((c + math.cos(ang) * r, c + math.sin(ang) * r))
    d.polygon(pts, fill=CREAM, outline=PINK)
    # thicken the outline by redrawing the edges as lines
    for i in range(len(pts)):
        d.line([pts[i], pts[(i + 1) % len(pts)]], fill=PINK, width=outline)
    # central disc + soft pink rim (where the temp sits)
    Rd = Ri - outline * 0.4
    d.ellipse([c - Rd, c - Rd, c + Rd, c + Rd], fill=CREAM, outline=PINK, width=outline)
    return img.resize((S, S), Image.LANCZOS)

# write the three sizes the watchface uses
make_sun(42).save(os.path.join(OUT, "wx_clear_day.png"))      # numerals/temps ring
make_sun(56).save(os.path.join(OUT, "wx_clear_day_lg.png"))   # center
make_sun(46).save(os.path.join(OUT, "wx_clear_day_xl.png"))   # hash mode ring

# preview at the center size (56), zoomed on black
S = 56
sun = make_sun(S)
Z = 8
prev = Image.new("RGBA", (S * Z, S * Z), (0, 0, 0, 255))
prev.alpha_composite(sun.resize((S * Z, S * Z), Image.NEAREST))
prev.convert("RGB").save(os.path.join(PREVIEW, "sun_new.png"))
print("wrote wx_clear_day.png/_lg/_xl; preview -> tools/preview/sun_new.png")
