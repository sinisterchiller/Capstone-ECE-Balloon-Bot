// Event Analysis page — event timeline, details, graph, and mode history.

import { store } from '../store.js';
import { fmtDuration, fmtTemp, fmtPress } from '../format.js';

function shortenCategory(cat) {
  const map = {
    mode_change: 'MODE',
    threshold: 'THRESH',
    gnss: 'GNSS',
    battery: 'BATT',
    link: 'LINK',
    system: 'SYS',
  };
  return map[cat] || cat;
}

function severityDot(severity) {
  const colors = { info: 'var(--ok)', warning: 'var(--warn)', critical: 'var(--fault)' };
  return `<span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:${colors[severity] || 'var(--muted)'};margin-right:6px;flex-shrink:0;"></span>`;
}

function drawEventGraph(svg, packets, eventTs, windowSec = 120) {
  const w = svg.clientWidth || 600;
  const h = svg.clientHeight || 200;
  svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
  svg.innerHTML = '';

  // Collect all packets with temperature data
  const relevant = [];
  for (let i = 0; i < packets.length; i++) {
    const p = packets[i];
    if (p && p.temp_ext != null && Number.isFinite(p.temp_ext)) {
      relevant.push(p);
    }
  }

  if (relevant.length < 2) {
    svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#7b8a9d" font-size="11">Insufficient data</text>';
    return;
  }

  // Find the event's relative position in the data
  let eventXIndex = -1;
  if (eventTs && Number.isFinite(eventTs)) {
    let minDist = Infinity;
    for (let i = 0; i < relevant.length; i++) {
      const d = Math.abs(relevant[i].rx_ts - eventTs);
      if (d < minDist) { minDist = d; eventXIndex = i; }
    }
  }

  let yMin = Infinity, yMax = -Infinity;
  for (const p of relevant) {
    yMin = Math.min(yMin, p.temp_ext);
    yMax = Math.max(yMax, p.temp_ext);
  }

  const pad = { l: 52, r: 16, t: 10, b: 32 };
  const pw = w - pad.l - pad.r;
  const ph = h - pad.t - pad.b;
  const yRange = yMax - yMin || 1;
  const fMin = yMin - yRange * 0.1;
  const fMax = yMax + yRange * 0.1;

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
    text.textContent = val.toFixed(0) + '°';
    grid.appendChild(text);
  }

  // X-axis labels
  const xLabelCount = 4;
  for (let i = 0; i <= xLabelCount; i++) {
    const idx = Math.floor((i / xLabelCount) * (relevant.length - 1));
    const p = relevant[idx];
    if (!p) continue;
    const x = pad.l + ((i / xLabelCount)) * pw;
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', x);
    line.setAttribute('x2', x);
    line.setAttribute('y1', pad.t);
    line.setAttribute('y2', pad.t + ph);
    line.setAttribute('stroke', '#1a2332');
    line.setAttribute('stroke-width', '0.5');
    grid.appendChild(line);

    const d = new Date(p.rx_ts);
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

  // Event trigger marker
  if (eventXIndex >= 0) {
    const ex = pad.l + (eventXIndex / (relevant.length - 1)) * pw;
    const eventLine = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    eventLine.setAttribute('x1', ex);
    eventLine.setAttribute('x2', ex);
    eventLine.setAttribute('y1', pad.t);
    eventLine.setAttribute('y2', pad.t + ph);
    eventLine.setAttribute('stroke', '#f87171');
    eventLine.setAttribute('stroke-width', '1');
    eventLine.setAttribute('stroke-dasharray', '4,3');
    svg.appendChild(eventLine);

    const eventLabel = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    eventLabel.setAttribute('x', ex);
    eventLabel.setAttribute('y', pad.t - 4);
    eventLabel.setAttribute('text-anchor', 'middle');
    eventLabel.setAttribute('fill', '#f87171');
    eventLabel.setAttribute('font-size', '8');
    eventLabel.setAttribute('font-family', 'var(--mono)');
    eventLabel.textContent = 'EVENT';
    svg.appendChild(eventLabel);
  }

  // Data line
  const points = relevant.map((p, idx) => {
    const x = pad.l + (idx / (relevant.length - 1)) * pw;
    const y = pad.t + ph - ((p.temp_ext - fMin) / (fMax - fMin)) * ph;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });

  const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  polyline.setAttribute('points', points.join(' '));
  polyline.setAttribute('fill', 'none');
  polyline.setAttribute('stroke', '#38bdf8');
  polyline.setAttribute('stroke-width', '1.5');
  polyline.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(polyline);
}

