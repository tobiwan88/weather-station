// dashboard.js — Chart.js 4.x update loop, per-type chart sections

// typeCharts: type → { chart, datasets: Map<uid→ds>, chipEls: Map<uid→{valEl,chipEl}> }
const typeCharts = new Map();
let globalTBase = 0;

// Location filter state
const selectedLocations = new Set();
// uid → location string (populated each update so filter always has fresh data)
const sensorLocations = new Map();

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function typeColor(type) {
  const v = cssVar('--color-' + type);
  return v || cssVar('--color-unknown');
}

function formatElapsed(ms) {
  const s = Math.round(ms / 1000);
  const m = Math.floor(s / 60);
  const ss = s % 60;
  return m + ':' + String(ss).padStart(2, '0');
}

function sensorLabel(s) {
  if (s.display_name && s.display_name.length > 0) return s.display_name;
  if (s.description && s.description.length > 0) return s.description;
  return (s.label + (s.location ? '@' + s.location : ''));
}

// --------------------------------------------------------------------------
// Location filter
// --------------------------------------------------------------------------

function renderFilterBar(locations) {
  var bar = document.getElementById('filterBar');
  if (!bar) return;
  bar.innerHTML = '';

  if (!locations || locations.length === 0) return;

  var label = document.createElement('span');
  label.className = 'filter-label';
  label.textContent = 'Location:';
  bar.appendChild(label);

  locations.forEach(function (loc) {
    var chip = document.createElement('button');
    chip.className = 'filter-chip' + (selectedLocations.has(loc) ? ' active' : '');
    chip.textContent = loc;
    chip.dataset.loc = loc;
    chip.addEventListener('click', function () {
      if (selectedLocations.has(loc)) {
        selectedLocations.delete(loc);
        chip.classList.remove('active');
      } else {
        selectedLocations.add(loc);
        chip.classList.add('active');
      }
      applyFilter();
    });
    bar.appendChild(chip);
  });
}

function sensorPassesFilter(uid) {
  if (selectedLocations.size === 0) return true;
  var loc = sensorLocations.get(uid) || '';
  return selectedLocations.has(loc);
}

function applyFilter() {
  typeCharts.forEach(function (entry, type) {
    var anyVisible = false;
    entry.datasets.forEach(function (ds, uid) {
      var show = sensorPassesFilter(uid);
      ds.hidden = !show;
      if (show) anyVisible = true;
      var chipInfo = entry.chipEls.get(uid);
      if (chipInfo) chipInfo.chipEl.style.display = show ? '' : 'none';
    });
    var section = document.getElementById('section-' + type);
    if (section) section.style.display = anyVisible ? '' : 'none';
    entry.chart.update('none');
  });
}

// --------------------------------------------------------------------------
// Chart section / chip / dataset helpers
// --------------------------------------------------------------------------

function ensureSection(type) {
  if (typeCharts.has(type)) return;

  const root = document.getElementById('chartsRoot');
  const section = document.createElement('section');
  section.className = 'sensor-section';
  section.id = 'section-' + type;

  const h2 = document.createElement('h2');
  h2.textContent = type.replace(/_/g, ' ');
  section.appendChild(h2);

  const chipsRow = document.createElement('div');
  chipsRow.className = 'chips-row';
  chipsRow.id = 'chips-' + type;
  section.appendChild(chipsRow);

  const wrap = document.createElement('div');
  wrap.className = 'chart-wrap';
  const canvas = document.createElement('canvas');
  canvas.id = 'canvas-' + type;
  wrap.appendChild(canvas);
  section.appendChild(wrap);

  root.appendChild(section);

  const color = typeColor(type);
  const chart = new Chart(canvas, {
    type: 'line',
    data: { datasets: [] },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: {
          labels: { color: cssVar('--text-muted'), font: { size: 11 } }
        },
        tooltip: {
          backgroundColor: cssVar('--surface2'),
          borderColor: cssVar('--border'),
          borderWidth: 1,
          titleColor: cssVar('--text'),
          bodyColor: cssVar('--text-muted'),
          callbacks: {
            title: function(items) {
              if (!items.length) return '';
              return 'T+' + formatElapsed(items[0].parsed.x);
            }
          }
        }
      },
      scales: {
        x: {
          type: 'linear',
          grid: { color: cssVar('--border') },
          ticks: {
            color: cssVar('--text-muted'),
            callback: function(v) { return formatElapsed(v); }
          },
          title: { display: true, text: 'elapsed', color: cssVar('--text-muted') }
        },
        y: {
          grid: { color: cssVar('--border') },
          ticks: { color: cssVar('--text-muted') },
          title: { display: true, text: type.replace(/_/g, ' '), color: cssVar('--text-muted') }
        }
      }
    }
  });

  typeCharts.set(type, { chart, datasets: new Map(), chipEls: new Map() });
}

