// Clay custom function (runs inside the configuration webview).
// Handles show/hide of manual-location and manual-tide-station inputs,
// and sorts the station dropdown by distance using hcNearbyStations from localStorage.
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
    var manualLoc  = clayConfig.getItemByMessageKey('USE_MANUAL_LOC');
    toggleManualLoc.call(manualLoc);
    manualLoc.on('change', toggleManualLoc);

    var manualTide = clayConfig.getItemByMessageKey('USE_MANUAL_TIDE');
    toggleManualTide.call(manualTide);
    manualTide.on('change', toggleManualTide);

    // Replace station dropdown options with the nearest-to-farthest list from
    // the last weather fetch. Falls back to the full static list in config.json
    // if localStorage hasn't been populated yet.
    try {
      var nearby = JSON.parse(localStorage.getItem('hcNearbyStations')) || [];
      if (nearby.length) {
        var stationItem = clayConfig.getItemByMessageKey('MANUAL_TIDE_STATION_ID');
        var selectEl = stationItem.$element[0].querySelector('select.component-item');
        if (selectEl) {
          var savedVal = stationItem.setting;
          selectEl.innerHTML = '';
          nearby.forEach(function (s, i) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = i === 0 ? s.name + ' (nearest)' : s.name;
            selectEl.appendChild(opt);
          });
          // Restore saved selection if it's in the new list, else use nearest
          selectEl.value = savedVal || nearby[0].id;
        }
      }
    } catch (e) { /* leave static options intact */ }
  });
};
