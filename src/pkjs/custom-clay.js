// Mirrors index.js's LAT_FALLBACK/LON_FALLBACK (Arlington, VA) — used only if
// the config page opens before the very first location fetch has landed.
var LAT_FALLBACK = 38.8816;
var LON_FALLBACK = -77.0910;

var TIDE_STATIONS = require('./tide_stations.json');

module.exports = function (minified) {
  var clayConfig = this;

  function toggleManualLoc() {
    var on = this.get();
    var lat = clayConfig.getItemByMessageKey('MANUAL_LAT');
    var lon = clayConfig.getItemByMessageKey('MANUAL_LON');
    if (on) { lat.show(); lon.show(); }
    else    { lat.hide(); lon.hide(); }
  }

  function toggleManualTide() {
    var on = this.get();
    var sel = clayConfig.getItemByMessageKey('MANUAL_TIDE_STATION_ID');
    if (on) { sel.show(); }
    else    { sel.hide(); }
  }

  // Populate the manual station dropdown with the 10 stations nearest the
  // same location weather already resolved (CURRENT_LAT/LON, set by
  // updateCurrentLocation() in index.js) — no separate geolocation prompt.
  function populateNearestTideStations() {
    var latItem = clayConfig.getItemByMessageKey('CURRENT_LAT');
    var lonItem = clayConfig.getItemByMessageKey('CURRENT_LON');
    var lat = parseFloat(latItem.get());
    var lon = parseFloat(lonItem.get());
    if (isNaN(lat) || isNaN(lon)) { lat = LAT_FALLBACK; lon = LON_FALLBACK; }

    var nearest = TIDE_STATIONS.slice().sort(function (a, b) {
      var da = (a.lat - lat) * (a.lat - lat) + (a.lon - lon) * (a.lon - lon);
      var db = (b.lat - lat) * (b.lat - lat) + (b.lon - lon) * (b.lon - lon);
      return da - db;
    }).slice(0, 10);

    var sel = clayConfig.getItemByMessageKey('MANUAL_TIDE_STATION_ID');
    var priorValue = sel.get();
    var html = nearest.map(function (s) {
      return '<option value="' + s.id + '" class="item-select-option">' + s.name + '</option>';
    }).join('');
    sel.$manipulatorTarget.set('innerHTML', html);

    // Keep the previously-saved station selected if it's still in the new
    // nearest-10 list; otherwise default to the closest one.
    var stillPresent = nearest.some(function (s) { return s.id === priorValue; });
    sel.set(stillPresent ? priorValue : nearest[0].id);
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var manualLoc = clayConfig.getItemByMessageKey('USE_MANUAL_LOC');
    toggleManualLoc.call(manualLoc);
    manualLoc.on('change', toggleManualLoc);

    var manualTide = clayConfig.getItemByMessageKey('USE_MANUAL_TIDE');
    toggleManualTide.call(manualTide);
    manualTide.on('change', toggleManualTide);

    populateNearestTideStations();

    // Phone-only storage, never shown to the user.
    clayConfig.getItemByMessageKey('CURRENT_LAT').hide();
    clayConfig.getItemByMessageKey('CURRENT_LON').hide();
  });
};
