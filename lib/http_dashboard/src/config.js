// config.js — populate form from /api/config, manage locations

// Show a flash message at the top of the page.
function showFlash(msg) {
  var el = document.getElementById('flashMsg');
  if (el) {
    el.textContent = msg;
    el.classList.remove('hidden');
    setTimeout(function () { el.classList.add('hidden'); }, 3000);
  }
}

// POST form data via fetch and show a flash on success.
// Returns a Promise that resolves to the parsed JSON response.
function postForm(form) {
  var data = new URLSearchParams(new FormData(form));
  return fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: data.toString(),
  }).then(function (r) {
    if (!r.ok) { throw new Error('HTTP ' + r.status); }
    return r.json();
  });
}

// Wire up a form so that it submits via fetch instead of a native POST.
// onSuccess(json) is called after a successful POST.
function interceptForm(id, onSuccess) {
  var form = document.getElementById(id);
  if (!form) return;
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    postForm(form).then(function (json) {
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

    // Intercept remove-location submit; refresh list on success.
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      postForm(form).then(function () {
        return fetch('/api/config').then(function (r) { return r.json(); });
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

  // Empty / unassigned option
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

// Wire up the static forms.
interceptForm('triggerForm');
interceptForm('sntpForm');
interceptForm('sensorsForm');

// Add-location form: refresh list on success.
(function () {
  var form = document.getElementById('addLocForm');
  if (!form) return;
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    postForm(form).then(function () {
      return fetch('/api/config').then(function (r) { return r.json(); });
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

// Populate form fields from /api/config (which now includes locations array)
fetch('/api/config').then(function (r) { return r.json(); }).then(function (d) {
  var locations = d.locations || [];

  renderLocations(locations);

  var ti = document.getElementById('ti');
  var ss = document.getElementById('ss');
  if (ti) ti.value = d.trigger_interval_ms;
  if (ss) ss.value = d.sntp_server || '';

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
