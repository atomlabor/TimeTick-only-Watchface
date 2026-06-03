var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

var SETTINGS_KEY = 'timetick_settings_manual';

var MSG_KEY_TEMP = 2;
var MSG_KEY_REQUEST = 3;
var MSG_KEY_ENABLE_WEATHER = 5;
var MSG_KEY_ENABLE_CLEANSTYLE = 7;

function log(msg) {
  console.log('[TimeTick] ' + msg);
}

function sendTemperature(temp) {
  var dict = {};
  dict[MSG_KEY_TEMP] = temp;

  Pebble.sendAppMessage(
    dict,
    function() {
      log('Weather sent: ' + temp);
    },
    function(e) {
      log('Error sending weather: ' + JSON.stringify(e));
    }
  );
}

function saveSettings(settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings || {}));
  } catch (err) {
    log('Save settings error: ' + err);
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
    log('Load settings error: ' + err);
  }

  return settings || {};
}

function normalizeBool(value, fallback) {
  if (value === undefined || value === null) return fallback;

  if (value === true || value === 1) return true;
  if (value === false || value === 0) return false;

  var str = String(value).toLowerCase();

  return str === 'true' || str === '1' || str === 'yes' || str === 'on';
}

function isWeatherEnabled() {
  var settings = loadSettings();

  var enabled =
    settings.enable_weather ||
    settings['enable_weather'] ||
    settings[MSG_KEY_ENABLE_WEATHER] ||
    settings[String(MSG_KEY_ENABLE_WEATHER)];

  return normalizeBool(enabled, true);
}

function getApiKey() {
  var settings = loadSettings();

  var apiKey =
    settings.owm_api_key ||
    settings['owm_api_key'] ||
    settings.openweathermap_api_key ||
    settings['openweathermap_api_key'] ||
    settings.api_key ||
    settings['api_key'] ||
    settings[4] ||
    settings['4'] ||
    '';

  return String(apiKey).trim();
}

function hasWeatherRequest(payload) {
  if (!payload) return false;

  return (
    payload[MSG_KEY_REQUEST] !== undefined ||
    payload[String(MSG_KEY_REQUEST)] !== undefined ||
    payload.MSG_KEY_REQUEST !== undefined ||
    payload.request !== undefined
  );
}

function fetchWeather(lat, lon) {
  var apiKey = getApiKey();
  var useOpenWeather = apiKey && apiKey.length > 0;
  var url = '';

  if (useOpenWeather) {
    url =
      'https://api.openweathermap.org/data/2.5/weather' +
      '?lat=' + encodeURIComponent(lat) +
      '&lon=' + encodeURIComponent(lon) +
      '&units=metric' +
      '&appid=' + encodeURIComponent(apiKey);

    log('Fetching weather via OpenWeather.');
  } else {
    url =
      'https://api.open-meteo.com/v1/forecast' +
      '?latitude=' + encodeURIComponent(lat) +
      '&longitude=' + encodeURIComponent(lon) +
      '&current_weather=true';

    log('Fetching weather via OpenMeteo fallback.');
  }

  var xhr = new XMLHttpRequest();

  xhr.onload = function() {
    log('Weather status: ' + xhr.status);

    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        var temp = null;

        if (useOpenWeather) {
          if (
            data &&
            data.main &&
            data.main.temp !== undefined &&
            data.main.temp !== null
          ) {
            temp = Math.round(data.main.temp);
          }
        } else {
          if (
            data &&
            data.current_weather &&
            data.current_weather.temperature !== undefined &&
            data.current_weather.temperature !== null
          ) {
            temp = Math.round(data.current_weather.temperature);
          }
        }

        if (temp !== null) {
          sendTemperature(temp);
        } else {
          log('Weather JSON has no usable temperature.');
          sendTemperature(93);
        }
      } catch (err) {
        log('Weather JSON parse error: ' + err);
        sendTemperature(93);
      }
    } else if (xhr.status === 401 || xhr.status === 403) {
      log('API key error.');
      sendTemperature(92);
    } else {
      log('Weather network error status: ' + xhr.status);
      sendTemperature(94);
    }
  };

  xhr.onerror = function() {
    log('Weather xhr error.');
    sendTemperature(94);
  };

  xhr.ontimeout = function() {
    log('Weather xhr timeout.');
    sendTemperature(94);
  };

  xhr.timeout = 15000;
  xhr.open('GET', url);
  xhr.send();
}

function getLocationAndFetch() {
  if (!isWeatherEnabled()) {
    log('Weather disabled.');
    sendTemperature(99);
    return;
  }

  if (!navigator.geolocation) {
    log('No geolocation available.');
    sendTemperature(95);
    return;
  }

  navigator.geolocation.getCurrentPosition(
    function(pos) {
      log('Location received.');

      fetchWeather(
        pos.coords.latitude,
        pos.coords.longitude
      );
    },
    function(err) {
      log('Location error: ' + JSON.stringify(err));
      sendTemperature(95);
    },
    {
      timeout: 15000,
      maximumAge: 300000,
      enableHighAccuracy: false
    }
  );
}

Pebble.addEventListener('ready', function() {
  log('PebbleKit JS ready.');
});

Pebble.addEventListener('appmessage', function(e) {
  log('AppMessage received: ' + JSON.stringify(e.payload));

  if (hasWeatherRequest(e.payload)) {
    log('Weather request received.');
    getLocationAndFetch();
  }
});

Pebble.addEventListener('showConfiguration', function(e) {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) {
    log('Configuration closed without response.');
    return;
  }

  var dict = {};

  try {
    dict = clay.getSettings(e.response);
  } catch (err) {
    log('Clay getSettings error: ' + err);
  }

  saveSettings(dict);

  Pebble.sendAppMessage(
    dict,
    function() {
      log('Settings sent to watch: ' + JSON.stringify(dict));
    },
    function(err) {
      log('Settings send error: ' + JSON.stringify(err));
    }
  );

  if (isWeatherEnabled()) {
    getLocationAndFetch();
  }
});
