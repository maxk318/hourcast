// Clay custom function (runs inside the configuration webview).
// Handles show/hide of manual-location and manual-tide-station inputs,
// and sorts the station dropdown nearest-to-farthest.
//
// Note: index.js (PebbleKit JS context) and this webview have separate
// localStorage. Shared data must go through clay-settings, which Clay
// serializes into the config URL so it's readable here.
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

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var manualLoc = clayConfig.getItemByMessageKey('USE_MANUAL_LOC');
    toggleManualLoc.call(manualLoc);
    manualLoc.on('change', toggleManualLoc);

    var manualTide = clayConfig.getItemByMessageKey('USE_MANUAL_TIDE');
    toggleManualTide.call(manualTide);
    manualTide.on('change', toggleManualTide);

    // Replace static station options with the 5 nearest sorted by distance.
    // NEARBY_STATIONS is written by index.js via setClaySetting(), which puts
    // it in clay-settings — the one data store Clay passes into the webview.
    try {
      var cs = JSON.parse(localStorage.getItem('clay-settings')) || {};
      var nearby = JSON.parse(cs.NEARBY_STATIONS || '[]');
      if (nearby.length) {
        var stationItem = clayConfig.getItemByMessageKey('MANUAL_TIDE_STATION_ID');
        var selectEl = stationItem.$element[0] ? stationItem.$element[0].querySelector('select') : null;
        if (selectEl) {
          var savedVal = stationItem.setting;
          selectEl.innerHTML = '';
          nearby.forEach(function (s, i) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = i === 0 ? s.name + ' (nearest)' : s.name;
            selectEl.appendChild(opt);
          });
          selectEl.value = savedVal || nearby[0].id;
        }
      }
    } catch (e) { /* leave static fallback options intact */ }
  });
};
