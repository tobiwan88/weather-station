// config.js — populate form from /api/config, manage locations and credentials

// Show a flash message at the top of the page.
function showFlash(msg) {
  var el = document.getElementById('flashMsg');
  if (el) {
    el.textContent = msg;
    el.classList.remove('hidden');
    setTimeout(function () { el.classList.add('hidden'); }, 3000);
  }
}

// Redirect to login page when the session is missing or expired.
function redirectToLogin() {
  window.location.replace('/login');
}

// POST form data via fetch; redirects to /login on 401.
// Returns a Promise that resolves to the parsed JSON response.
function postForm(form, path) {
  path = path || '/api/config';
  var data = new URLSearchParams(new FormData(form));
  return fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: data.toString(),
  }).then(function (r) {
    if (r.status === 401 || r.status === 302) {
      redirectToLogin();
      throw new Error('Session expired');
    }
    if (r.status === 403) {
      throw new Error('Wrong credentials');
    }
    if (!r.ok) { throw new Error('HTTP ' + r.status); }
    return r.json();
  });
}

// Wire up a form so it submits via fetch instead of a native POST.
function interceptForm(id, onSuccess, path) {
  var form = document.getElementById(id);
  if (!form) return;
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    postForm(form, path).then(function (json) {
      if (onSuccess) { onSuccess(json); }
      else { showFlash('Settings saved.'); }
    }).catch(function (err) {
      showFlash('Error: ' + err.message);
    });
  });
}

// Render the locations management list
function renderLocations(locations) {
  var list = document.getElementById('locList');
  if (!list) return;
  list.innerHTML = '';
  if (!locations || locations.length === 0) {
    list.innerHTML = '<p class="hint muted">No locations defined yet.</p>';
    return;
  }
  locations.forEach(function (name) {
    var row = document.createElement('div');
    row.className = 'loc-row';

    var label = document.createElement('span');
    label.className = 'loc-name';
    label.textContent = name;

    var form = document.createElement('form');
    form.style.display = 'inline';

    var hidAction = document.createElement('input');
    hidAction.type = 'hidden';
    hidAction.name = 'action';
    hidAction.value = 'remove_location';

    var hidName = document.createElement('input');
    hidName.type = 'hidden';
    hidName.name = 'loc_name';
    hidName.value = name;

    var btn = document.createElement('button');
    btn.type = 'submit';
    btn.className = 'btn-danger btn-small';
    btn.textContent = 'Remove';

    form.appendChild(hidAction);
    form.appendChild(hidName);
    form.appendChild(btn);

    form.addEventListener('submit', function (e) {
      e.preventDefault();
      postForm(form).then(function () {
        return fetch('/api/config').then(function (r) {
          if (r.status === 401) { redirectToLogin(); throw new Error('auth'); }
          return r.json();
        });
      }).then(function (d) {
        renderLocations(d.locations || []);
        showFlash('Location removed.');
      }).catch(function (err) {
        showFlash('Error: ' + err.message);
      });
    });

    row.appendChild(label);
    row.appendChild(form);
    list.appendChild(row);
  });
}

// Build a location <select> for a sensor row
function mkLocSelect(fieldName, currentVal, locations) {
  var sel = document.createElement('select');
  sel.name = fieldName;

  var emptyOpt = document.createElement('option');
  emptyOpt.value = '';
  emptyOpt.text = '— unassigned —';
  if (!currentVal) emptyOpt.selected = true;
  sel.appendChild(emptyOpt);

  (locations || []).forEach(function (loc) {
    var opt = document.createElement('option');
    opt.value = loc;
    opt.text = loc;
    if (loc === currentVal) opt.selected = true;
    sel.appendChild(opt);
  });
  return sel;
}

// Wire up static config forms
interceptForm('triggerForm');
interceptForm('sntpForm');
interceptForm('sensorsForm');
interceptForm('mqttForm');

// Change-credentials form
interceptForm('changeCredsForm', function () {
  showFlash('Credentials updated. Sign in again to continue.');
  document.getElementById('changeCredsForm').reset();
}, '/api/change-credentials');

// Add-location form: refresh list on success.
(function () {
  var form = document.getElementById('addLocForm');
  if (!form) return;
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    postForm(form).then(function () {
      return fetch('/api/config').then(function (r) {
        if (r.status === 401) { redirectToLogin(); throw new Error('auth'); }
        return r.json();
      });
    }).then(function (d) {
      renderLocations(d.locations || []);
      showFlash('Location added.');
      var inp = document.getElementById('locNameInput');
      if (inp) inp.value = '';
    }).catch(function (err) {
      showFlash('Error: ' + err.message);
    });
  });
})();

