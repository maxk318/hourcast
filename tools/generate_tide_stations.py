#!/usr/bin/env python3
"""Pull NOAA's full tide-prediction station list and write it out as the JSON
data file that both index.js (auto nearest-station detection) and
custom-clay.js (nearest-10 manual dropdown) require. Re-run this occasionally
to pick up newly published stations; NOAA doesn't retire IDs, so it's safe to
just overwrite."""
import json, os, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "src", "pkjs", "tide_stations.json"))
URL = "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions"

with urllib.request.urlopen(URL) as resp:
    data = json.load(resp)

stations = []
for s in data["stations"]:
    name = s["name"]
    if s.get("state"):
        name = name + ", " + s["state"]
    stations.append({"id": s["id"], "name": name, "lat": s["lat"], "lon": s["lng"]})

with open(OUT, "w") as f:
    json.dump(stations, f, separators=(",", ":"))

print("wrote", len(stations), "stations ->", os.path.relpath(OUT, os.path.join(HERE, "..")))
