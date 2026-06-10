// Clay custom function (runs inside the configuration webview). It only
// shows/hides the manual lat-lon inputs when the toggle changes. The
// "Current location" line is NOT located here: geolocation is blocked in the
// (non-secure-origin) config webview, so pkjs does the locating and injects the
// CURRENT_LOC value via clay-settings (see index.js).
module.exports = function (minified) {
  var clayConfig = this;

  function toggleManual() {
    var on = this.get();   // toggle: truthy when manual entry is enabled
    var lat = clayConfig.getItemByMessageKey('MANUAL_LAT');
    var lon = clayConfig.getItemByMessageKey('MANUAL_LON');
    if (on) { lat.show(); lon.show(); }
    else    { lat.hide(); lon.hide(); }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var manual = clayConfig.getItemByMessageKey('USE_MANUAL_LOC');
    toggleManual.call(manual);          // set initial visibility
    manual.on('change', toggleManual);  // and react to changes
  });
};
