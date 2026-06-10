#!/usr/bin/env python3
"""Pre-render moon-phase bitmaps (v3).
 - 10 phases: index 0 = new (solid gray), 5 = full (solid blue),
   1-4 waxing crescents (blue from left), 6-9 waning (gray from left)
 - curved terminator on the crescents (never a flat half)
 - stylized dark crater spots on every phase (own pattern, not a tracing)
 - thick rim border + thinner terminator line
two sizes (ring 42, center 56)."""
import math, os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "resources", "icons"))
PREVIEW = os.path.join(HERE, "preview"); os.makedirs(PREVIEW, exist_ok=True)
GRAY   = (84, 84, 84, 255)
BLUE   = (105, 181, 221, 255)
BORDER = (38, 38, 38, 255)
CLEAR  = (0, 0, 0, 0)
SS = 4

# (lit fraction, mode): mode None = solid disc (a=0 gray / a=1 blue)
SPECS = [(0.0, None), (0.20, 'wax'), (0.40, 'wax'), (0.60, 'wax'), (0.80, 'wax'),
         (1.0, None), (0.80, 'wan'), (0.60, 'wan'), (0.40, 'wan'), (0.20, 'wan')]
N = len(SPECS)

# stylized maria (cx_frac, cy_frac, r_frac), shifted up. Only drawn on the
# SPOTTED set (display mode 2), where no temp circle covers the moon.
CRATERS = [(-0.30, -0.44, 0.26), (0.18, -0.28, 0.20), (0.32, 0.06, 0.15),
           (-0.16, 0.20, 0.22), (-0.42, -0.04, 0.12), (0.06, -0.06, 0.10)]

def shade(c):
    return (int(c[0] * 0.5), int(c[1] * 0.5), int(c[2] * 0.5), c[3])

def in_crater(xn, yn):
    for cxf, cyf, rf in CRATERS:
        if (xn - cxf) ** 2 + (yn - cyf) ** 2 <= rf * rf:
            return True
    return False

def render(i, S, spots=False):
    a, mode = SPECS[i]
    s = S * SS
    img = Image.new("RGBA", (s, s), CLEAR)
    px = img.load()
    R = (s / 2.0 - 4 * SS) * 0.95  # 5% smaller disc
    cx = cy = s / 2.0
    rim, term = 2.6 * SS, 1.3 * SS
    waxing = (mode == 'wax')
    for yy in range(s):
        y = yy - cy
        if abs(y) > R:
            continue
        w = math.sqrt(R * R - y * y)
        if mode is None:
            xt = None
        elif waxing:
            xt = (2 * a - 1) * w
        else:
            xt = (1 - 2 * a) * w
        for xx in range(s):
            x = xx - cx
            de = w - abs(x)
            if de < 0:
                continue
            if mode is None:
                base = BLUE if a >= 0.999 else GRAY
            elif waxing:
                base = BLUE if x <= xt else GRAY
            else:
                base = GRAY if x <= xt else BLUE
            if spots and in_crater(x / R, y / R):
                base = shade(base)
            if de < rim:                 # only the outside rim is bordered
                px[xx, yy] = BORDER
            else:
                px[xx, yy] = base
    return img.resize((S, S), Image.LANCZOS)

for i in range(N):
    # all moons are spotted now (the temp sits on them as outlined digits, not a
    # filled pill, so the crater detail shows in every mode)
    render(i, 42, spots=True).save(os.path.join(OUT, "moon_ring_%02d.png" % i))  # numeral-mode ring
    render(i, 56, spots=True).save(os.path.join(OUT, "moon_ctr_%02d.png" % i))   # center (all modes)
    render(i, 46, spots=True).save(os.path.join(OUT, "moon_xl_%02d.png" % i))    # 46px ring (modes 0/1)
    render(i, 42, spots=True).save(os.path.join(OUT, "moon_sp_%02d.png" % i))    # 42px ring (mode 2)
# remove the now-unused small behind-cloud moons
for n in range(12):
    for pre in ("moon_sm_ring_%02d.png", "moon_sm_ctr_%02d.png"):
        p = os.path.join(OUT, pre % n)
        if os.path.exists(p):
            os.remove(p)
for n in (10, 11):  # remove stale extras from the old 12-set
    for pre in ("moon_ring_%02d.png", "moon_ctr_%02d.png"):
        p = os.path.join(OUT, pre % n)
        if os.path.exists(p):
            os.remove(p)

Z = 3
cell = 42 * Z
sheet = Image.new("RGBA", (cell * 5 + 6 * 8, cell * 2 + 3 * 8), (20, 20, 20, 255))
for i in range(N):
    im = Image.open(os.path.join(OUT, "moon_ring_%02d.png" % i)).resize((cell, cell), Image.NEAREST)
    col, row = i % 5, i // 5
    sheet.paste(im, (8 + col * (cell + 8), 8 + row * (cell + 8)), im)
sheet.save(os.path.join(PREVIEW, "moon_phases.png"))
print("generated", N, "phases x2 (solid new/full, craters); sheet -> tools/preview/moon_phases.png")
