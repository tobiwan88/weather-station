// config.js — populate form from /api/config, sessionStorage flash feedback

// Show flash message if we just saved settings (post-redirect-get pattern)
(function () {
  const key = 'configSaved';
  if (sessionStorage.getItem(key)) {
    sessionStorage.removeItem(key);
    const el = document.getElementById('flashMsg');
    if (el) {
      el.textContent = 'Settings saved.';
      el.classList.remove('hidden');
    }
  }
})();

// Before any form POST, set the flag so the redirect GET shows the flash
['triggerForm', 'sntpForm', 'sensorsForm'].forEach(function (id) {
  const form = document.getElementById(id);
  if (form) {
    form.addEventListener('submit', function () {
      sessionStorage.setItem('configSaved', '1');
    });
  }
});

// Populate form fields from /api/config
fetch('/api/config').then(function (r) { return r.json(); }).then(function (d) {
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
    var c2 = row.insertCell(2); c2.className = 'ro'; c2.textContent = s.dt_location || s.location || '';

    function mkInput(name, val, maxLen) {
      var inp = document.createElement('input');
      inp.type = 'text';
      inp.name = name;
      inp.value = val || '';
      inp.maxLength = maxLen;
      return inp;
    }

    row.insertCell(3).appendChild(mkInput('sensor_' + s.uid + '_name', s.display_name, 32));
    row.insertCell(4).appendChild(mkInput('sensor_' + s.uid + '_loc', s.location, 32));
    row.insertCell(5).appendChild(mkInput('sensor_' + s.uid + '_desc', s.description, 64));

    var sel = document.createElement('select');
    sel.name = 'sensor_' + s.uid + '_en';
    [['1', 'Enabled'], ['0', 'Disabled']].forEach(function (o) {
      var opt = document.createElement('option');
      opt.value = o[0]; opt.text = o[1];
      if ((o[0] === '1' && s.enabled) || (o[0] === '0' && !s.enabled)) opt.selected = true;
      sel.appendChild(opt);
    });
    row.insertCell(6).appendChild(sel);
  });
}).catch(function (e) { console.error('config fetch', e); });
