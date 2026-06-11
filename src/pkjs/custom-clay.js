// Clay custom function (runs inside the configuration webview).
// Handles show/hide of manual-location and manual-tide-station inputs,
// and registers the dynamic tide-station-select component.
module.exports = function (minified) {
  var clayConfig = this;

  // ---- Dynamic tide station select component -------------------------
  // Reads the 5 nearest stations (stored by the phone JS on last weather
  // fetch as hcNearbyStations in localStorage) and renders a <select>.
  // The selected station ID is stored under messageKey MANUAL_TIDE_STATION_ID.
  clayConfig.registerComponent({
    name: 'tide-station-select',
    template: [
      '<div class="component-body">',
        '<label class="clay-label"></label>',
        '<select class="component-item tide-sel"></select>',
      '</div>'
    ].join(''),
    manipulator: {
      get: function (item) { return item.$manipulatorTarget[0].value; },
      set: function (item, val) { item.$manipulatorTarget[0].value = val; }
    },
    initialize: function (min) {
      var self = this;
      var root = this.$element[0];

      // Set label text from config
      var label = root.querySelector('.clay-label');
      if (label) label.textContent = this.config.label || '';

      var sel = root.querySelector('.tide-sel');
      this.$manipulatorTarget = min(sel);

      // Populate from the list the phone JS stored on last fetch
      var stations = [];
      try { stations = JSON.parse(localStorage.getItem('hcNearbyStations')) || []; } catch (e) {}

      sel.innerHTML = '';
      if (!stations.length) {
        sel.innerHTML = '<option value="">Locating nearby stations…</option>';
      } else {
        stations.forEach(function (s, i) {
          var opt = document.createElement('option');
          opt.value = s.id;
          opt.textContent = (i === 0 ? s.name + ' (nearest)' : s.name);
          sel.appendChild(opt);
        });
      }

      // Restore previously saved selection
      if (this.setting) sel.value = this.setting;

      sel.addEventListener('change', function () { self.triggerChange(); });
    }
  });

  // ---- Show/hide helpers ---------------------------------------------
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
