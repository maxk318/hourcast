// HourCast companion: fetch Open-Meteo and send 12 hourly slots + current.
// Each icon is encoded as: night*100 + state*10 + count, where state =
//   0 clear, 1 partly, 2 overcast, 3 rain, 4 snow, 5 storm
// and count (0..4) = how many rays/flakes/bolts to draw for precip states,
// derived from precipitation probability. The classification cutoffs are
// user-editable on the settings page (see THRESHOLDS / loadThresholds / config.json).

var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay');
var clay = new Clay(clayConfig, customClay);
var messageKeys = require('message_keys');

// Location settings (persisted in pkjs localStorage so they survive relaunch).
var useManualLoc = false;
var manualLat = null;
var manualLon = null;

function loadLocationConfig() {
  try {
    var c = JSON.parse(localStorage.getItem('hcLoc')) || {};
    useManualLoc = !!c.useManual;
    manualLat = c.lat || null;
    manualLon = c.lon || null;
  } catch (e) { /* defaults */ }
}

// Classification cutoffs — defaults match the settings page defaults. All are
// user-editable; loadThresholds() pulls any saved values from clay-settings.
var THRESHOLDS = {
  pop1: 10, pop2: 20, pop3: 40, pop4: 60,   // POP %: lower bound for 1/2/3/4 marks
  overcast: 90, clear: 15,                   // cloud cover %: >=overcast full, <clear clear
  stormCape: 1500                            // >= this CAPE (J/kg) precip renders as storm
};

function loadThresholds() {
  try {
    var s = JSON.parse(localStorage.getItem('clay-settings')) || {};
    function num(k, d) { var v = parseFloat(s[k]); return isNaN(v) ? d : v; }
    THRESHOLDS.pop1 = num('POP1', 10);
    THRESHOLDS.pop2 = num('POP2', 20);
    THRESHOLDS.pop3 = num('POP3', 40);
    THRESHOLDS.pop4 = num('POP4', 60);
    THRESHOLDS.overcast = num('CLOUD_OVERCAST', 90);
    THRESHOLDS.clear = num('CLOUD_CLEAR', 15);
    THRESHOLDS.stormCape = num('STORM_CAPE', 1500);
  } catch (e) { /* keep defaults */ }
}

// Push saved display settings to the watch on launch. A fresh sideload resets
// the watch's persisted setting to its default, but the phone still holds the
// real choice in clay-settings — without this they can load out of sync.
function syncSettingsToWatch() {
  // Phone is the source of truth for display mode: always push it so the watch
  // can't diverge from what the settings page shows. Default is 0 (Clock Numbers).
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { /* */ }
  var mode = s.hasOwnProperty('DISPLAY_MODE') ? (parseInt(s.DISPLAY_MODE, 10) || 0) : 0;
  var showTide = s.hasOwnProperty('SHOW_TIDE') ? (!!s.SHOW_TIDE) : false;
  Pebble.sendAppMessage({ 'DISPLAY_MODE': mode, 'SHOW_TIDE': (showTide ? 1 : 0) },
    function () { console.log('HourCast: settings synced'); },
    function (err) { console.log('HourCast: settings sync failed ' + JSON.stringify(err)); });
}

// Merge a value into Clay's settings store. clay.generateUrl() (which opens the
// settings page) reads localStorage 'clay-settings', so writing here makes the
// value appear in the config the next time it's opened.
function setClaySetting(key, value) {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { /* */ }
  s[key] = value;
  localStorage.setItem('clay-settings', JSON.stringify(s));
}

