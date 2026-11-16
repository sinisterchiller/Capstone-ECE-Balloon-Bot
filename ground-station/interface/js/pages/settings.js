// Settings page — connection config, dashboard preferences, system configuration.

import { store, saveSettings } from '../store.js';

export function renderSettings(root) {
  const s = store.state;
  const st = s.settings;

  root.innerHTML = `
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:var(--gap);">
      <div class="panel">
        <h2 class="panel-title">Connection</h2>
        <div style="display:flex;flex-direction:column;gap:12px;">
          <div>
            <label style="display:block;font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.1em;margin-bottom:4px;">WebSocket URL</label>
            <input type="text" id="set-ws-url" value="${st.wsUrl}" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:6px 10px;font-size:13px;font-family:var(--mono);">
          </div>
          <div style="display:flex;gap:8px;">
            <button id="set-connect" style="background:var(--ok);color:#0b1017;border:none;border-radius:4px;padding:6px 16px;font-size:12px;font-weight:600;cursor:pointer;font-family:var(--sans);">Connect</button>
            <button id="set-disconnect" style="background:var(--panel);color:var(--text);border:1px solid var(--border);border-radius:4px;padding:6px 16px;font-size:12px;cursor:pointer;font-family:var(--sans);">Disconnect</button>
            <button id="set-sim" style="background:var(--accent);color:#0b1017;border:none;border-radius:4px;padding:6px 16px;font-size:12px;font-weight:600;cursor:pointer;font-family:var(--sans);">Start Simulator</button>
          </div>
          <div style="font-size:12px;">
            <span style="color:var(--muted);">Source:</span>
            <span class="mono" id="set-source" style="margin-left:4px;">${s.source}</span>
          </div>
          <div style="font-size:12px;">
            <span style="color:var(--muted);">WS State:</span>
            <span class="mono" id="set-ws-state" style="margin-left:4px;">${s.wsState}</span>
          </div>
        </div>
      </div>
      <div class="panel">
        <h2 class="panel-title">Dashboard</h2>
        <div style="display:flex;flex-direction:column;gap:12px;">
          <div>
            <label style="display:block;font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.1em;margin-bottom:4px;">Mission Name</label>
            <input type="text" id="set-mission" value="${st.missionName}" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:6px 10px;font-size:13px;">
          </div>
          <div style="display:flex;gap:8px;">
            <div style="flex:1;">
              <label style="display:block;font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.1em;margin-bottom:4px;">Stale Threshold (s)</label>
              <input type="number" id="set-stale" value="${st.staleSec}" min="5" max="120" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:6px 10px;font-size:13px;font-family:var(--mono);">
            </div>
            <div style="flex:1;">
              <label style="display:block;font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.1em;margin-bottom:4px;">Unit System</label>
              <select id="set-unit" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:6px 10px;font-size:13px;">
                <option value="metric" ${st.unitSystem === 'metric' ? 'selected' : ''}>Metric</option>
                <option value="imperial" ${st.unitSystem === 'imperial' ? 'selected' : ''}>Imperial</option>
              </select>
            </div>
          </div>
        </div>
      </div>
      <div class="panel">
        <h2 class="panel-title">Simulator</h2>
        <div style="display:flex;flex-direction:column;gap:12px;">
          <div style="display:flex;align-items:center;gap:8px;">
            <input type="checkbox" id="set-autosim" ${st.autoSim ? 'checked' : ''} style="width:auto;">
            <label for="set-autosim" style="font-size:12px;cursor:pointer;">Auto-start simulator</label>
          </div>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Interval (ms)</label>
              <input type="number" id="set-sim-interval" value="${st.simIntervalMs}" min="100" max="5000" step="100" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Time Scale</label>
              <input type="number" id="set-sim-timescale" value="${st.simTimeScale}" min="1" max="10000" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Loss Rate</label>
              <input type="number" id="set-sim-loss" value="${st.simLossRate}" min="0" max="1" step="0.01" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Seed</label>
              <input type="number" id="set-sim-seed" value="${st.simSeed}" min="0" max="999999" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
          </div>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Launch Lat</label>
              <input type="number" id="set-launch-lat" value="${st.launchLat}" step="0.001" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
            <div>
              <label style="display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;margin-bottom:2px;">Launch Lon</label>
              <input type="number" id="set-launch-lon" value="${st.launchLon}" step="0.001" style="width:100%;background:var(--panel-2);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
            </div>
          </div>
        </div>
      </div>
      <div class="panel">
        <h2 class="panel-title">System</h2>
        <div style="display:flex;flex-direction:column;gap:12px;">
          <div style="font-size:12px;">
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span style="color:var(--muted);">Schema Version</span>
              <span class="mono">1</span>
            </div>
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span style="color:var(--muted);">Packet Buffer</span>
              <span class="mono">${s.packets.length} / 10,000</span>
            </div>
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span style="color:var(--muted);">Event Buffer</span>
              <span class="mono">${s.events.length} / 2,000</span>
            </div>
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span style="color:var(--muted);">Bad Frames</span>
              <span class="mono fault-text">${s.stats.badFrames}</span>
            </div>
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span style="color:var(--muted);">Gaps Detected</span>
              <span class="mono warn-text">${s.stats.gaps}</span>
            </div>
            <div style="display:flex;justify-content:space-between;">
              <span style="color:var(--muted);">Packets Received</span>
              <span class="mono ok-text">${s.stats.rx}</span>
            </div>
          </div>
          <div style="display:flex;gap:8px;">
            <button id="set-reset-settings" style="background:var(--panel);color:var(--text);border:1px solid var(--border);border-radius:4px;padding:6px 16px;font-size:12px;cursor:pointer;font-family:var(--sans);">Reset Settings</button>
          </div>
        </div>
      </div>
    </div>`;

  // Wire up controls
  const wsUrlInput = root.querySelector('#set-ws-url');
  const connectBtn = root.querySelector('#set-connect');
  const disconnectBtn = root.querySelector('#set-disconnect');
  const simBtn = root.querySelector('#set-sim');
  const sourceEl = root.querySelector('#set-source');
  const wsStateEl = root.querySelector('#set-ws-state');

  function updateStatus() {
    sourceEl.textContent = s.source;
    wsStateEl.textContent = s.wsState;
    sourceEl.className = `mono ${s.source === 'ws' ? 'ok-text' : s.source === 'simulator' ? 'accent-text' : 'fault-text'}`;
    wsStateEl.className = `mono ${s.wsState === 'open' ? 'ok-text' : s.wsState === 'connecting' ? 'warn-text' : 'fault-text'}`;
  }

  const unsubLink = store.on('link-state', updateStatus);

  connectBtn.addEventListener('click', () => store.emit('connect-ws', wsUrlInput.value));

  disconnectBtn.addEventListener('click', () => {
    // Trigger disconnect via simulator stop
    store.state.settings.autoSim = false;
    store.emit('disconnect');
  });

  simBtn.addEventListener('click', () => {
    store.state.settings.autoSim = true;
    store.emit('start-sim');
  });

  // Save settings on change
  const saveBtn = () => {
    const newSettings = {
      wsUrl: wsUrlInput.value,
      missionName: root.querySelector('#set-mission').value,
      staleSec: parseInt(root.querySelector('#set-stale').value) || 15,
      unitSystem: root.querySelector('#set-unit').value,
      autoSim: root.querySelector('#set-autosim').checked,
      simIntervalMs: parseInt(root.querySelector('#set-sim-interval').value) || 1000,
      simTimeScale: parseInt(root.querySelector('#set-sim-timescale').value) || 1000,
      simLossRate: parseFloat(root.querySelector('#set-sim-loss').value) || 0.02,
      simSeed: parseInt(root.querySelector('#set-sim-seed').value) || 42,
      launchLat: parseFloat(root.querySelector('#set-launch-lat').value) || 51.078,
      launchLon: parseFloat(root.querySelector('#set-launch-lon').value) || -114.123,
    };
    Object.assign(s.settings, newSettings);
    saveSettings(s.settings);
  };

  [wsUrlInput, root.querySelector('#set-mission'), root.querySelector('#set-stale'),
   root.querySelector('#set-unit'), root.querySelector('#set-autosim'),
   root.querySelector('#set-sim-interval'), root.querySelector('#set-sim-timescale'),
   root.querySelector('#set-sim-loss'), root.querySelector('#set-sim-seed'),
   root.querySelector('#set-launch-lat'), root.querySelector('#set-launch-lon')].forEach(el => {
    el.addEventListener('change', saveBtn);
  });

  root.querySelector('#set-reset-settings').addEventListener('click', () => {
    localStorage.removeItem('bb.settings.v1');
    location.reload();
  });

  updateStatus();
  return () => unsubLink();
}
