// Atmospheric Data page — live charts, vertical profiles, humidity, time range.

import { store } from '../store.js';
import { fmtTemp, fmtPress, fmtNum } from '../format.js';

const TIME_RANGES = [
  { label: 'All', value: Infinity },
  { label: '5 min', value: 300 },
  { label: '2 min', value: 120 },
  { label: '1 min', value: 60 },
];

function drawTimeChart(svg, data, valueKey, yFormat, color, timeRangeSec) {
  const w = svg.clientWidth || 500;
  const h = svg.clientHeight || 200;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  if (data.length < 2) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Collecting data...</text>`;
    return;
  }

  const pad = { l: 52, r: 16, t: 10, b: 32 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  // Filter by time range
  const latestTs = data[data.length - 1].rx_ts;
  const cutoff = latestTs - timeRangeSec * 1000;
  const filtered = data.filter(p => p.rx_ts >= cutoff && p[valueKey] != null);

  if (filtered.length < 2) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">No data in range</text>`;
    return;
  }

  let yMin = Infinity, yMax = -Infinity;
  let xMin = Infinity, xMax = -Infinity;
  for (const d of filtered) {
    const v = d[valueKey];
    yMin = Math.min(yMin, v);
    yMax = Math.max(yMax, v);
    xMin = Math.min(xMin, d.rx_ts);
    xMax = Math.max(xMax, d.rx_ts);
  }

  const yRange = yMax - yMin || 1;
  const xRange = xMax - xMin || 1;
  const fMin = yMin - yRange * 0.1;
  const fMax = yMax + yRange * 0.1;
  const tMin = xMin - xRange * 0.05;
  const tMax = xMax + xRange * 0.05;

  const points = filtered.map(d => {
    const x = pad.l + ((d.rx_ts - tMin) / (tMax - tMin)) * pw;
    const y = pad.t + ph - ((d[valueKey] - fMin) / (fMax - fMin)) * ph;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  // Grid
  const grid = document.createElementNS('http://www.w3.org/2000/svg', 'g');
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + (ph / 4) * i;
    const val = fMax - ((fMax - fMin) / 4) * i;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', pad.l);
    line.setAttribute('x2', w - pad.r);
    line.setAttribute('y1', y);
    line.setAttribute('y2', y);
    line.setAttribute('stroke', '#1a2332');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', pad.l - 6);
    text.setAttribute('y', y + 4);
    text.setAttribute('text-anchor', 'end');
    text.setAttribute('fill', '#7b8a9d');
    text.setAttribute('font-size', '9');
    text.setAttribute('font-family', 'var(--mono)');
    text.textContent = yFormat(val);
    grid.appendChild(text);
  }

  // X-axis labels
  const xLabelCount = 4;
  for (let i = 0; i <= xLabelCount; i++) {
    const t = tMin + ((tMax - tMin) / xLabelCount) * i;
    const x = pad.l + ((t - tMin) / (tMax - tMin)) * pw;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', x);
    line.setAttribute('x2', x);
    line.setAttribute('y1', pad.t);
    line.setAttribute('y2', pad.t + ph);
    line.setAttribute('stroke', '#1a2332');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const d = new Date(t);
    const label = `${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}`;
    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', x);
    text.setAttribute('y', h - 4);
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('fill', '#7b8a9d');
    text.setAttribute('font-size', '9');
    text.setAttribute('font-family', 'var(--mono)');
    text.textContent = label;
    grid.appendChild(text);
  }
  svg.appendChild(grid);

  // Data line
  const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  polyline.setAttribute('points', points.join(' '));
  polyline.setAttribute('fill', 'none');
  polyline.setAttribute('stroke', color);
  polyline.setAttribute('stroke-width', '1.5');
  polyline.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(polyline);

  // Current value marker
  const last = filtered[filtered.length - 1];
  const cx = pad.l + ((last.rx_ts - tMin) / (tMax - tMin)) * pw;
  const cy = pad.t + ph - ((last[valueKey] - fMin) / (fMax - fMin)) * ph;
  const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  dot.setAttribute('cx', cx);
  dot.setAttribute('cy', cy);
  dot.setAttribute('r', 3);
  dot.setAttribute('fill', color);
  dot.setAttribute('stroke', '#0b1017');
  dot.setAttribute('stroke-width', '1.5');
  svg.appendChild(dot);
}

