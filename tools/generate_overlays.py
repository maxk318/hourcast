#!/usr/bin/env python3
"""Cloud-only overlays (no celestial) for Option A, + a composited preview.
Clear/partly/overcast clouds sit low so the celestial peeks above; precip
states use a big two-cloud mass kept high, with the celestial clipped to a
crown (see CROWN_FRAC) so only its top shows from behind the clouds."""
import os, math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CAPS = os.path.join(HERE, "caps")
OUT  = os.path.normpath(os.path.join(HERE, "..", "resources", "icons"))
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
    # Scale the WHOLE cloud (body + its outline/texture lines) by brightness f.
    # The internal lines stay proportionally darker than the body and still show.
    # f near 1.0 -> near-white (plain overcast); f low -> dark (storm/precip cloud).
    im = im.copy(); p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if a:
                p[x, y] = (int(r * f), int(g * f), int(b * f), a)
    return im

def marks_only(code):
    im = cropk(code); p = im.load(); W, H = im.size
    def isw(q):
        r, g, b, a = q
        return a and min(r, g, b) >= 165 and max(r, g, b) - min(r, g, b) <= 45
    in_c = False; cut = 0
    for y in range(H):
        wc = sum(1 for x in range(W) if isw(p[x, y]))
        op = sum(1 for x in range(W) if p[x, y][3] > 0)
        if wc >= W * 0.30: in_c = True; cut = y
        elif in_c:
            if op == 0: break
            cut = y
    cut = min(cut, int(H * 0.62))
    for y in range(H):
        for x in range(W):
            if y <= cut: p[x, y] = (0, 0, 0, 0)
    return im

def dilate(im, dxs, dys, passes=1):
    for _ in range(passes):
        src = im.copy(); sp = src.load(); px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = sp[x, y]
                if a > 0:
                    for ddx in dxs:
                        for ddy in dys:
                            nx, ny = x + ddx, y + ddy
                            if 0 <= nx < im.width and 0 <= ny < im.height and px[nx, ny][3] == 0:
                                px[nx, ny] = (r, g, b, a)
    return im

plain  = cropk(8)             # white plain cloud (32px)
light_cl = to_gray(plain, 0.90)   # plain overcast: near-white, dry sky
dark_cl  = to_gray(plain, 0.43)   # precip cloud: dark, because rain/snow clouds ARE dark

# Full dark cloud (trimmed) -- the SAME complete cloud shape as the white
# overcast cloud, so it keeps its rounded, textured bottom. The precip cloud is
# a single one of these, squashed short and raised so the sun/moon top peeks
# above it (no cropped/stacked pieces that would leave a flat cut-off bottom).
_dbb = dark_cl.getbbox()
dark_full = dark_cl.crop(_dbb) if _dbb else dark_cl
marks  = {
    "rain":  dilate(marks_only(15), (-1, 0, 1), (0,), passes=1),       # thicker vertical lines
    "snow":  dilate(marks_only(23), (-1, 0, 1), (-1, 0, 1), passes=1), # bolder asterisks
    "storm": marks_only(27),
}

# Fraction of the celestial (from its top) that stays visible behind precip
# clouds. The C side clips the sun/moon to this top slice so nothing of it
# shows below the cloud mass -- only a crown peeks up from behind.
CROWN_FRAC = 0.34

def diagonal_rain(W, H):
    # Light-blue rain as slanted streaks, supersampled then downscaled so the
    # diagonal edges stay smooth at icon size.
    SS = 4
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    col = (120, 190, 255, 255)            # light blue
    sw = max(2, int(W * SS * 0.075))      # streak thickness (bolder)
    run = int(W * SS * 0.16)              # horizontal slant over the streak
    n = 4
    pad = sw                              # keep strokes fully inside the canvas
    x_first = pad + run                   # bottom of leftmost streak clears the edge
    x_last = W * SS - pad                 # top of rightmost streak clears the edge
    y0, y1 = int(H * SS * 0.05), int(H * SS * 0.95)
    for i in range(n):
        x = x_first + (x_last - x_first) * i / (n - 1)
        d.line([(x, y0), (x - run, y1)], fill=col, width=sw)
    return im.resize((W, H), Image.LANCZOS)

