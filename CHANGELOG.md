# Changelog

## 3.1.0

Probability-driven precipitation.

**New / Changed**
- Precipitation chance now drives the icon: each precip hour shows 1–4 marks — rays (rain), snowflakes (snow), or lightning bolts (storm) — scaled to the chance of precipitation, instead of a single categorical icon.
- Weather now factors in precipitation probability, cloud cover, and CAPE (instability) — so likely rain/storms show up that the old code-only classification missed. Rain vs. snow comes straight from the forecast (snowfall / snow conditions); a high-CAPE precip hour reads as a storm.
- All marks are drawn on-watch with a black outline (like the hands); snowflakes are branched, not bare asterisks.
- New "Weather sensitivity" settings section: the 1/2/3/4-mark probability thresholds, the overcast/clear cloud-cover %, and the storm CAPE level are all editable, with a legend explaining what the marks mean.
- The inner ring now defaults to Clock Numbers and stays in sync with the settings page.

**Improved**
- Overcast clouds are white; rain/snow/storm use a darker storm cloud, sized so the top of the sun or moon (its phase) peeks over it.

## 2.2.0

A legibility and polish pass.

**New / Changed**
- The display setting is now called "Inner Ring" with clearer options: Clock Numbers, Hashes, Temperature.
- Hashes is now the default inner ring style.

**Improved**
- New symmetric sun artwork with even rays, so the center temp sits truly centered.
- All digits (temps, hour numbers, center temp, battery, date) are now white with a black outline and no filled backgrounds, so they read over the icons, the hands, and the black face while letting the hands show through.
- Sunrise and sunset times now match the temperature font weight.
- Hash marks are outlined too, and the hash style now shows the spotted moon art.
- Moons are 5% smaller for a cleaner ring.
- Hands are outlined and stay distinct where they overlap each other.
- The minute hand is longer, reaching closer to the edge.

## 2.1.0

**New**
- Three display styles (Settings, Display style):
  - Time numerals: hour numbers on the inner ring, temperature on each weather icon.
  - Hash marks plus big icons: a tick ring with larger 46px weather icons.
  - Temps on inner ring: hourly temperatures on the inner ring (12/3/6/9 bold), leaving the weather icons uncovered so the moon phases show in full.
- Location settings: the config page shows your current location (city plus coordinates), and you can switch to manual coordinates if you would rather pin a fixed spot than use GPS.
- On the hour refresh: the watch now pulls fresh weather at :05 and :35 every hour, so the rolling forecast and the green upcoming hour stay current.
- Disconnect alert: both hands turn red and the watch buzzes the moment it loses the phone connection.

**Improved**
- New sun artwork with clean, evenly spaced rays.
- Moon phases redrawn as smooth two tone discs (with crater detail in the temps view where the moon is not covered), sized 5% smaller for a cleaner ring.
- Overcast clouds are a richer, darker gray with their texture preserved.
- Hands are wider and a lighter, more legible blue; the hour hand is shorter for clearer separation from the minute hand.
- Temperatures and the center temp sit on tidy pill backgrounds; battery and date moved to the 5 and 7 o'clock positions so they stay readable under the hands.

**Fixed**
- Crash when switching display styles.
- Weather occasionally failing to load on launch (overlapping requests now coalesce).
- Location name showing the metro area ("Washington") instead of your town ("Springfield").

## 1.0.0

- Initial release: rolling 12 hour weather ring, current conditions, moon phases, sunrise and sunset, and Bluetooth connection coloring on the hands.