function drawProfileChart(svg, xData, yData, xFormat, yFormat, color) {
  const w = svg.clientWidth || 500;
  const h = svg.clientHeight || 180;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  if (xData.length < 2 || yData.length < 2) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Collecting data...</text>`;
    return;
  }

  const pad = { l: 52, r: 16, t: 10, b: 32 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  let xMin = Infinity, xMax = -Infinity;
  let yMin = Infinity, yMax = -Infinity;
  for (let i = 0; i < xData.length; i++) {
    if (xData[i] == null || yData[i] == null) continue;
    xMin = Math.min(xMin, xData[i]);
    xMax = Math.max(xMax, xData[i]);
    yMin = Math.min(yMin, yData[i]);
    yMax = Math.max(yMax, yData[i]);
  }

  const xRange = xMax - xMin || 1;
  const yRange = yMax - yMin || 1;
  const fXMin = xMin - xRange * 0.1;
  const fXMax = xMax + xRange * 0.1;
  const fYMin = yMin - yRange * 0.1;
  const fYMax = yMax + yRange * 0.1;

  const points = [];
  for (let i = 0; i < xData.length; i++) {
    if (xData[i] == null || yData[i] == null) continue;
    const x = pad.l + ((xData[i] - fXMin) / (fXMax - fXMin)) * pw;
    const y = pad.t + ph - ((yData[i] - fYMin) / (fYMax - fYMin)) * ph;
    points.push(`${x.toFixed(1)},${y.toFixed(1)}`);
  }

  const grid = document.createElementNS('http://www.w3.org/2000/svg', 'g');
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + (ph / 4) * i;
    const val = fYMax - ((fYMax - fYMin) / 4) * i;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', pad.l);
    line.setAttribute('x2', w - pad.r);
    line.setAttribute('y1', y);
    line.setAttribute('y2', y);
    line.setAttribute('stroke', '#1a2332');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', pad.l - 6);
    text.setAttribute('y', y + 4);
    text.setAttribute('text-anchor', 'end');
    text.setAttribute('fill', '#7b8a9d');
    text.setAttribute('font-size', '9');
    text.setAttribute('font-family', 'var(--mono)');
    text.textContent = yFormat(val);
    grid.appendChild(text);
  }

  for (let i = 0; i <= 4; i++) {
    const x = fXMin + ((fXMax - fXMin) / 4) * i;
    const px = pad.l + ((x - fXMin) / (fXMax - fXMin)) * pw;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', px);
    line.setAttribute('x2', px);
    line.setAttribute('y1', pad.t);
    line.setAttribute('y2', pad.t + ph);
    line.setAttribute('stroke', '#1a2332');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', px);
    text.setAttribute('y', h - 4);
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('fill', '#7b8a9d');
    text.setAttribute('font-size', '9');
    text.setAttribute('font-family', 'var(--mono)');
    text.textContent = xFormat(x);
    grid.appendChild(text);
  }
  svg.appendChild(grid);

  const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  polyline.setAttribute('points', points.join(' '));
  polyline.setAttribute('fill', 'none');
  polyline.setAttribute('stroke', color);
  polyline.setAttribute('stroke-width', '1.5');
  polyline.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(polyline);
}

export function renderAtmospheric(root) {
  const s = store.state;

  // Time range buttons
  const timeRangeBtns = TIME_RANGES.map((tr, i) =>
    `<button class="atm-time-btn ${i === 0 ? 'active' : ''}" data-range="${tr.value}">${tr.label}</button>`
  ).join('');

  root.innerHTML = `
    <div style="margin-bottom:16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;">
      <h2 class="panel-title" style="margin:0;">Atmospheric Data</h2>
      <div style="display:flex;gap:4px;">${timeRangeBtns}</div>
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:var(--gap);margin-bottom:var(--gap);">
      <div class="panel" id="atm-temp-chart"></div>
      <div class="panel" id="atm-press-chart"></div>
      <div class="panel" id="atm-alt-chart"></div>
      <div class="panel" id="atm-vspeed-chart"></div>
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:var(--gap);margin-bottom:var(--gap);">
      <div class="panel" id="atm-profile-temp"></div>
      <div class="panel" id="atm-profile-press"></div>
    </div>
    <div class="panel" id="atm-humidity">
      <h2 class="panel-title">Internal / Enclosure Humidity (SHT45)</h2>
      <div style="display:flex;justify-content:space-between;align-items:baseline;">
        <span style="font-size:24px;font-family:var(--mono);" id="atm-rh-value">—</span>
        <span style="color:var(--muted);font-size:12px;">% RH (internal — not ambient)</span>
      </div>
      <svg class="chart-svg" id="atm-rh-chart" style="height:100px;margin-top:8px;"></svg>
    </div>`;

  // Build chart containers
  const charts = [
    { id: 'atm-temp-chart', key: 'temp_ext', label: 'External Temperature vs Time', format: fmtTemp, color: '#f87171' },
    { id: 'atm-press-chart', key: 'pressure', label: 'Pressure vs Time', format: fmtPress, color: '#38bdf8' },
    { id: 'atm-alt-chart', key: 'alt', label: 'Altitude vs Time', format: v => `${Math.round(v)} m`, color: '#34d399' },
    { id: 'atm-vspeed-chart', key: 'vspeed', label: 'Vertical Speed vs Time', format: v => `${v.toFixed(1)} m/s`, color: '#fbbf24' },
  ];

  charts.forEach(c => {
    const el = root.querySelector(`#${c.id}`);
    el.innerHTML = `<h2 class="panel-title">${c.label}</h2><svg id="svg-${c.key}" style="width:100%;height:200px;"></svg>`;
  });

  // Profile containers
  const profileTemp = root.querySelector('#atm-profile-temp');
  profileTemp.innerHTML = `<h2 class="panel-title">Temperature vs Altitude</h2><svg id="svg-profile-temp" style="width:100%;height:180px;"></svg>`;

  const profilePress = root.querySelector('#atm-profile-press');
  profilePress.innerHTML = `<h2 class="panel-title">Pressure vs Altitude</h2><svg id="svg-profile-press" style="width:100%;height:180px;"></svg>`;

  let currentTimeRange = Infinity;

  // Time range buttons
  root.querySelectorAll('.atm-time-btn').forEach(btn => {
    btn.style.cssText = `
      padding: 4px 12px;
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 4px;
      color: var(--muted);
      font-size: 11px;
      cursor: pointer;
      font-family: var(--sans);
    `;
    btn.addEventListener('click', () => {
      root.querySelectorAll('.atm-time-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      btn.style.borderColor = 'var(--accent)';
      btn.style.color = 'var(--accent)';
      currentTimeRange = Number(btn.dataset.range);
    });
    // Initial style for active
    if (btn.classList.contains('active')) {
      btn.style.borderColor = 'var(--accent)';
      btn.style.color = 'var(--accent)';
    }
  });

  function render() {
    const packets = s.packets;
    const count = packets.length;
    if (count === 0) return;

    for (const c of charts) {
      const svg = root.querySelector(`#svg-${c.key}`);
      if (svg) drawTimeChart(svg, packets.items, c.key, c.format, c.color, currentTimeRange);
    }

    // Vertical profiles — use all packets with valid data
    const tempX = [], tempY = [];
    const pressX = [], pressY = [];
    for (let i = 0; i < count; i++) {
      const p = packets.get(i);
      if (!p) continue;
      if (p.temp_ext != null && p.alt != null) {
        tempX.push(p.alt);
        tempY.push(p.temp_ext);
      }
      if (p.pressure != null && p.alt != null) {
        pressX.push(p.alt);
        pressY.push(p.pressure);
      }
    }

    const svgProfileTemp = root.querySelector('#svg-profile-temp');
    if (svgProfileTemp) drawProfileChart(svgProfileTemp, tempX, tempY,
      v => `${Math.round(v)} m`, fmtTemp, '#f87171');

    const svgProfilePress = root.querySelector('#svg-profile-press');
    if (svgProfilePress) drawProfileChart(svgProfilePress, pressX, pressY,
      v => `${Math.round(v)} m`, fmtPress, '#38bdf8');

    // Humidity
    const last = packets.last(1)[0];
    const rhEl = root.querySelector('#atm-rh-value');
    if (last && last.rh != null) {
      rhEl.textContent = `${last.rh.toFixed(1)}%`;
      rhEl.style.color = last.rh > 80 ? 'var(--warn)' : 'var(--text)';
    }

    const rhSvg = root.querySelector('#atm-rh-chart');
    if (rhSvg) drawTimeChart(rhSvg, packets.items, 'rh', v => `${v.toFixed(0)}%`, '#a78bfa', currentTimeRange);
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
