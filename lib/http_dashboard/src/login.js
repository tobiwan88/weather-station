// login.js — submit login form via fetch, redirect on success

document.getElementById('loginForm').addEventListener('submit', function (e) {
  e.preventDefault();
  var form = e.target;
  var data = new URLSearchParams(new FormData(form)).toString();

  fetch('/api/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: data,
  }).then(function (r) {
    if (r.status === 401) {
      showError('Invalid username or password.');
      return;
    }
    if (r.status === 503) {
      showError('Too many active sessions. Try again shortly.');
      return;
    }
    if (!r.ok) {
      showError('Server error (' + r.status + ').');
      return;
    }
    // Session cookie is set by the server (HttpOnly). Navigate to dashboard.
    window.location.replace('/');
  }).catch(function () {
    showError('Connection error. Is the device reachable?');
  });
});

function showError(msg) {
  var el = document.getElementById('loginError');
  if (el) {
    el.textContent = msg;
    el.classList.remove('hidden');
  }
}