// Update the "Current location" line shown in settings: coords immediately,
// then a reverse-geocoded name (BigDataCloud, free / no key) once it returns.
function updateCurrentLocation(lat, lon, manual) {
  var prefix = manual ? 'Manual: ' : '';
  var lat4 = Number(lat).toFixed(4), lon4 = Number(lon).toFixed(4);
  setClaySetting('CURRENT_LOC', prefix + lat4 + ', ' + lon4);
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var j = JSON.parse(this.responseText);
      var parts = [];
      // locality is the actual town (e.g. "Springfield"); city is the metro
      // principal city (e.g. "Washington") and is too coarse — prefer locality.
      var loc = j.locality || j.city;
      if (loc) parts.push(loc);
      if (j.principalSubdivision) parts.push(j.principalSubdivision);
      var name = parts.join(', ');
      if (j.countryCode && j.countryCode !== 'US' && j.countryName) {
        name = (name ? name + ', ' : '') + j.countryName;
      }
      if (name) {
        setClaySetting('CURRENT_LOC',
          prefix + name + ' (' + Number(lat).toFixed(2) + ', ' + Number(lon).toFixed(2) + ')');
      }
    } catch (e) { /* keep the bare coords */ }
  };
  xhr.open('GET', 'https://api.bigdatacloud.net/data/reverse-geocode-client' +
    '?latitude=' + lat + '&longitude=' + lon + '&localityLanguage=en');
  xhr.send();
}

// When the settings page is saved: store the location prefs, forward all
// settings to the watch, then refresh weather with the new location.
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response);

  useManualLoc = !!dict[messageKeys.USE_MANUAL_LOC];
  manualLat = dict[messageKeys.MANUAL_LAT];
  manualLon = dict[messageKeys.MANUAL_LON];
  localStorage.setItem('hcLoc', JSON.stringify(
    { useManual: useManualLoc, lat: manualLat, lon: manualLon }));

  // select values arrive as strings ("0"/"1"/"2"); the watch expects an int
  if (dict.hasOwnProperty(messageKeys.DISPLAY_MODE)) {
    dict[messageKeys.DISPLAY_MODE] = parseInt(dict[messageKeys.DISPLAY_MODE], 10) || 0;
  }

  // booleans need to be 0/1 for the watch
  if (dict.hasOwnProperty(messageKeys.SHOW_TIDE)) {
    dict[messageKeys.SHOW_TIDE] = dict[messageKeys.SHOW_TIDE] ? 1 : 0;
  }

  Pebble.sendAppMessage(dict,
    function () { console.log('HourCast: settings sent'); },
    function (err) { console.log('HourCast: settings send failed ' + JSON.stringify(err)); });

  loadThresholds();   // pick up any edited classification cutoffs before refetch
  fetchWeather();
});

// Fallback location (Arlington, VA) used if geolocation fails — also lets the
// emulator (no GPS) still pull real weather.
var LAT_FALLBACK = 38.8816;
var LON_FALLBACK = -77.0910;

// Moon phase index 0..11 (0 = new, 6 = full), matching the bundled bitmaps.
function moonPhaseIndex(date) {
  var ref = Date.UTC(2000, 0, 6, 18, 14, 0);   // a known new moon
  var syn = 29.530588853;                        // synodic month (days)
  var p = ((date.getTime() - ref) / 86400000) / syn;
  p = p - Math.floor(p);                         // 0..1 (0 = new, 0.5 = full)
  return Math.round(p * 10) % 10;
}

// "2026-05-31T05:50" -> "5:50" (12-hour, no am/pm; the sun/moon icon implies it)
function hhmm(iso) {
  var h = parseInt(iso.substr(11, 2), 10) % 12;
  if (h === 0) h = 12;
  return h + ':' + iso.substr(14, 2);
}

function sunriseForDate(j, hourIso) {
  var date = hourIso.substr(0, 10);
  for (var i = 0; i < j.daily.time.length; i++) {
    if (j.daily.time[i] === date) return hhmm(j.daily.sunrise[i]);
  }
  return '';
}

function sunsetBefore(j, hourIso) {   // the sunset that began this night
  var best = '';
  for (var i = 0; i < j.daily.sunset.length; i++) {
    if (j.daily.sunset[i] <= hourIso && j.daily.sunset[i] > best) best = j.daily.sunset[i];
  }
  return best ? hhmm(best) : '';
}

function num0(v) { return (typeof v === 'number' && !isNaN(v)) ? v : 0; }

