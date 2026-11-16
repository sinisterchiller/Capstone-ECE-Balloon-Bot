// Atmospheric charts panel — temperature, pressure, altitude, vertical speed trends.

import { store } from '../store.js';
import { fmtTemp, fmtPress } from '../format.js';

const MAX_POINTS = 300;

function drawSparkline(svg, data, valueKey, yMin, yMax, yLabel, yFormat, color) {
  const w = svg.clientWidth || 400;
  const h = svg.clientHeight || 68;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  if (data.length < 2) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="9">No data</text>`;
    return;
  }

  const pad = { l: 40, r: 8, t: 4, b: 2 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  // Find actual min/max from data
  let dMin = Infinity, dMax = -Infinity;
  for (const d of data) {
    const v = d[valueKey];
    if (v != null) { dMin = Math.min(dMin, v); dMax = Math.max(dMax, v); }
  }
  if (!Number.isFinite(dMin)) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="9">No data</text>`;
    return;
  }

  const range = dMax - dMin || 1;
  const fMin = dMin - range * 0.1;
  const fMax = dMax + range * 0.1;

  const points = [];
  for (let i = 0; i < data.length; i++) {
    const d = data[i];
    const v = d[valueKey];
    if (v == null) continue;
    const x = pad.l + (i / (data.length - 1)) * pw;
    const y = pad.t + ph - ((v - fMin) / (fMax - fMin)) * ph;
    points.push(`${x.toFixed(1)},${y.toFixed(1)}`);
  }

  // Grid line at midpoint
  const midY = pad.t + ph / 2;
  const midLine = document.createElementNS('http://www.w3.org/2000/svg', 'line');
  midLine.setAttribute('x1', pad.l);
  midLine.setAttribute('x2', w - pad.r);
  midLine.setAttribute('y1', midY);
  midLine.setAttribute('y2', midY);
  midLine.setAttribute('stroke', '#232d3b');
  midLine.setAttribute('stroke-width', '0.5');
  midLine.setAttribute('stroke-dasharray', '3');
  svg.appendChild(midLine);

  // Y-axis labels
  const topVal = dMax;
  const botVal = dMin;
  const topText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  topText.setAttribute('x', pad.l - 4);
  topText.setAttribute('y', pad.t + 10);
  topText.setAttribute('text-anchor', 'end');
  topText.setAttribute('fill', '#7b8a9d');
  topText.setAttribute('font-size', '8');
  topText.setAttribute('font-family', 'var(--mono)');
  topText.textContent = yFormat(topVal);
  svg.appendChild(topText);

  const botText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  botText.setAttribute('x', pad.l - 4);
  botText.setAttribute('y', h - pad.b);
  botText.setAttribute('text-anchor', 'end');
  botText.setAttribute('fill', '#7b8a9d');
  botText.setAttribute('font-size', '8');
  botText.setAttribute('font-family', 'var(--mono)');
  botText.textContent = yFormat(botVal);
  svg.appendChild(botText);

  // Data line
  const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  polyline.setAttribute('points', points.join(' '));
  polyline.setAttribute('fill', 'none');
  polyline.setAttribute('stroke', color);
  polyline.setAttribute('stroke-width', '1.2');
  polyline.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(polyline);
}

const CHARTS = [
  { key: 'temp_ext', label: 'Temperature', format: fmtTemp, color: '#f87171', unit: '°C' },
  { key: 'pressure', label: 'Pressure', format: fmtPress, color: '#38bdf8', unit: 'hPa' },
  { key: 'alt', label: 'Altitude', format: v => `${Math.round(v)} m`, color: '#34d399', unit: 'm' },
  { key: 'vspeed', label: 'Vertical Speed', format: v => `${v.toFixed(1)} m/s`, color: '#fbbf24', unit: 'm/s' },
];

export function renderCharts(container, store) {
  const s = store.state;

  // Build chart rows
  const chartRows = CHARTS.map(c =>
    `<div class="chart-row">` +
      `<div class="chart-label"><span>${c.label}</span><span class="chart-val" id="cv-${c.key}">—</span></div>` +
      `<svg class="chart-svg" id="svg-${c.key}"></svg>` +
    `</div>`
  ).join('');

  container.innerHTML = `
    <h2 class="panel-title">Atmospheric Trends</h2>
    ${chartRows}`;

  function render() {
    const packets = s.packets;
    const count = Math.min(MAX_POINTS, packets.length);
    if (count === 0) return;

    // Extract data slices
    const data = [];
    for (let i = 0; i < count; i++) {
      const p = packets.get(packets.length - count + i);
      if (p) data.push(p);
    }

    for (const c of CHARTS) {
      const svg = container.querySelector(`#svg-${c.key}`);
      if (!svg) continue;

      // Current value
      const last = packets.last(1)[0];
      const valEl = container.querySelector(`#cv-${c.key}`);
      if (last && last[c.key] != null && valEl) {
        valEl.textContent = c.format(last[c.key]);
      }

      drawSparkline(svg, data, c.key, null, null, '', c.format, c.color);
    }
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
