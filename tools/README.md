# Asset generators

These scripts generate the bundled weather/clock art in `../resources/icons/`.
They write directly into that folder and drop zoomed previews in `tools/preview/`
(gitignored). Requires Python 3 + Pillow (`pip install pillow`).

```sh
cd tools
python3 generate_sun.py        # the golden sun (wx_clear_day.png / _lg / _xl)
python3 generate_moons.py      # the 10 moon phases, 4 sizes (moon_ring/ctr/xl/sp_*)
python3 generate_overlays.py   # cloud overlays for partly/overcast/rain/snow/storm
```

What each produces:

- **generate_sun.py** — a fully procedural 12-ray sun. Colors are at the top
  (`CREAM` body, `PINK` outline). Sizes: 42 (numerals/temps ring), 56 (center),
  46 (hash/clock-numbers ring).
- **generate_moons.py** — procedural moon phases (gray + soft blue, crater
  spots, curved terminator). Emits ring (42), center (56), xl (46), and sp (42)
  sets — all spotted.
- **generate_overlays.py** — the cloud overlays. The cloud and precip marks are
  cropped from the captured reference frames in `tools/caps/` (`cap_8` cloud,
  `cap_15` rain, `cap_23` snow, `cap_27` storm), recolored to a dark gray, and
  composited under the sun/moon at runtime by the watchface.

## Notes

- `tools/caps/` holds the captured reference frames the overlay generator needs.
- The launcher icon (`resources/icons/menu_icon.png`) is a pre-rendered capture
  of the watchface itself, not produced by these scripts; it's committed as-is.
- After regenerating, rebuild with `pebble build` from the repo root.