function ensureChip(type, uid, label) {
  const entry = typeCharts.get(type);
  if (entry.chipEls.has(uid)) return entry.chipEls.get(uid).valEl;

  const chip = document.createElement('div');
  chip.className = 'sensor-chip';

  const valEl = document.createElement('div');
  valEl.className = 'chip-value';
  valEl.textContent = '—';

  const lblEl = document.createElement('div');
  lblEl.className = 'chip-label';
  lblEl.textContent = label;
  lblEl.title = label;

  chip.appendChild(valEl);
  chip.appendChild(lblEl);
  document.getElementById('chips-' + type).appendChild(chip);
  entry.chipEls.set(uid, { valEl, chipEl: chip });
  return valEl;
}

function ensureDataset(type, uid, label) {
  const entry = typeCharts.get(type);
  if (entry.datasets.has(uid)) return entry.datasets.get(uid);

  const color = typeColor(type);
  const ds = {
    label: label,
    data: [],
    borderColor: color,
    backgroundColor: color + '33',
    fill: false,
    tension: 0.3,
    pointRadius: 2,
    pointHoverRadius: 5,
    borderWidth: 2
  };
  entry.chart.data.datasets.push(ds);
  entry.datasets.set(uid, ds);
  return ds;
}

// --------------------------------------------------------------------------
// Status
// --------------------------------------------------------------------------

function setStatus(ok) {
  const dot = document.getElementById('statusDot');
  const txt = document.getElementById('statusText');
  if (ok) {
    dot.className = 'status-dot ok';
    txt.textContent = 'Updated ' + new Date().toLocaleTimeString();
  } else {
    dot.className = 'status-dot err';
    txt.textContent = 'Fetch error';
  }
}

// --------------------------------------------------------------------------
// Update loop
// --------------------------------------------------------------------------

function update() {
  fetch('/api/data').then(r => r.json()).then(data => {
    const sensors = data.sensors || [];

    // Compute globalTBase = min timestamp across all readings
    let tmin = Infinity;
    sensors.forEach(s => {
      (s.readings || []).forEach(r => { if (r.t < tmin) tmin = r.t; });
    });
    if (tmin !== Infinity && (globalTBase === 0 || tmin < globalTBase)) {
      globalTBase = tmin;
    }

    sensors.forEach(s => {
      const type  = s.type || 'unknown';
      const uid   = s.uid;
      const label = sensorLabel(s);

      // Keep location map fresh
      sensorLocations.set(uid, s.location || '');

      ensureSection(type);
      const chipValEl = ensureChip(type, uid, label);
      const ds = ensureDataset(type, uid, label);

      const pts = (s.readings || []).map(r => ({ x: r.t - globalTBase, y: r.v }));
      ds.data = pts;

      if (pts.length > 0) {
        const last = pts[pts.length - 1].y;
        chipValEl.textContent = Number.isInteger(last) ? last : last.toFixed(2);
      }
    });

    // Re-apply filter after data update (visibility may have changed)
    applyFilter();
    setStatus(true);
  }).catch(() => setStatus(false));
}

// --------------------------------------------------------------------------
// Init
// --------------------------------------------------------------------------

fetch('/api/locations').then(r => r.json()).then(d => {
  renderFilterBar(d.locations || []);
}).catch(() => {});

update();
setInterval(update, 5000);
