// GPS map panel — SVG equirectangular projection with flight path and markers.

import { store } from '../store.js';

const MAX_PATH_POINTS = 500;
const VIEW_DEG_LAT = 0.18; // ~20 km vertical view
const VIEW_DEG_LON = 0.22; // ~20 km horizontal view (adjusted for latitude)

function project(lat, lon, centerLat, centerLon, w, h) {
  const cx = ((lon - centerLon) / VIEW_DEG_LON + 0.5) * w;
  const cy = (1 - (lat - centerLat) / VIEW_DEG_LAT - 0.5) * h; // north is up
  return { x: Math.max(0, Math.min(w, cx)), y: Math.max(0, Math.min(h, cy)) };
}

function subsample(arr, max) {
  if (arr.length <= max) return arr;
  const stride = Math.ceil(arr.length / max);
  const out = [];
  for (let i = 0; i < arr.length; i += stride) out.push(arr[i]);
  return out;
}

function drawMarker(svg, x, y, type, color) {
  const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');

  if (type === 'launch') {
    // Square
    const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    r.setAttribute('x', x - 5);
    r.setAttribute('y', y - 5);
    r.setAttribute('width', 10);
    r.setAttribute('height', 10);
    r.setAttribute('fill', 'none');
    r.setAttribute('stroke', color);
    r.setAttribute('stroke-width', '2');
    r.setAttribute('rx', '1');
    g.appendChild(r);
  } else if (type === 'current') {
    // Filled circle with pulse ring
    const outer = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    outer.setAttribute('cx', x);
    outer.setAttribute('cy', y);
    outer.setAttribute('r', 8);
    outer.setAttribute('fill', 'none');
    outer.setAttribute('stroke', color);
    outer.setAttribute('stroke-width', '1.5');
    outer.setAttribute('opacity', '0.4');
    g.appendChild(outer);

    const inner = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    inner.setAttribute('cx', x);
    inner.setAttribute('cy', y);
    inner.setAttribute('r', 4);
    inner.setAttribute('fill', color);
    inner.setAttribute('stroke', '#0b1017');
    inner.setAttribute('stroke-width', '1.5');
    g.appendChild(inner);
  } else {
    // Triangle for last position
    const tri = document.createElementNS('http://www.w3.org/2000/svg', 'polygon');
    const s = 6;
    tri.setAttribute('points',
      `${x},${y - s} ${x - s * 0.866},${y + s * 0.5} ${x + s * 0.866},${y + s * 0.5}`
    );
    tri.setAttribute('fill', color);
    tri.setAttribute('stroke', '#0b1017');
    tri.setAttribute('stroke-width', '1');
    g.appendChild(tri);
  }

  return g;
}

export function renderMap(container, store) {
  const s = store.state;

  container.innerHTML = `
    <h2 class="panel-title">GPS Flight Map</h2>
    <svg class="map-svg" id="map-svg"></svg>`;

  const svg = container.querySelector('#map-svg');

  function render() {
    const packets = s.packets;
    const count = packets.length;
    if (count === 0) {
      svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="12">Awaiting GPS fix</text>';
      return;
    }

    const w = svg.clientWidth || 530;
    const h = svg.clientHeight || 280;
    svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
    svg.innerHTML = '';

    // Background
    const bg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    bg.setAttribute('width', w);
    bg.setAttribute('height', h);
    bg.setAttribute('fill', '#0f151d');
    svg.appendChild(bg);

    // Get last packet for center
    const last = packets.last(1)[0];
    if (!last || !last.lat || !last.lon) {
      svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="12">No GPS fix yet</text>';
      return;
    }

    // Center on current position
    const centerLat = last.lat;
    const centerLon = last.lon;

    // Grid lines
    const grid = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    const latStep = VIEW_DEG_LAT / 4; // every ~5km
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
      label.textContent = `${lat.toFixed(3)}°`;
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
      svg.appendChild(drawMarker(svg, lp.x, lp.y, 'launch', '#34d399'));
    }

    // Current position
    const cp = project(last.lat, last.lon, centerLat, centerLon, w, h);
    svg.appendChild(drawMarker(svg, cp.x, cp.y, 'current', '#38bdf8'));

    // Crosshair at center
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
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
