// Clay custom function (runs inside the configuration webview).
// Handles show/hide of manual-location and manual-tide-station inputs.
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
  });
};
