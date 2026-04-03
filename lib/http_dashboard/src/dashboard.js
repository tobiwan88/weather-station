// dashboard.js — Chart.js 4.x update loop, per-type chart sections

const typeCharts = new Map(); // type → { chart, datasets: Map<uid → dataset>, chipEls: Map<uid → el> }
let globalTBase = 0;

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
  return (s.description && s.description.length > 0)
    ? s.description
    : (s.label + '@' + s.location);
}

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
  if (entry.chipEls.has(uid)) return entry.chipEls.get(uid);

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
  entry.chipEls.set(uid, valEl);
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
      const type = s.type || 'unknown';
      const uid  = s.uid;
      const label = sensorLabel(s);

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

    typeCharts.forEach(entry => entry.chart.update('active'));
    setStatus(true);
  }).catch(() => setStatus(false));
}

update();
setInterval(update, 5000);