// Rotate API token button handler
function onRotateToken() {
  fetch('/api/token/rotate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: '',
  }).then(function (r) {
    if (r.status === 401) { redirectToLogin(); return null; }
    if (!r.ok) { throw new Error('HTTP ' + r.status); }
    return r.json();
  }).then(function (d) {
    if (!d) return;
    var el = document.getElementById('apiTokenDisplay');
    if (el && d.token) { el.value = d.token; }
    showFlash('API token rotated.');
  }).catch(function (err) {
    showFlash('Error: ' + err.message);
  });
}

// Sign-out handler
function onLogout() {
  fetch('/api/logout', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: '',
  }).then(function () {
    redirectToLogin();
  }).catch(function () {
    redirectToLogin();
  });
}

// Populate form fields from /api/config
fetch('/api/config').then(function (r) {
  if (r.status === 401) { redirectToLogin(); return null; }
  return r.json();
}).then(function (d) {
  if (!d) return;
  var locations = d.locations || [];

  renderLocations(locations);

  var ti = document.getElementById('ti');
  var ss = document.getElementById('ss');
  if (ti) ti.value = d.trigger_interval_ms;
  if (ss) ss.value = d.sntp_server || '';

  // Populate API token display
  var apiTok = document.getElementById('apiTokenDisplay');
  if (apiTok && d.api_token) { apiTok.value = d.api_token; }

  var mqEnabled = document.getElementById('mqEnabled');
  var mqHost = document.getElementById('mqHost');
  var mqPort = document.getElementById('mqPort');
  var mqUser = document.getElementById('mqUser');
  var mqGw = document.getElementById('mqGw');
  var mqKeepalive = document.getElementById('mqKeepalive');
  var mqttAvailable = d.mqtt !== undefined;
  [mqEnabled, mqHost, mqPort, mqUser, mqGw, mqKeepalive].forEach(function (el) {
    if (el) el.disabled = !mqttAvailable;
  });
  if (!mqttAvailable) {
    if (mqHost) { mqHost.value = ''; mqHost.placeholder = 'MQTT not available in this build'; }
    if (mqPort) mqPort.value = '';
    if (mqUser) mqUser.value = '';
    if (mqGw) mqGw.value = '';
    if (mqKeepalive) mqKeepalive.value = '';
    if (mqEnabled) mqEnabled.checked = false;
  } else {
    var mq = d.mqtt;
    if (mqEnabled) mqEnabled.checked = mq.enabled !== false;
    if (mqHost) mqHost.value = mq.host || '';
    if (mqPort) mqPort.value = mq.port || 1883;
    if (mqUser) mqUser.value = mq.user || '';
    if (mqGw) mqGw.value = mq.gateway || '';
    if (mqKeepalive) mqKeepalive.value = mq.keepalive || 60;
  }

  var tb = document.getElementById('sb');
  if (!tb) return;
  (d.sensors || []).forEach(function (s) {
    var row = tb.insertRow(-1);
    row.insertCell(0).textContent = '0x' + s.uid.toString(16).padStart(8, '0');

    var c1 = row.insertCell(1); c1.className = 'ro'; c1.textContent = s.dt_label || s.label || '';

    function mkInput(name, val, maxLen) {
      var inp = document.createElement('input');
      inp.type = 'text';
      inp.name = name;
      inp.value = val || '';
      inp.maxLength = maxLen;
      return inp;
    }

    row.insertCell(2).appendChild(mkInput('sensor_' + s.uid + '_name', s.display_name, 32));
    row.insertCell(3).appendChild(mkLocSelect('sensor_' + s.uid + '_loc', s.location, locations));
    row.insertCell(4).appendChild(mkInput('sensor_' + s.uid + '_desc', s.description, 64));

    var sel = document.createElement('select');
    sel.name = 'sensor_' + s.uid + '_en';
    [['1', 'Enabled'], ['0', 'Disabled']].forEach(function (o) {
      var opt = document.createElement('option');
      opt.value = o[0]; opt.text = o[1];
      if ((o[0] === '1' && s.enabled) || (o[0] === '0' && !s.enabled)) opt.selected = true;
      sel.appendChild(opt);
    });
    row.insertCell(5).appendChild(sel);
  });
}).catch(function (e) { console.error('config fetch', e); });
