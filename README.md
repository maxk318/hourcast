# HourCast

HourCast turns your Pebble Time 2 into an at a glance weather clock.

- Rolling 12 hour forecast around the dial: each hour shows its conditions (sun, moon, clouds, rain, snow, or storms) with its temperature.
- The upcoming hour in the forecast is highlighted green to confirm where the forecast starts. This will also give a visual indication if the weather data is lagging.
- Real moon phases on the nighttime hours, drawn from the actual lunar cycle.
- Current conditions and temperature anchor the center of the face.
- Sunrise and sunset times appear on the hours they occur.
- Date and battery tuck into the lower dial.
- Weather from Open-Meteo using your phone's location (no account, no API key), or from coordinates you set yourself.
- Connection at a glance: the hands glow blue while your phone is connected and turn red, with a buzz, if it drops, so you always know your data is live.
- Three display styles: classic hour numerals, bold tick marks with oversized icons, or temperatures on the ring for an uncluttered face that shows off the weather art and moon phases.

See [CHANGELOG.md](CHANGELOG.md) for release notes.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. The
modern Pebble hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round
2), and **flint** (Pebble 2 Duo); the original Pebble platforms (aplite,
basalt, chalk, diorite) are included by default for backwards compatibility.

## Project layout

```
src/c/           C source for the watchapp
src/pkjs/        PebbleKit JS (phone-side) source, if any
worker_src/c/    Background worker source, if any
resources/       Images, fonts, and other bundled resources
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

By default this project is configured as a watchapp. To make it a watchface,
set `pebble.watchapp.watchface` to `true` in `package.json`.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