// Probability-driven classification. Returns { state, count }:
//   count  = number of marks (0..4) from precipitation probability buckets
//   state  = 0 clear / 1 partly / 2 overcast (when count 0), else
//            3 rain / 4 snow / 5 storm (the precip type carrying the marks)
// POP sets HOW MANY marks; CAPE/code decide WHICH kind. Snow vs rain comes
// straight from the forecast (snowfall / snow weather code), not a temp guess.
function classify(code, pop, cloud, cape, snow) {
  var T = THRESHOLDS;
  pop = num0(pop); cloud = num0(cloud); cape = num0(cape); snow = num0(snow);
  var count = pop >= T.pop4 ? 4 : pop >= T.pop3 ? 3 : pop >= T.pop2 ? 2 : pop >= T.pop1 ? 1 : 0;
  if (count === 0) {
    var st = cloud >= T.overcast ? 2 : (cloud < T.clear ? 0 : 1);
    return { state: st, count: 0 };
  }
  var snowCode = (code >= 71 && code <= 77) || code === 85 || code === 86;
  var state;
  if (snow > 0 || snowCode) state = 4;                  // snow (wins over storm)
  else if (code >= 95 || cape >= T.stormCape) state = 5; // storm
  else state = 3;                                        // rain
  return { state: state, count: count };
}

// Pack a classified cell + day/night into the icon int the watch decodes.
function packIcon(isDay, cell) {
  return (isDay ? 0 : 100) + cell.state * 10 + cell.count;
}

function sendTide(lat, lon) {
  // Fetch water level data from NOAA for the nearest tide station
  // Use a simple station lookup: quantize location to find closest station from a small set
  var now = new Date();
  var beginDate = new Date(now.getTime() - 6 * 3600000);
  var endDate = new Date(now.getTime() + 18 * 3600000);

  function pad(n) { return (n < 10 ? '0' : '') + n; }
  var begin = beginDate.getFullYear() + pad(beginDate.getMonth() + 1) + pad(beginDate.getDate()) +
             pad(beginDate.getHours()) + pad(beginDate.getMinutes());
  var end = endDate.getFullYear() + pad(endDate.getMonth() + 1) + pad(endDate.getDate()) +
           pad(endDate.getHours()) + pad(endDate.getMinutes());

  // For now, use major US coastal stations. TODO: implement proper nearest-station lookup
  var stations = [
    {id: '8638610', name: 'Sewells Point, VA', lat: 36.9428, lon: -76.3286},
    {id: '8658163', name: 'Charleston, SC', lat: 32.7769, lon: -79.5268},
    {id: '8722670', name: 'Miami, FL', lat: 25.7667, lon: -80.1628},
    {id: '8727520', name: 'Key West, FL', lat: 24.5627, lon: -81.8093},
    {id: '8454000', name: 'Galveston, TX', lat: 29.3186, lon: -94.7878},
    {id: '8467150', name: 'Biloxi, MS', lat: 30.3869, lon: -88.8829},
    {id: '8571892', name: 'New Orleans, LA', lat: 29.9186, lon: -90.2667},
    {id: '8638386', name: 'Norfolk, VA', lat: 36.8457, lon: -76.2982}
  ];

  // Find nearest station
  var best = null, bestDist = Infinity;
  for (var s = 0; s < stations.length; s++) {
    var dx = stations[s].lat - lat, dy = stations[s].lon - lon;
    var dist = dx * dx + dy * dy;
    if (dist < bestDist) { bestDist = dist; best = stations[s]; }
  }

  if (!best) {
    console.log('HourCast: no tide stations available');
    return;
  }

  console.log('HourCast: using tide station ' + best.name + ' (' + best.id + ')');

  // Request hourly interval in GMT; begin 1 hour back so we always have slot 0
  var tideUrl = 'https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?station=' + best.id +
               '&begin_date=' + begin + '&end_date=' + end +
               '&product=water_level&datum=msl&format=json&units=metric&time_zone=gmt&interval=hourly';

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var tides = JSON.parse(this.responseText);
      if (!tides || !tides.data) {
        console.log('HourCast: no tide data - ' + JSON.stringify(tides && tides.error));
        return;
      }

      // Build a map of utcHour -> level from NOAA response.
      // NOAA timestamps are "YYYY-MM-DD HH:MM" in UTC — parse explicitly with T+Z.
      var levelByUtcHour = {};
      for (var t = 0; t < tides.data.length; t++) {
        var ts = new Date(tides.data[t].t.replace(' ', 'T') + 'Z');
        levelByUtcHour[ts.getTime()] = parseFloat(tides.data[t].v);
      }

      // Current UTC hour (truncated to top of hour)
      var utcHour0 = new Date(now);
      utcHour0.setUTCMinutes(0);
      utcHour0.setUTCSeconds(0);
      utcHour0.setUTCMilliseconds(0);

      var rawTides = {};
      var minLevel = Infinity, maxLevel = -Infinity;

      for (var h = 0; h < 12; h++) {
        var ms = utcHour0.getTime() + h * 3600000;
        if (levelByUtcHour.hasOwnProperty(ms)) {
          var v = levelByUtcHour[ms];
          minLevel = Math.min(minLevel, v);
          maxLevel = Math.max(maxLevel, v);
          rawTides[h] = v;
        }
      }

      // Normalize to 0-100
      if (Object.keys(rawTides).length > 0 && maxLevel > minLevel) {
        var tideDict = {};
        for (var h = 0; h < 12; h++) {
          if (rawTides.hasOwnProperty(h)) {
            tideDict['TIDE_' + h] = Math.round((rawTides[h] - minLevel) / (maxLevel - minLevel) * 100);
          }
        }
        tideDict['TIDE_VALID'] = 1;
        console.log('HourCast: sending tide data (' + Object.keys(rawTides).length + ' hours)');
        Pebble.sendAppMessage(tideDict,
          function () { console.log('HourCast: tide sent'); },
          function (e) { console.log('HourCast: tide send failed ' + JSON.stringify(e)); });
      } else {
        console.log('HourCast: no matching tide hours (min=' + minLevel + ' max=' + maxLevel + ' slots=' + Object.keys(rawTides).length + ')');
      }
    } catch (e) { console.log('HourCast: tide parse error ' + e); }
  };
  xhr.onerror = function () { console.log('HourCast: tide fetch error'); };
  xhr.open('GET', tideUrl);
  xhr.send();
}

function sendWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat + '&longitude=' + lon +
    '&hourly=weather_code,is_day,temperature_2m,precipitation_probability,cloud_cover,cape,snowfall' +
    '&current=temperature_2m,weather_code,is_day' +
    '&daily=sunrise,sunset' +
    '&temperature_unit=fahrenheit' +
    '&past_days=1&forecast_days=2&timezone=auto';

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    var j;
    try { j = JSON.parse(this.responseText); }
    catch (e) { console.log('HourCast: parse error ' + e); return; }
    if (!j.hourly || !j.current) { console.log('HourCast: no weather data'); return; }

    // All Open-Meteo timestamps are in the LOCATION's local time (timezone=auto)
    // as "YYYY-MM-DDTHH:MM". Compare/derive hours straight from the strings so
    // there's no dependence on the JS runtime's own timezone.
    var curStr = j.current.time;                 // e.g. "2026-05-30T11:30"
    var i0 = 0;
    for (var i = 0; i < j.hourly.time.length; i++) {
      if (j.hourly.time[i] > curStr) { i0 = i; break; }   // lexical = chronological
    }

    var dict = {};
    var startK = -1, sunriseK = -1, sunsetK = -1;
    // day/night of the hour just before the window (i.e. "now") — so the first
    // slot isn't mistaken for a transition.
    var prevDay = (i0 > 0) ? j.hourly.is_day[i0 - 1] : j.hourly.is_day[i0];
    for (var d = 0; d < 12; d++) {
      var idx = i0 + d;
      if (idx >= j.hourly.weather_code.length) break;
      var t = j.hourly.time[idx];
      var k = parseInt(t.substr(11, 2), 10) % 12;   // clock position
      var isDay = j.hourly.is_day[idx];
      if (d === 0) startK = k;
      // only on an actual transition: night->day = sunrise, day->night = sunset
      if (isDay !== prevDay) {
        if (isDay && sunriseK < 0) { sunriseK = k; dict['SUNRISE_STR'] = sunriseForDate(j, t); }
        if (!isDay && sunsetK < 0) { sunsetK = k; dict['SUNSET_STR'] = sunsetBefore(j, t); }
      }
      prevDay = isDay;
      var H = j.hourly;
      var cell = classify(H.weather_code[idx], H.precipitation_probability[idx],
                          H.cloud_cover[idx], H.cape[idx], H.snowfall[idx]);
      dict['ICON_' + k] = packIcon(isDay, cell);
      dict['TEMP_' + k] = Math.round(j.hourly.temperature_2m[idx]);
    }
    dict['START_K'] = startK;
    if (sunriseK >= 0) dict['SUNRISE_K'] = sunriseK;
    if (sunsetK >= 0) dict['SUNSET_K'] = sunsetK;

    dict['MOON_PHASE'] = moonPhaseIndex(new Date());

    // Current block lacks POP/CAPE/snow, so classify "now" from the current
    // hour's hourly values, with the live current temp/weather_code/day flag.
    var curIdx = (i0 > 0) ? i0 - 1 : 0;
    var curCell = classify(j.current.weather_code, j.hourly.precipitation_probability[curIdx],
                           j.hourly.cloud_cover[curIdx], j.hourly.cape[curIdx],
                           j.hourly.snowfall[curIdx]);
    dict['CUR_ICON'] = packIcon(j.current.is_day, curCell);
    dict['CUR_TEMP'] = Math.round(j.current.temperature_2m);

    Pebble.sendAppMessage(dict,
      function () { console.log('HourCast: weather sent'); },
      function (e) { console.log('HourCast: send failed ' + JSON.stringify(e)); });

    // Fetch tide data asynchronously (will send in a follow-up message if available)
    sendTide(lat, lon);
  };
  xhr.onerror = function () { console.log('HourCast: xhr error'); };
  xhr.open('GET', url);
  xhr.send();
}

