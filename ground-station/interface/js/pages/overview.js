// Overview page — six telemetry cards + GPS map, atmospheric charts,
// communication health, event monitor, and telemetry log.

import { store } from '../store.js';
import { fmtAlt, fmtTemp, fmtPress, fmtBattery, fmtVSpeed } from '../format.js';
import { renderMap } from './overview-map.js';
import { renderCharts } from './overview-charts.js';
import { renderComm } from './overview-comm.js';
import { renderEvents } from './overview-events.js';
import { renderLog } from './overview-log.js';

const AGO_MS = 60000; // trend compares against ~60 s ago

function trendArrow(key, packets, refTs) {
  let old = null;
  for (let i = packets.length - 1; i >= 0; i--) {
    const p = packets.get(i);
    if (p.rx_ts <= refTs - AGO_MS) { old = p[key]; break; }
  }
  return old;
}

export function renderOverview(root) {
  const s = store.state;

  root.innerHTML = `
    <section class="cards" aria-label="Telemetry cards">
      <div class="card">
        <div class="card-label">Altitude</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-alt">—</span>
          <span class="card-unit" id="ov-alt-unit"></span>
          <span class="card-trend" id="ov-alt-trend"></span>
        </div>
      </div>
      <div class="card">
        <div class="card-label">Temperature</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-temp">—</span>
          <span class="card-unit" id="ov-temp-unit"></span>
          <span class="card-trend" id="ov-temp-trend"></span>
        </div>
      </div>
      <div class="card">
        <div class="card-label">Pressure</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-press">—</span>
          <span class="card-unit">hPa</span>
          <span class="card-trend" id="ov-press-trend"></span>
        </div>
      </div>
      <div class="card">
        <div class="card-label">Battery</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-batt">—</span>
          <span class="card-unit" id="ov-batt-unit"></span>
          <span class="card-trend" id="ov-batt-trend"></span>
        </div>
      </div>
      <div class="card">
        <div class="card-label">Vertical Speed</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-vspeed">—</span>
          <span class="card-unit" id="ov-vspeed-unit"></span>
          <span class="card-trend" id="ov-vspeed-trend"></span>
        </div>
      </div>
      <div class="card">
        <div class="card-label">Payload Mode</div>
        <div class="card-value live-value">
          <span class="mono" id="ov-mode">—</span>
        </div>
      </div>
    </section>
    <section class="overview-main">
      <div class="panel map-panel" id="ov-map">
        <h2 class="panel-title">GPS Flight Map</h2>
        <svg class="map-svg" id="map-svg"></svg>
      </div>
      <div class="panel charts-panel" id="ov-charts">
        <h2 class="panel-title">Atmospheric Trends</h2>
        <div class="chart-row">
          <div class="chart-label"><span>Temperature</span><span class="chart-val" id="cv-temp_ext">—</span></div>
          <svg class="chart-svg" id="svg-temp_ext"></svg>
        </div>
        <div class="chart-row">
          <div class="chart-label"><span>Pressure</span><span class="chart-val" id="cv-pressure">—</span></div>
          <svg class="chart-svg" id="svg-pressure"></svg>
        </div>
        <div class="chart-row">
          <div class="chart-label"><span>Altitude</span><span class="chart-val" id="cv-alt">—</span></div>
          <svg class="chart-svg" id="svg-alt"></svg>
        </div>
        <div class="chart-row">
          <div class="chart-label"><span>Vertical Speed</span><span class="chart-val" id="cv-vspeed">—</span></div>
          <svg class="chart-svg" id="svg-vspeed"></svg>
        </div>
      </div>
    </section>
    <section class="overview-bottom">
      <div class="panel comm-panel" id="ov-comm">
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
        <svg class="comm-graph" id="comm-graph"></svg>
      </div>
      <div class="panel event-panel" id="ov-events">
        <h2 class="panel-title">Event Monitor</h2>
        <div class="event-list" id="event-list"></div>
      </div>
    </section>
    <section class="panel log-panel" id="ov-log">
      <h2 class="panel-title">Recent Telemetry</h2>
      <div class="table-wrap log-table">
        <table class="grid">
          <thead>
            <tr>
              <th>Time</th><th>Seq</th><th>Mode</th><th>Alt</th><th>Temp</th><th>Status</th>
            </tr>
          </thead>
          <tbody id="log-body"></tbody>
        </table>
      </div>
    </section>`;

  // ---- Telemetry cards ----

  const els = {
    alt: root.querySelector('#ov-alt'),
    altUnit: root.querySelector('#ov-alt-unit'),
    altTrend: root.querySelector('#ov-alt-trend'),
    temp: root.querySelector('#ov-temp'),
    tempUnit: root.querySelector('#ov-temp-unit'),
    tempTrend: root.querySelector('#ov-temp-trend'),
    press: root.querySelector('#ov-press'),
    pressTrend: root.querySelector('#ov-press-trend'),
    batt: root.querySelector('#ov-batt'),
    battUnit: root.querySelector('#ov-batt-unit'),
    battTrend: root.querySelector('#ov-batt-trend'),
    vspeed: root.querySelector('#ov-vspeed'),
    vspeedUnit: root.querySelector('#ov-vspeed-unit'),
    vspeedTrend: root.querySelector('#ov-vspeed-trend'),
    mode: root.querySelector('#ov-mode'),
  };

  const setTrend = (el, cur, old) => {
    if (cur == null || old == null) { el.textContent = ''; return; }
    const d = cur - old;
    el.textContent = Math.abs(d) < 1e-9 ? '→' : (d > 0 ? '▲' : '▼');
  };

  const onTelemetry = p => {
    const u = s.settings.unitSystem;
    const pkts = s.packets;

    els.alt.textContent = p.alt == null ? '—' : Math.round(p.alt).toLocaleString();
    els.altUnit.textContent = u === 'imperial' ? 'ft' : 'm';
    setTrend(els.altTrend, p.alt, trendArrow('alt', pkts, p.rx_ts));

    els.temp.textContent = p.temp_ext == null ? '—' : p.temp_ext.toFixed(1);
    els.tempUnit.textContent = u === 'imperial' ? '°F' : '°C';
    setTrend(els.tempTrend, p.temp_ext, trendArrow('temp_ext', pkts, p.rx_ts));

    els.press.textContent = p.pressure == null ? '—' : p.pressure.toFixed(1);
    setTrend(els.pressTrend, p.pressure, trendArrow('pressure', pkts, p.rx_ts));

    els.batt.textContent = p.battery == null ? '—' : p.battery.toFixed(2);
    els.battUnit.textContent = 'V';
    setTrend(els.battTrend, p.battery, trendArrow('battery', pkts, p.rx_ts));

    els.vspeed.textContent = p.vspeed == null ? '—' : p.vspeed.toFixed(1);
    els.vspeedUnit.textContent = u === 'imperial' ? 'ft/s' : 'm/s';
    setTrend(els.vspeedTrend, p.vspeed, trendArrow('vspeed', pkts, p.rx_ts));

    els.mode.textContent = p.mode;
  };

  const unsubCards = store.on('telemetry', onTelemetry);
  onTelemetry(s.packets.last(1)[0] ?? { alt: null, temp_ext: null, pressure: null,
    battery: null, vspeed: null, mode: '—', rx_ts: 0 });

  // ---- Panel modules ----

  const mapClean = renderMap(root.querySelector('#ov-map'), store);
  const chartsClean = renderCharts(root.querySelector('#ov-charts'), store);
  const commClean = renderComm(root.querySelector('#ov-comm'), store);
  const eventsClean = renderEvents(root.querySelector('#ov-events'), store);
  const logClean = renderLog(root.querySelector('#ov-log'), store);

  return () => { unsubCards(); mapClean(); chartsClean(); commClean(); eventsClean(); logClean(); };
}
