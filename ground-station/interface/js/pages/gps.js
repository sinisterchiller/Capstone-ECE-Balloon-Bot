// GPS Tracking page — large flight map + telemetry panel.

import { store } from '../store.js';
import { fmtLat, fmtLon, fmtAlt, fmtVSpeed } from '../format.js';

const MAX_PATH_POINTS = 2000;
const VIEW_DEG_LAT = 0.36; // ~40 km vertical view
const VIEW_DEG_LON = 0.44; // ~40 km horizontal view

function project(lat, lon, centerLat, centerLon, w, h) {
  const cx = ((lon - centerLon) / VIEW_DEG_LON + 0.5) * w;
  const cy = (1 - (lat - centerLat) / VIEW_DEG_LAT - 0.5) * h;
  return { x: Math.max(0, Math.min(w, cx)), y: Math.max(0, Math.min(h, cy)) };
}

function distanceKm(lat1, lon1, lat2, lon2) {
  const R = 6371;
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const a = Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) * Math.sin(dLon / 2) ** 2;
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function subsample(arr, max) {
  if (arr.length <= max) return arr;
  const stride = Math.ceil(arr.length / max);
  const out = [];
  for (let i = 0; i < arr.length; i += stride) out.push(arr[i]);
  return out;
}

function fixLabel(fix) {
  if (fix === 0) return 'No Fix';
  if (fix === 2) return '2D Fix';
  return '3D Fix';
}

function fixClass(fix) {
  if (fix === 0) return 'fault-text';
  if (fix === 2) return 'warn-text';
  return 'ok-text';
}

