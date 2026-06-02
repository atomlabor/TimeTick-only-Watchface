// src/pkjs/index.js  –  TimeTick PebbleKit JS
// Holt aktuelles Wetter per GPS-Standort und sendet die Temperatur ans Watchface.
// Kostenloser API-Key von openweathermap.org nötig → hier eintragen:
var OWM_API_KEY = 'DEIN_OWM_API_KEY_HIER';

function fetchWeather(lat, lon) {
  var url = 'https://api.openweathermap.org/data/2.5/weather'
          + '?lat=' + lat + '&lon=' + lon
          + '&units=metric'        // °C
          + '&appid=' + OWM_API_KEY;

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status === 200) {
      var data = JSON.parse(xhr.responseText);
      var temp = Math.round(data.main.temp);
      Pebble.sendAppMessage({ MSG_KEY_TEMP: temp }, function() {
        console.log('Wetter gesendet: ' + temp + '°C');
      }, function(e) {
        console.log('sendAppMessage Fehler: ' + JSON.stringify(e));
      });
    }
  };
  xhr.open('GET', url);
  xhr.send();
}

function getLocationAndFetch() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function(err) {
      console.log('GPS-Fehler: ' + err.message);
    },
    { timeout: 15000 }
  );
}

// Watchface hat Wetter angefordert
Pebble.addEventListener('appmessage', function(e) {
  if (e.payload.MSG_KEY_REQUEST) {
    getLocationAndFetch();
  }
});

// Beim Start einmal holen
Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS bereit');
  getLocationAndFetch();
});
