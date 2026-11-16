// Communication health panel — RSSI, SNR, delivery %, ACK rate + signal graph.

import { store } from '../store.js';

const GRAPH_POINTS = 200;

function drawLineGraph(svg, data, yMin, yMax, yLabel, color) {
  const w = svg.clientWidth || 400;
  const h = svg.clientHeight || 100;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  if (data.length < 2) {
    svg.innerHTML = `<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">No data</text>`;
    return;
  }

  const pad = { l: 36, r: 8, t: 6, b: 4 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  const slice = data.slice(-GRAPH_POINTS);
  const points = slice.map((d, i) => {
    const x = pad.l + (i / (GRAPH_POINTS - 1)) * pw;
    const v = Math.max(yMin, Math.min(yMax, d.v));
    const y = pad.t + ph - ((v - yMin) / (yMax - yMin)) * ph;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  // Grid lines
  const grid = document.createElementNS('http://www.w3.org/2000/svg', 'g');
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + (ph / 4) * i;
    const val = yMax - ((yMax - yMin) / 4) * i;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', pad.l);
    line.setAttribute('x2', w - pad.r);
    line.setAttribute('y1', y);
    line.setAttribute('y2', y);
    line.setAttribute('stroke', '#232d3b');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', pad.l - 4);
    text.setAttribute('y', y + 3);
    text.setAttribute('text-anchor', 'end');
    text.setAttribute('fill', '#7b8a9d');
    text.setAttribute('font-size', '8');
    text.setAttribute('font-family', 'var(--mono)');
    text.textContent = val.toFixed(0);
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
}

export function renderComm(container, store) {
  const s = store.state;

  container.innerHTML = `
    <h2 class="panel-title">Communication Health</h2>
    <div class="comm-metrics">
      <div class="comm-metric">
        <div class="comm-metric-value accent-text" id="comm-rssi">—</div>
        <div class="comm-metric-label">RSSI</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value accent-text" id="comm-snr">—</div>
        <div class="comm-metric-label">SNR</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value ok-text" id="comm-delivery">—</div>
        <div class="comm-metric-label">DELIVERY</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value ok-text" id="comm-ack">—</div>
        <div class="comm-metric-label">ACK</div>
      </div>
    </div>
    <svg class="comm-graph" id="comm-graph"></svg>`;

  const rssEl = container.querySelector('#comm-rssi');
  const snrEl = container.querySelector('#comm-snr');
  const delEl = container.querySelector('#comm-delivery');
  const ackEl = container.querySelector('#comm-ack');
  const graph = container.querySelector('#comm-graph');

  function render() {
    const last = s.packets.last(1)[0];

    // RSSI
    if (last && last.rssi != null) {
      rssEl.textContent = `${last.rssi} dBm`;
      rssEl.className = `comm-metric-value ${last.rssi < -100 ? 'fault-text' : last.rssi < -90 ? 'warn-text' : 'accent-text'}`;
    } else {
      rssEl.textContent = '—';
    }

    // SNR
    if (last && last.snr != null) {
      snrEl.textContent = `${last.snr} dB`;
      snrEl.className = `comm-metric-value ${last.snr < 3 ? 'fault-text' : last.snr < 5 ? 'warn-text' : 'accent-text'}`;
    } else {
      snrEl.textContent = '—';
    }

    // Delivery %
    if (s.lastSeq > 0) {
      const pct = ((s.stats.rx / s.lastSeq) * 100);
      delEl.textContent = `${pct.toFixed(1)}%`;
      delEl.className = `comm-metric-value ${pct < 90 ? 'fault-text' : pct < 95 ? 'warn-text' : 'ok-text'}`;
    } else {
      delEl.textContent = '—';
    }

    // ACK rate
    const ackTotal = s.stats.acked + s.stats.ackMissed;
    if (ackTotal > 0) {
      const pct = (s.stats.acked / ackTotal) * 100;
      ackEl.textContent = `${pct.toFixed(1)}%`;
      ackEl.className = `comm-metric-value ${pct < 90 ? 'fault-text' : pct < 95 ? 'warn-text' : 'ok-text'}`;
    } else {
      ackEl.textContent = '—';
    }

    // RSSI graph
    const rssiData = s.stats.rssi.last(GRAPH_POINTS);
    if (rssiData.length > 1) {
      // RSSI ranges from ~-120 to -60
      drawLineGraph(graph, rssiData.map(d => ({ v: d.v })), -120, -60, 'dBm', '#38bdf8');
    } else {
      graph.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Awaiting signal data</text>';
    }
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
