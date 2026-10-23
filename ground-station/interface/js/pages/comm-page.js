// Communication page — RSSI, SNR, delivery %, ACK diagnostics, packet table, signal graphs.

import { store } from '../store.js';

const GRAPH_POINTS = 300;

function drawLineGraph(svg, data, yMin, yMax, yFormat, color) {
  const w = svg.clientWidth || 500;
  const h = svg.clientHeight || 150;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  if (data.length < 2) {
    svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">No data</text>';
    return;
  }

  const slice = data.slice(-GRAPH_POINTS);
  const pad = { l: 48, r: 12, t: 6, b: 24 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;

  const points = slice.map((d, i) => {
    const x = pad.l + (i / (GRAPH_POINTS - 1)) * pw;
    const v = Math.max(yMin, Math.min(yMax, d.v));
    const y = pad.t + ph - ((v - yMin) / (yMax - yMin)) * ph;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  // Grid
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
    text.textContent = yFormat(val);
    grid.appendChild(text);
  }
  svg.appendChild(grid);

  // Data line
  const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  polyline.setAttribute('points', points.join(' '));
  polyline.setAttribute('fill', 'none');
  polyline.setAttribute('stroke', color);
  polyline.setAttribute('stroke-width', '1.2');
  polyline.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(polyline);
}

export function renderCommPage(root) {
  const s = store.state;

  root.innerHTML = `
    <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:var(--gap);margin-bottom:var(--gap);">
      <div class="comm-metric">
        <div class="comm-metric-value accent-text" id="comm-rssi">—</div>
        <div class="comm-metric-label">RSSI (dBm)</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value accent-text" id="comm-snr">—</div>
        <div class="comm-metric-label">SNR (dB)</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value ok-text" id="comm-delivery">—</div>
        <div class="comm-metric-label">DELIVERY %</div>
      </div>
      <div class="comm-metric">
        <div class="comm-metric-value ok-text" id="comm-ack">—</div>
        <div class="comm-metric-label">ACK %</div>
      </div>
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:var(--gap);margin-bottom:var(--gap);">
      <div class="panel">
        <h2 class="panel-title">Signal Strength Over Time</h2>
        <svg id="comm-rssi-graph" style="width:100%;height:150px;"></svg>
      </div>
      <div class="panel">
        <h2 class="panel-title">SNR Over Time</h2>
        <svg id="comm-snr-graph" style="width:100%;height:150px;"></svg>
      </div>
    </div>
    <div class="panel" style="margin-top:var(--gap);">
      <h2 class="panel-title">Packet History</h2>
      <div class="table-wrap" style="max-height:300px;">
        <table class="grid">
          <thead>
            <tr><th>Seq</th><th>Time</th><th>Type</th><th>Valid</th><th>RSSI</th><th>SNR</th><th>ACK</th></tr>
          </thead>
          <tbody id="comm-packet-body"></tbody>
        </table>
      </div>
    </div>`;

  const rssEl = root.querySelector('#comm-rssi');
  const snrEl = root.querySelector('#comm-snr');
  const delEl = root.querySelector('#comm-delivery');
  const ackEl = root.querySelector('#comm-ack');
  const rssiGraph = root.querySelector('#comm-rssi-graph');
  const snrGraph = root.querySelector('#comm-snr-graph');
  const packetBody = root.querySelector('#comm-packet-body');

  function render() {
    const last = s.packets.last(1)[0];

    if (last && last.rssi != null) {
      rssEl.textContent = `${last.rssi}`;
      rssEl.className = `comm-metric-value ${last.rssi < -100 ? 'fault-text' : last.rssi < -90 ? 'warn-text' : 'accent-text'}`;
    } else {
      rssEl.textContent = '—';
    }

    if (last && last.snr != null) {
      snrEl.textContent = `${last.snr.toFixed(1)}`;
      snrEl.className = `comm-metric-value ${last.snr < 3 ? 'fault-text' : last.snr < 5 ? 'warn-text' : 'accent-text'}`;
    } else {
      snrEl.textContent = '—';
    }

    if (s.lastSeq > 0) {
      const pct = ((s.stats.rx / s.lastSeq) * 100);
      delEl.textContent = `${pct.toFixed(1)}%`;
      delEl.className = `comm-metric-value ${pct < 90 ? 'fault-text' : pct < 95 ? 'warn-text' : 'ok-text'}`;
    } else {
      delEl.textContent = '—';
    }

    const ackTotal = s.stats.acked + s.stats.ackMissed;
    if (ackTotal > 0) {
      const pct = (s.stats.acked / ackTotal) * 100;
      ackEl.textContent = `${pct.toFixed(1)}%`;
      ackEl.className = `comm-metric-value ${pct < 90 ? 'fault-text' : pct < 95 ? 'warn-text' : 'ok-text'}`;
    } else {
      ackEl.textContent = '—';
    }

    // Graphs
    const rssiData = s.stats.rssi.last(GRAPH_POINTS);
    if (rssiData.length > 1) {
      drawLineGraph(rssiGraph, rssiData.map(d => ({ v: d.v })), -120, -60, v => `${v.toFixed(0)}`, '#38bdf8');
    } else {
      rssiGraph.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Awaiting signal data</text>';
    }

    const snrData = s.stats.snr.last(GRAPH_POINTS);
    if (snrData.length > 1) {
      drawLineGraph(snrGraph, snrData.map(d => ({ v: d.v })), 0, 12, v => `${v.toFixed(0)}`, '#34d399');
    } else {
      snrGraph.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Awaiting signal data</text>';
    }

    // Packet table
    const count = Math.min(100, s.packets.length);
    if (count === 0) {
      packetBody.innerHTML = '<tr><td colspan="7" style="text-align:center;color:var(--muted)">Awaiting packets</td></tr>';
      return;
    }

    const rows = [];
    for (let i = 0; i < count; i++) {
      const idx = s.packets.length - count + i;
      const p = s.packets.get(idx);
      if (!p) continue;
      const ackText = p.ack === 'acked' ? '<span class="ok-text">acked</span>' : p.ack === 'missed' ? '<span class="fault-text">missed</span>' : '<span style="color:var(--muted)">pending</span>';
      rows.push(
        `<tr>` +
        `<td>${p.seq}</td>` +
        `<td>${new Date(p.rx_ts).toISOString().slice(11, 19)}</td>` +
        `<td>${p.pkt_type || 'telemetry'}</td>` +
        `<td class="${p.valid ? 'ok-text' : 'fault-text'}">${p.valid ? 'OK' : 'BAD'}</td>` +
        `<td>${p.rssi != null ? p.rssi : '—'}</td>` +
        `<td>${p.snr != null ? p.snr.toFixed(1) : '—'}</td>` +
        `<td>${ackText}</td>` +
        `</tr>`
      );
    }
    packetBody.innerHTML = rows.join('');
  }

  const unsub = store.on('telemetry', render);
  const unsubAck = store.on('ack', render);
  render();
  return () => { unsub(); unsubAck(); };
}