export function renderEvents(root) {
  const s = store.state;

  root.innerHTML = `
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:var(--gap);margin-bottom:var(--gap);">
      <div class="panel" id="evt-timeline-panel">
        <h2 class="panel-title">Event Timeline</h2>
        <div class="event-list" id="evt-timeline" style="max-height:400px;overflow-y:auto;"></div>
      </div>
      <div class="panel" id="evt-details-panel">
        <h2 class="panel-title">Event Details</h2>
        <div id="evt-details" style="min-height:350px;display:flex;align-items:center;justify-content:center;">
          <p style="color:var(--muted);text-align:center;">Select an event to view details</p>
        </div>
      </div>
    </div>
    <div class="panel" id="evt-graph-panel">
      <h2 class="panel-title">Sensor Context — Temperature Around Event</h2>
      <svg id="evt-graph" style="width:100%;height:200px;"></svg>
    </div>
    <div class="panel" id="evt-mode-panel" style="margin-top:var(--gap);">
      <h2 class="panel-title">Mode History</h2>
      <div id="evt-mode-timeline" style="max-height:150px;overflow-y:auto;"></div>
    </div>`;

  const timelineEl = root.querySelector('#evt-timeline');
  const detailsEl = root.querySelector('#evt-details');
  const graphSvg = root.querySelector('#evt-graph');
  const modeTimelineEl = root.querySelector('#evt-mode-timeline');

  let selectedEvent = null;

  function renderTimeline() {
    const events = s.events;
    const count = events.length;
    if (count === 0) {
      timelineEl.innerHTML = '<p style="padding:20px;color:var(--muted);text-align:center;">No events recorded</p>';
      return;
    }

    const items = [];
    for (let i = count - 1; i >= 0; i--) {
      const e = events.get(i);
      if (!e) continue;
      const elapsed = s.session.launchTs ? fmtDuration(e.ts - s.session.launchTs) : fmtDuration(e.ts);
      const isSelected = selectedEvent && selectedEvent.id === e.id;
      items.push(
        `<div class="event-item ${e.severity} ${isSelected ? 'selected' : ''}" data-id="${e.id}" style="cursor:pointer;${isSelected ? 'background:rgba(56,189,248,0.08);border-left-width:4px;' : ''}">` +
          `${severityDot(e.severity)}` +
          `<div class="event-body">` +
            `<div class="event-meta">` +
              `<span class="event-time">${elapsed}</span>` +
              `<span class="event-type">${shortenCategory(e.category)}</span>` +
              `<span style="color:var(--muted);font-size:10px;">${e.code || ''}</span>` +
            `</div>` +
            `<div class="event-msg">${e.message}</div>` +
          `</div>` +
        `</div>`
      );
    }
    timelineEl.innerHTML = items.join('');

    // Click handlers
    timelineEl.querySelectorAll('.event-item').forEach(el => {
      el.addEventListener('click', () => {
        const evt = s.events.items.find(e => e.id === el.dataset.id);
        if (evt) {
          selectedEvent = evt;
          renderTimeline();
          renderDetails(evt);
          renderGraph(evt);
        }
      });
    });
  }

  function renderDetails(evt) {
    const s = store.state;
    const elapsed = s.session.launchTs ? fmtDuration(evt.ts - s.session.launchTs) : fmtDuration(evt.ts);

    // Find nearby telemetry
    const nearby = [];
    for (let i = 0; i < s.packets.length; i++) {
      const p = s.packets.get(i);
      if (p && Math.abs(p.rx_ts - evt.ts) < 10000) {
        nearby.push(p);
      }
    }

    detailsEl.innerHTML = `
      <div style="margin-bottom:12px;">
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Event ID</span>
          <span class="mono" style="font-size:13px;">${evt.id}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Time</span>
          <span class="mono" style="font-size:13px;">${elapsed}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Category</span>
          <span class="mono" style="font-size:13px;">${shortenCategory(evt.category)}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Severity</span>
          <span class="mono" style="font-size:13px;color:${evt.severity === 'critical' ? 'var(--fault)' : evt.severity === 'warning' ? 'var(--warn)' : 'var(--ok)'};">${evt.severity.toUpperCase()}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Trigger</span>
          <span class="mono" style="font-size:13px;">${evt.measurement || '—'}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Value / Threshold</span>
          <span class="mono" style="font-size:13px;">${evt.value != null ? evt.value.toFixed(2) : '—'} / ${evt.threshold != null ? evt.threshold.toFixed(2) : '—'} ${evt.unit || ''}</span>
        </div>
        <div style="display:flex;justify-content:space-between;margin-bottom:6px;">
          <span style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;">Mode</span>
          <span class="mono" style="font-size:13px;">${evt.mode || '—'}</span>
        </div>
        <div style="margin-top:10px;">
          <div style="color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:0.1em;margin-bottom:4px;">Nearby Sensor Readings</div>
          ${nearby.length > 0 ? nearby.map(p =>
            `<div style="display:flex;justify-content:space-between;font-size:12px;margin-bottom:2px;">` +
              `<span class="mono" style="color:var(--muted);">${new Date(p.rx_ts).toISOString().slice(11, 19)}</span>` +
              `<span class="mono">T: ${p.temp_ext != null ? p.temp_ext.toFixed(1) + '°C' : '—'} | P: ${p.pressure != null ? p.pressure.toFixed(1) + ' hPa' : '—'}</span>` +
            `</div>`
          ).join('') : '<div style="font-size:12px;color:var(--muted);">No nearby readings</div>'}
        </div>`;
  }

  function renderGraph(evt) {
    drawEventGraph(graphSvg, s.packets.items, evt.ts, 120);
  }

  function renderModeHistory() {
    const packets = s.packets;
    const modes = [];
    let lastMode = null;

    for (let i = 0; i < packets.length; i++) {
      const p = packets.get(i);
      if (p && (!lastMode || p.mode !== lastMode)) {
        modes.push({ mode: p.mode, ts: p.rx_ts });
        lastMode = p.mode;
      }
    }

    if (modes.length === 0) {
      modeTimelineEl.innerHTML = '<p style="padding:12px;color:var(--muted);text-align:center;">No mode transitions yet</p>';
      return;
    }

    const modeColors = { NORMAL: 'var(--ok)', EVENT: 'var(--warn)', LOW_POWER: 'var(--accent)' };

    let html = '<div style="display:flex;flex-direction:column;gap:4px;">';
    for (let i = 0; i < modes.length; i++) {
      const m = modes[i];
      const elapsed = s.session.launchTs ? fmtDuration(m.ts - s.session.launchTs) : fmtDuration(m.ts);
      html += `<div style="display:flex;align-items:center;gap:8px;padding:4px 8px;background:var(--panel-2);border-radius:4px;font-size:12px;">` +
        `<span class="mono" style="color:var(--muted);min-width:60px;">${elapsed}</span>` +
        `<span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:${modeColors[m.mode] || 'var(--muted)'};"></span>` +
        `<span class="mono" style="color:${modeColors[m.mode] || 'var(--text)'};font-weight:600;">${m.mode}</span>` +
        (i > 0 ? `<span style="color:var(--muted);margin-left:auto;">${modes[i - 1].mode} → ${m.mode}</span>` : '') +
      `</div>`;
    }
    html += '</div>';
    modeTimelineEl.innerHTML = html;
  }

  function render() {
    renderTimeline();
    renderModeHistory();
    if (selectedEvent) {
      // Re-render details and graph if event still exists
      const evt = s.events.items.find(e => e.id === selectedEvent.id);
      if (evt) {
        renderDetails(evt);
        renderGraph(evt);
      } else {
        selectedEvent = null;
      }
    } else if (s.events.length > 0) {
      // Auto-select first event for initial view (useful for dashboards)
      const first = s.events.get(0);
      if (first) {
        selectedEvent = first;
        renderTimeline();
        renderDetails(first);
        renderGraph(first);
      }
    }
  }

  const unsub = store.on('event', render);
  const unsubHello = store.on('hello', () => { selectedEvent = null; render(); });
  render();
  return () => { unsub(); unsubHello(); };
}