def draw_snow(W, H):
    # Three big, evenly-spaced six-spoke flakes, supersampled then downscaled.
    SS = 4
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    col = (235, 245, 255, 255)            # near-white, faint icy blue
    R = int(W * SS * 0.16)                # spoke length (sized off width = big)
    sw = max(2, int(R * 0.38))            # spoke thickness
    centers = [(0.20, 0.32), (0.5, 0.70), (0.80, 0.32)]   # triangular layout
    for cx, cy in centers:
        x, y = cx * W * SS, cy * H * SS
        for k in range(3):                # 3 lines -> 6 spokes
            a = math.radians(k * 60)
            dx, dy = math.cos(a) * R, math.sin(a) * R
            d.line([(x - dx, y - dy), (x + dx, y + dy)], fill=col, width=sw)
    return im.resize((W, H), Image.LANCZOS)

def draw_bolt(W, H):
    # Two bold lightning bolts side by side, yellow with a thin dark edge.
    SS = 4
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = [(0.52, 0.00), (0.18, 0.56), (0.46, 0.56),
           (0.30, 1.00), (0.84, 0.40), (0.54, 0.40), (0.70, 0.00)]
    ow = max(2, int(W * SS * 0.02))
    def bolt(ox, oy, sx, sy):
        poly = [((ox + px * sx) * W * SS, (oy + py * sy) * H * SS) for px, py in pts]
        d.polygon(poly, fill=(255, 205, 35, 255), outline=(120, 70, 0, 255), width=ow)
    bolt(0.02, 0.00, 0.52, 0.84)          # left bolt, a bit higher
    bolt(0.50, 0.14, 0.52, 0.84)          # right bolt, a bit lower
    return im.resize((W, H), Image.LANCZOS)

def precip_cloud_mass(S):
    # A single full dark cloud (rounded, textured bottom intact), squashed short
    # and raised so the sun/moon top peeks above it. One cloud, not a stack of
    # cropped pieces -- so the bottom keeps its natural shape, no flat cut-off.
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = int(S * 0.92)
    h = int(S * 0.57)                    # squashed (vs proportional ~0.68S) to reveal
                                         # the celestial top while keeping the full shape
    cl = dark_full.resize((w, h), Image.LANCZOS)
    x = (S - w) // 2
    bottom = int(S * 0.80)               # anchor the cloud bottom (matches the C mark band)
    canvas.alpha_composite(cl, (x, bottom - h))
    # Snap to pure palette colours (DarkGray body + black texture/outline) so the
    # watch's 64-colour display can't dither the body into noise that hides the
    # thin texture lines. Keeps the texture crisp even at the small 42px ring.
    p = canvas.load()
    for yy in range(S):
        for xx in range(S):
            r, g, b, a = p[xx, yy]
            if a > 0:
                v = 85 if r >= 80 else 0      # GColorDarkGray body / GColorBlack lines
                p[xx, yy] = (v, v, v, 255 if a > 128 else a)
    # Despeckle: drop tiny stray gray blobs pinched off the cloud edges by the
    # squash (e.g. the out-of-place dots at the bottom corners).
    visited = [[False] * S for _ in range(S)]
    def is_body(xx, yy):
        c = p[xx, yy]; return c[3] > 0 and c[0] == 85
    for yy in range(S):
        for xx in range(S):
            if is_body(xx, yy) and not visited[yy][xx]:
                stack = [(xx, yy)]; visited[yy][xx] = True; cells = [(xx, yy)]
                while stack:
                    ax, ay = stack.pop()
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = ax + dx, ay + dy
                        if 0 <= nx < S and 0 <= ny < S and is_body(nx, ny) and not visited[ny][nx]:
                            visited[ny][nx] = True; stack.append((nx, ny)); cells.append((nx, ny))
                if len(cells) < 20:
                    for cx, cy in cells:
                        p[cx, cy] = (0, 0, 0, 0)
    return canvas, bottom

