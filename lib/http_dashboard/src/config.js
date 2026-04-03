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
    row.insertCell(1).textContent = s.label;
    row.insertCell(2).textContent = s.location;
    var inp = document.createElement('input');
    inp.type = 'text';
    inp.name = 'desc_' + s.uid;
    inp.value = s.description || '';
    inp.maxLength = 32;
    inp.placeholder = 'e.g. Living room';
    row.insertCell(3).appendChild(inp);
  });
}).catch(function (e) { console.error('config fetch', e); });
