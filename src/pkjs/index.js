// HourCast companion: fetch Open-Meteo and send 12 hourly slots + current.
// Each icon is encoded as: night*10 + state, where state =
//   0 clear, 1 partly, 2 overcast, 3 rain, 4 snow, 5 storm

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

// Push saved display settings to the watch on launch. A fresh sideload resets
// the watch's persisted setting to its default, but the phone still holds the
// real choice in clay-settings — without this they can load out of sync.
function syncSettingsToWatch() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { /* */ }
  if (!s.hasOwnProperty('DISPLAY_MODE')) return;   // never configured -> use watch default
  Pebble.sendAppMessage({ 'DISPLAY_MODE': parseInt(s.DISPLAY_MODE, 10) || 0 },
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

  Pebble.sendAppMessage(dict,
    function () { console.log('HourCast: settings sent'); },
    function (err) { console.log('HourCast: settings send failed ' + JSON.stringify(err)); });

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

function wxState(code) {
  if (code === 0) return 0;                              // clear
  if (code === 1 || code === 2) return 1;                // partly
  if (code === 3 || code === 45 || code === 48) return 2; // overcast / fog
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 3; // drizzle/rain
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 4; // snow
  if (code >= 95) return 5;                              // thunderstorm
  return 2;                                              // default: overcast
}

function sendWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat + '&longitude=' + lon +
    '&hourly=weather_code,is_day,temperature_2m' +
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
      dict['ICON_' + k] = (isDay ? 0 : 10) + wxState(j.hourly.weather_code[idx]);
      dict['TEMP_' + k] = Math.round(j.hourly.temperature_2m[idx]);
    }
    dict['START_K'] = startK;
    if (sunriseK >= 0) dict['SUNRISE_K'] = sunriseK;
    if (sunsetK >= 0) dict['SUNSET_K'] = sunsetK;

    dict['MOON_PHASE'] = moonPhaseIndex(new Date());

    dict['CUR_ICON'] = (j.current.is_day ? 0 : 10) + wxState(j.current.weather_code);
    dict['CUR_TEMP'] = Math.round(j.current.temperature_2m);

    Pebble.sendAppMessage(dict,
      function () { console.log('HourCast: weather sent'); },
      function (e) { console.log('HourCast: send failed ' + JSON.stringify(e)); });
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
  syncSettingsToWatch();   // keep the watch's display options in step with the phone
  fetchWeather();
  setInterval(fetchWeather, 30 * 60 * 1000);  // belt-and-suspenders if JS stays alive
});