export function renderGps(root) {
  const s = store.state;

  root.innerHTML = `
    <div style="display:grid;grid-template-columns:70% 30%;gap:var(--gap);min-height:0;">
      <div class="panel map-panel" id="gps-map">
        <h2 class="panel-title">Flight Path</h2>
        <svg class="map-svg" id="gps-svg"></svg>
      </div>
      <div style="display:flex;flex-direction:column;gap:var(--gap);">
        <div class="panel" id="gps-info">
          <h2 class="panel-title">Current Position</h2>
          <div id="gps-coords" style="margin-bottom:10px;"></div>
          <div id="gps-alt" style="margin-bottom:10px;"></div>
          <div id="gps-vspeed" style="margin-bottom:10px;"></div>
          <div id="gps-fix" style="margin-bottom:10px;"></div>
          <div id="gps-pkt" style="margin-bottom:10px;"></div>
        </div>
        <div class="panel" id="gps-stats">
          <h2 class="panel-title">Flight Statistics</h2>
          <div id="gps-stats-body"></div>
        </div>
        <div class="panel" id="gps-history">
          <h2 class="panel-title">Tracking History</h2>
          <div class="table-wrap" style="max-height:180px;">
            <table class="grid" id="gps-history-table">
              <thead>
                <tr><th>Time</th><th>Alt</th><th>Lat</th><th>Lon</th></tr>
              </thead>
              <tbody id="gps-history-body"></tbody>
            </table>
          </div>
        </div>
      </div>
    </div>`;

  const svg = root.querySelector('#gps-svg');
  const coordsEl = root.querySelector('#gps-coords');
  const altEl = root.querySelector('#gps-alt');
  const vspeedEl = root.querySelector('#gps-vspeed');
  const fixEl = root.querySelector('#gps-fix');
  const pktEl = root.querySelector('#gps-pkt');
  const statsEl = root.querySelector('#gps-stats-body');
  const historyBody = root.querySelector('#gps-history-body');

  // Running stats
  let maxAlt = 0;
  let totalDistance = 0;
  let lastLat = null;
  let lastLon = null;

  function updateStats(p) {
    if (p.alt != null && p.alt > maxAlt) maxAlt = p.alt;
    if (p.lat != null && p.lon != null && lastLat != null) {
      totalDistance += distanceKm(lastLat, lastLon, p.lat, p.lon);
    }
    lastLat = p.lat;
    lastLon = p.lon;
  }

  function renderStats() {
    statsEl.innerHTML = `
      <div style="margin-bottom:8px;display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Max Altitude</span>
        <span class="mono" style="font-size:14px;">${maxAlt > 0 ? Math.round(maxAlt).toLocaleString() + ' m' : '—'}</span>
      </div>
      <div style="margin-bottom:8px;display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Distance</span>
        <span class="mono" style="font-size:14px;">${totalDistance > 0 ? totalDistance.toFixed(2) + ' km' : '—'}</span>
      </div>
      <div style="display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Packets</span>
        <span class="mono" style="font-size:14px;">${s.packets.length}</span>
      </div>`;
  }

  // Sample history entries (every Nth packet to avoid table bloat)
  const historySamples = [];
  const SAMPLE_INTERVAL = 10;

  function render() {
    const packets = s.packets;
    const count = packets.length;

    if (count === 0) {
      svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="12">Awaiting GPS fix</text>';
      coordsEl.innerHTML = '<p style="color:var(--muted);text-align:center;padding:20px;">Awaiting GPS data</p>';
      altEl.innerHTML = vspeedEl.innerHTML = fixEl.innerHTML = pktEl.innerHTML = '';
      statsEl.innerHTML = '';
      historyBody.innerHTML = '';
      return;
    }

    const last = packets.last(1)[0];
    if (!last) return;

    // Update stats
    updateStats(last);
    renderStats();

    // Current position panel
    if (last.lat != null && last.lon != null) {
      coordsEl.innerHTML = `
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Latitude</span>
          <span class="mono" style="font-size:14px;">${fmtLat(last.lat)}</span>
        </div>
        <div style="display:flex;justify-content:space-between;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Longitude</span>
          <span class="mono" style="font-size:14px;">${fmtLon(last.lon)}</span>
        </div>`;
    } else {
      coordsEl.innerHTML = '<p style="color:var(--muted);text-align:center;padding:20px;">No GPS fix</p>';
    }

    altEl.innerHTML = `
      <div style="display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Altitude</span>
        <span class="mono" style="font-size:14px;">${last.alt != null ? fmtAlt(last.alt) : '—'}</span>
      </div>`;

    vspeedEl.innerHTML = `
      <div style="display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Vertical Speed</span>
        <span class="mono" style="font-size:14px;">${last.vspeed != null ? last.vspeed.toFixed(1) + ' m/s' : '—'}</span>
      </div>`;

    fixEl.innerHTML = `
      <div style="display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">GNSS Fix</span>
        <span class="mono ${fixClass(last.fix)}" style="font-size:14px;">${fixLabel(last.fix)}</span>
      </div>`;

    pktEl.innerHTML = `
      <div style="display:flex;justify-content:space-between;">
        <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Last Packet</span>
        <span class="mono" style="font-size:12px;">${last.rx_ts > 0 ? new Date(last.rx_ts).toISOString().slice(11, 19) : '—'}</span>
      </div>`;

    // ---- Map ----
    const w = svg.clientWidth || 600;
    const h = svg.clientHeight || 480;
    svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
    svg.innerHTML = '';

    // Background
    const bg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    bg.setAttribute('width', w);
    bg.setAttribute('height', h);
    bg.setAttribute('fill', '#0f151d');
    svg.appendChild(bg);

    if (last.lat == null || last.lon == null) {
      svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="12">Awaiting GPS fix</text>';
      return;
    }

    const centerLat = last.lat;
    const centerLon = last.lon;

    // Grid
    const grid = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    const latStep = VIEW_DEG_LAT / 4;
    const lonStep = VIEW_DEG_LON / 4;

    for (let i = -4; i <= 4; i++) {
      const lat = centerLat + (i / 4) * VIEW_DEG_LAT;
      const p = project(lat, centerLon, centerLat, centerLon, w, h);
      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      line.setAttribute('x1', 0);
      line.setAttribute('x2', w);
      line.setAttribute('y1', p.y);
      line.setAttribute('y2', p.y);
      line.setAttribute('stroke', '#1a2332');
      line.setAttribute('stroke-width', '0.5');
      grid.appendChild(line);

      const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      label.setAttribute('x', w - 4);
      label.setAttribute('y', p.y - 3);
      label.setAttribute('text-anchor', 'end');
      label.setAttribute('fill', '#3d4f63');
      label.setAttribute('font-size', '8');
      label.setAttribute('font-family', 'var(--mono)');
      label.textContent = `${lat.toFixed(2)}°`;
      grid.appendChild(label);
    }

    for (let i = -4; i <= 4; i++) {
      const lon = centerLon + (i / 4) * VIEW_DEG_LON;
      const p = project(centerLat, lon, centerLat, centerLon, w, h);
      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      line.setAttribute('x1', p.x);
      line.setAttribute('x2', p.x);
      line.setAttribute('y1', 0);
      line.setAttribute('y2', h);
      line.setAttribute('stroke', '#1a2332');
      line.setAttribute('stroke-width', '0.5');
      grid.appendChild(line);
    }
    svg.appendChild(grid);

    // Flight path
    const pathData = [];
    for (let i = 0; i < count; i++) {
      const p = packets.get(i);
      if (p.lat != null && p.lon != null) {
        pathData.push(project(p.lat, p.lon, centerLat, centerLon, w, h));
      }
    }

    if (pathData.length > 1) {
      const subsampled = subsample(pathData, MAX_PATH_POINTS);
      const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
      polyline.setAttribute('points', subsampled.map(p => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' '));
      polyline.setAttribute('fill', 'none');
      polyline.setAttribute('stroke', '#38bdf8');
      polyline.setAttribute('stroke-width', '1.5');
      polyline.setAttribute('stroke-linejoin', 'round');
      polyline.setAttribute('opacity', '0.8');
      svg.appendChild(polyline);
    }

    // Markers
    // Launch position
    if (s.session.launch && s.session.launch.lat != null) {
      const lp = project(s.session.launch.lat, s.session.launch.lon, centerLat, centerLon, w, h);
      const launchMarker = document.createElementNS('http://www.w3.org/2000/svg', 'g');
      const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      r.setAttribute('x', lp.x - 5);
      r.setAttribute('y', lp.y - 5);
      r.setAttribute('width', 10);
      r.setAttribute('height', 10);
      r.setAttribute('fill', 'none');
      r.setAttribute('stroke', '#34d399');
      r.setAttribute('stroke-width', '2');
      r.setAttribute('rx', '1');
      launchMarker.appendChild(r);
      svg.appendChild(launchMarker);
    }

    // Current position
    const cp = project(last.lat, last.lon, centerLat, centerLon, w, h);
    const currentMarker = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    const outer = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    outer.setAttribute('cx', cp.x);
    outer.setAttribute('cy', cp.y);
    outer.setAttribute('r', 8);
    outer.setAttribute('fill', 'none');
    outer.setAttribute('stroke', '#38bdf8');
    outer.setAttribute('stroke-width', '1.5');
    outer.setAttribute('opacity', '0.4');
    currentMarker.appendChild(outer);
    const inner = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    inner.setAttribute('cx', cp.x);
    inner.setAttribute('cy', cp.y);
    inner.setAttribute('r', 4);
    inner.setAttribute('fill', '#38bdf8');
    inner.setAttribute('stroke', '#0b1017');
    inner.setAttribute('stroke-width', '1.5');
    currentMarker.appendChild(inner);
    svg.appendChild(currentMarker);

    // Crosshair
    const ch = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    ch.setAttribute('opacity', '0.3');
    const chV = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    chV.setAttribute('x1', w / 2);
    chV.setAttribute('y1', 0);
    chV.setAttribute('x2', w / 2);
    chV.setAttribute('y2', h);
    chV.setAttribute('stroke', '#7b8a9d');
    chV.setAttribute('stroke-width', '0.5');
    chV.setAttribute('stroke-dasharray', '4,4');
    ch.appendChild(chV);
    const chH = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    chH.setAttribute('x1', 0);
    chH.setAttribute('y1', h / 2);
    chH.setAttribute('x2', w);
    chH.setAttribute('y2', h / 2);
    chH.setAttribute('stroke', '#7b8a9d');
    chH.setAttribute('stroke-width', '0.5');
    chH.setAttribute('stroke-dasharray', '4,4');
    ch.appendChild(chH);
    svg.appendChild(ch);

    // History table (sampled)
    if (count >= SAMPLE_INTERVAL) {
      const rows = [];
      for (let i = count - 1; i >= 0; i -= SAMPLE_INTERVAL) {
        const p = packets.get(i);
        if (!p || p.lat == null || p.lon == null) continue;
        rows.push(
          `<tr>` +
          `<td>${new Date(p.rx_ts).toISOString().slice(11, 19)}</td>` +
          `<td>${p.alt != null ? Math.round(p.alt) + ' m' : '—'}</td>` +
          `<td>${p.lat != null ? p.lat.toFixed(4) : '—'}</td>` +
          `<td>${p.lon != null ? p.lon.toFixed(4) : '—'}</td>` +
          `</tr>`
        );
        if (rows.length >= 20) break;
      }
      historyBody.innerHTML = rows.join('');
    } else {
      historyBody.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--muted)">Collecting data...</td></tr>';
    }
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
