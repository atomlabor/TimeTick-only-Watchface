// src/pkjs/index.js

var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

var SETTINGS_KEY = 'timetick_settings_manual';

function sendTemperature(temp) {
  var message = {
    "MSG_KEY_TEMP": temp
  };

  console.log('Sending to watch: ' + JSON.stringify(message));

  Pebble.sendAppMessage(
    message,
    function() {
      console.log('Sent to watch: ' + temp + '°C');
    },
    function(e) {
      console.log('sendAppMessage error: ' + JSON.stringify(e));
    }
  );
}

function saveSettings(settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings || {}));
    console.log('Settings saved manually: ' + JSON.stringify(settings || {}));
  } catch (err) {
    console.log('Settings save error: ' + err.message);
  }
}

function loadSettings() {
  var settings = {};

  try {
    var raw = localStorage.getItem(SETTINGS_KEY);

    if (raw) {
      settings = JSON.parse(raw);
    }
  } catch (err) {
    console.log('Settings load error: ' + err.message);
  }

  console.log('Loaded manual settings: ' + JSON.stringify(settings || {}));

  return settings || {};
}

function parseClayResponse(response) {
  console.log('Raw Clay response: ' + response);

  if (!response) {
    return {};
  }

  var decoded = response;

  try {
    decoded = decodeURIComponent(decoded);
  } catch (err1) {
    console.log('First decode error: ' + err1.message);
  }

  try {
    decoded = decodeURIComponent(decoded);
  } catch (err2) {
    // Manche Antworten sind nur einmal encodet. Das ist okay.
  }

  console.log('Decoded Clay response: ' + decoded);

  var jsonStart = decoded.indexOf('{');
  var jsonEnd = decoded.lastIndexOf('}');

  if (jsonStart >= 0 && jsonEnd > jsonStart) {
    var jsonText = decoded.substring(jsonStart, jsonEnd + 1);

    try {
      var parsed = JSON.parse(jsonText);
      console.log('Parsed Clay JSON: ' + JSON.stringify(parsed));
      return parsed || {};
    } catch (err3) {
      console.log('Clay JSON parse error: ' + err3.message);
    }
  }

  var querySettings = {};

  try {
    var clean = decoded;

    if (clean.indexOf('?') >= 0) {
      clean = clean.split('?')[1];
    }

    if (clean.indexOf('#') >= 0) {
      clean = clean.split('#')[1];
    }

    var parts = clean.split('&');

    for (var i = 0; i < parts.length; i++) {
      var pair = parts[i].split('=');

      if (pair.length === 2) {
        var key = decodeURIComponent(pair[0]);
        var value = decodeURIComponent(pair[1]);

        querySettings[key] = value;
      }
    }

    console.log('Parsed Clay query settings: ' + JSON.stringify(querySettings));
  } catch (err4) {
    console.log('Clay query parse error: ' + err4.message);
  }

  return querySettings;
}

function getApiKey() {
  var settings = loadSettings();

  var apiKey =
    settings.owm_api_key ||
    settings["owm_api_key"] ||
    settings[4] ||
    settings["4"] ||
    '';

  apiKey = String(apiKey).trim();

  console.log('API key settings object: ' + JSON.stringify(settings));

  if (!apiKey) {
    console.log('No API key found');
  } else {
    console.log('API key found, length: ' + apiKey.length);
  }

  return apiKey;
}

function fetchWeather(lat, lon) {
  var apiKey = getApiKey();

if (!apiKey) {
  console.log('No API key has been set in Clay. Weather will stay hidden.');
  return;
}

  var url = 'https://api.openweathermap.org/data/2.5/weather'
          + '?lat=' + encodeURIComponent(lat)
          + '&lon=' + encodeURIComponent(lon)
          + '&units=metric'
          + '&appid=' + encodeURIComponent(apiKey);

  console.log('Fetching weather from OpenWeatherMap');

  var xhr = new XMLHttpRequest();

  xhr.onload = function() {
    console.log('OWM status: ' + xhr.status);
    console.log('OWM response: ' + xhr.responseText);

    if (xhr.status !== 200) {
      // 91°C bedeutet: OpenWeatherMap antwortet mit Fehler
      sendTemperature(91);
      return;
    }

    try {
      var data = JSON.parse(xhr.responseText);

      if (!data.main || typeof data.main.temp === 'undefined') {
        // 92°C bedeutet: Antwort da, aber keine Temperatur gefunden
        sendTemperature(92);
        return;
      }

      var temp = Math.round(data.main.temp);

      console.log('Temperature received from OWM: ' + temp + '°C');

      sendTemperature(temp);
    } catch (err) {
      console.log('JSON parse error: ' + err.message);

      // 93°C bedeutet: Antwort konnte nicht gelesen werden
      sendTemperature(93);
    }
  };

  xhr.onerror = function() {
    console.log('Network error while fetching weather');

    // 94°C bedeutet: Netzwerkfehler beim Wetterabruf
    sendTemperature(94);
  };

  xhr.open('GET', url);
  xhr.send();
}

function getLocationAndFetch() {
  console.log('Requesting location');

  navigator.geolocation.getCurrentPosition(
    function(pos) {
      console.log('Location received: ' + pos.coords.latitude + ', ' + pos.coords.longitude);

      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function(err) {
      console.log('GPS error: ' + err.code + ' ' + err.message);

      // 95°C bedeutet: Standort konnte nicht gelesen werden
      sendTemperature(95);
    },
    {
      timeout: 15000,
      maximumAge: 60000,
      enableHighAccuracy: false
    }
  );
}

Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready');

  loadSettings();
});

Pebble.addEventListener('showConfiguration', function(e) {
  console.log('Opening Clay configuration');

  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  console.log('Clay webview closed');

  if (!e || !e.response) {
    console.log('No Clay response received');
    return;
  }

  var settings = parseClayResponse(e.response);

  saveSettings(settings);

  console.log('Clay settings final: ' + JSON.stringify(settings));
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received: ' + JSON.stringify(e.payload));

  getLocationAndFetch();
});