// Collapse near-simultaneous fetches into one. On launch at :05/:35 the watch's
// tick request and the 'ready' handler both call fetchWeather; without this the
// second sendAppMessage fails ("already in progress").
var lastFetchAt = 0;

function fetchWeather() {
  var now = Date.now();
  if (now - lastFetchAt < 5000) { console.log('HourCast: duplicate fetch skipped'); return; }
  lastFetchAt = now;

  // Manual coordinates (from the settings page) override GPS when enabled.
  if (useManualLoc) {
    var lat = parseFloat(manualLat), lon = parseFloat(manualLon);
    if (!isNaN(lat) && !isNaN(lon)) {
      updateCurrentLocation(lat, lon, true);
      sendWeather(lat.toFixed(4), lon.toFixed(4));
      return;
    }
    console.log('HourCast: manual location enabled but invalid; falling back to GPS');
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      updateCurrentLocation(pos.coords.latitude, pos.coords.longitude, false);
      sendWeather(pos.coords.latitude.toFixed(4), pos.coords.longitude.toFixed(4));
    },
    function (err) {
      console.log('HourCast: geolocation failed (' + err.message + '); using fallback');
      updateCurrentLocation(LAT_FALLBACK, LON_FALLBACK, false);
      sendWeather(LAT_FALLBACK, LON_FALLBACK);
    },
    { timeout: 15000, maximumAge: 60000 });
}

// The watch asks for a refresh at :05 and :35 (its tick handler). Inbound
// messages wake this JS even when it's been suspended in the background.
Pebble.addEventListener('appmessage', function (e) {
  if (e && e.payload && e.payload.REQUEST_WEATHER) {
    console.log('HourCast: weather requested by watch');
    fetchWeather();
  }
});

Pebble.addEventListener('ready', function () {
  loadLocationConfig();
  loadThresholds();        // classification cutoffs from the settings page
  syncSettingsToWatch();   // keep the watch's display options in step with the phone
  fetchWeather();
  setInterval(fetchWeather, 30 * 60 * 1000);  // belt-and-suspenders if JS stays alive
});