def flake_sprite(diam):
    # One branched-tip snowflake (white, dark outline) on a transparent square,
    # supersampled then downscaled. The C side stamps this N times for snow.
    SS = 4
    P = diam * SS
    im = Image.new("RGBA", (P, P), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = P / 2.0
    R = P * 0.42
    ow = max(3, int(R * 0.52)); iw = max(2, int(R * 0.30))
    barbs = [(0.68, 0.34)]
    def flake(w, col):
        for k in range(6):
            a = math.radians(k * 60); ca, sa = math.cos(a), math.sin(a)
            d.line([(cx, cy), (cx + ca * R, cy + sa * R)], fill=col, width=w)
            for frac, blen in barbs:
                bx, by = cx + ca * R * frac, cy + sa * R * frac
                for s in (1, -1):
                    ba = a + s * math.radians(45)
                    d.line([(bx, by), (bx + math.cos(ba) * R * blen, by + math.sin(ba) * R * blen)],
                           fill=col, width=w)
    flake(ow, (0, 0, 0, 255))
    flake(iw, (235, 245, 255, 255))
    return im.resize((diam, diam), Image.LANCZOS)

def build_cloud(state, S):
    # Only the CLOUD body now: rays/flakes/bolts are drawn on-watch in C so the
    # mark count can vary with precipitation probability. "precip" is the bare
    # dark two-cloud mass used for rain/snow/storm alike.
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if state == "partly":
        w = int(S * 0.62)
        c = plain.resize((w, int(w * 0.74)), Image.LANCZOS)   # small WHITE cloud
        canvas.alpha_composite(c, ((S - c.width) // 2, S - c.height - 1))
    elif state == "overcast":
        w = int(S * 0.96)
        c = light_cl.resize((w, int(w * 0.74)), Image.LANCZOS)
        canvas.alpha_composite(c, ((S - w) // 2, S - c.height - 1))
    elif state == "precip":
        cloud, _ = precip_cloud_mass(S)
        canvas.alpha_composite(cloud, (0, 0))
    return canvas

# ring = 42 (temps mode), xl = 46 (off-mode bigger ring), ctr = 56 (center)
SIZES = {"ring": 42, "xl": 46, "ctr": 56}
for name, S in SIZES.items():
    for st in ["partly", "overcast", "precip"]:
        build_cloud(st, S).save(os.path.join(OUT, f"ov_{st}_{name}.png"))
    flake_sprite(max(10, round(S * 0.30))).save(os.path.join(OUT, f"flake_{name}.png"))

# ---- composited preview (rows: day, night; cols: clear..storm) ----
sun  = Image.open(f"{OUT}/wx_clear_day_lg.png").convert("RGBA")          # full sun
moon = Image.open(f"{OUT}/moon_ctr_05.png").convert("RGBA")             # single moon size
S = 56
cols = ["clear", "partly", "overcast", "precip"]
Z = 3

def crown(cel):
    # Keep only the top CROWN_FRAC of the celestial (what the C side will clip
    # to for precip states), so nothing of it shows below the cloud mass.
    bb = cel.getbbox()
    if not bb:
        return cel
    cut = bb[1] + int((bb[3] - bb[1]) * CROWN_FRAC)
    out = cel.copy(); p = out.load()
    for y in range(cut, out.height):
        for x in range(out.width):
            r, g, b, a = p[x, y]
            if a:
                p[x, y] = (r, g, b, 0)
    return out

PRECIP = {"precip"}
sheet = Image.new("RGBA", (S * Z * len(cols) + (len(cols) + 1) * 6, S * Z * 2 + 3 * 6), (0, 0, 0, 255))
for r, night in enumerate([False, True]):
    for cidx, st in enumerate(cols):
        tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        cel = moon if night else sun
        if st == "clear":
            tile.alpha_composite(cel)
        else:
            tile.alpha_composite(crown(cel) if st in PRECIP else cel)
            tile.alpha_composite(Image.open(f"{OUT}/ov_{st}_ctr.png").convert("RGBA"))
        sheet.alpha_composite(tile.resize((S * Z, S * Z), Image.NEAREST),
                              (6 + cidx * (S * Z + 6), 6 + r * (S * Z + 6)))
sheet.save(os.path.join(PREVIEW, "overlay_full_preview.png"))
print("overlays generated; preview -> tools/preview/overlay_full_preview.png (row1 day, row2 night)")
