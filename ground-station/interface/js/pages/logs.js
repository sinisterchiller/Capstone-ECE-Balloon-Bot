// Data Logs page — searchable telemetry table, session selection, CSV export.

import { store } from '../store.js';
import { parseSessionFile } from '../schema.js';

export function renderLogs(root) {
  const s = store.state;

  root.innerHTML = `
    <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;margin-bottom:var(--gap);">
      <h2 class="panel-title" style="margin:0;">Telemetry Logs</h2>
      <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
        <select id="log-session" style="background:var(--panel);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--sans);">
          <option value="live">Live Session</option>
        </select>
        <label style="font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;">Import session JSON</label>
        <input type="file" id="log-file" accept=".json,application/json" style="background:var(--panel);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--sans);">
        <input type="text" id="log-search" placeholder="Search..." style="background:var(--panel);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;width:150px;font-family:var(--sans);">
        <label style="font-size:10px;color:var(--muted);">From</label>
        <input type="datetime-local" id="log-from" style="background:var(--panel);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
        <label style="font-size:10px;color:var(--muted);">To</label>
        <input type="datetime-local" id="log-to" style="background:var(--panel);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:4px 8px;font-size:12px;font-family:var(--mono);">
        <button id="log-export" style="background:var(--accent);color:#0b1017;border:none;border-radius:4px;padding:4px 12px;font-size:12px;font-weight:600;cursor:pointer;font-family:var(--sans);">Export CSV</button>
      </div>
    </div>
    <div class="panel" style="margin-bottom:var(--gap);">
      <div style="display:flex;justify-content:space-between;font-size:12px;">
        <span style="color:var(--muted);">Showing <span id="log-count">0</span> of <span id="log-total">0</span> packets</span>
        <span style="color:var(--muted);" id="log-session-info">Live data</span>
      </div>
    </div>
    <div class="panel">
      <div class="table-wrap" style="max-height:calc(100vh - 280px);">
        <table class="grid">
          <thead>
            <tr><th>Seq</th><th>Timestamp</th><th>Lat</th><th>Lon</th><th>Alt</th><th>Temp</th><th>Pressure</th><th>Battery</th><th>Mode</th><th>Fix</th></tr>
          </thead>
          <tbody id="log-body"></tbody>
        </table>
      </div>
    </div>`;

  const sessionSelect = root.querySelector('#log-session');
  const fileInput = root.querySelector('#log-file');
  const searchInput = root.querySelector('#log-search');
  const fromInput = root.querySelector('#log-from');
  const toInput = root.querySelector('#log-to');
  const exportBtn = root.querySelector('#log-export');
  const countEl = root.querySelector('#log-count');
  const totalEl = root.querySelector('#log-total');
  const sessionInfo = root.querySelector('#log-session-info');
  const tbody = root.querySelector('#log-body');

  /** The packet set currently in view: live buffer, or an imported session. */
  function active() {
    const id = s.activeSessionId;
    if (id !== 'live') {
      const entry = s.loadedSessions.find(e => e.id === id);
      if (entry) {
        return {
          items: entry.telemetry,
          label: `${entry.session.name} (imported)`,
          onboard: entry.onboardRecords,
        };
      }
    }
    return { items: s.packets.items, label: 'Ground-received (live)', onboard: null };
  }

  /** Apply search + date filters to *items* and return the filtered array. */
  function applyFilters(items) {
    const filter = searchInput.value.toLowerCase().trim();
    const from = fromInput.value ? Date.parse(fromInput.value) : null;
    const to = toInput.value ? Date.parse(toInput.value) : null;
    const out = [];
    for (const p of items) {
      if (!p) continue;
      if (filter) {
        const str = `${p.seq} ${p.ts ?? ''} ${p.mode} ${p.phase || ''} ${p.lat ?? ''} ${p.lon ?? ''}`.toLowerCase();
        if (!str.includes(filter)) continue;
      }
      const t = p.rx_ts ?? p.ts;
      if (from != null && Number.isFinite(t) && t < from) continue;
      if (to != null && Number.isFinite(t) && t > to) continue;
      out.push(p);
    }
    return out;
  }

  function exportCSV() {
    const allItems = active().items;
    const filtered = applyFilters(allItems);
    if (filtered.length === 0) return;

    const headers = ['seq', 'ts', 'rx_ts', 'lat', 'lon', 'alt', 'temp_ext', 'pressure', 'battery', 'battery_pct', 'mode', 'phase', 'rssi', 'snr', 'fix'];
    const rows = [headers.join(',')];
    for (const p of filtered) {
      rows.push(headers.map(h => {
        const v = p[h];
        if (v == null) return '';
        const str = String(v);
        return str.includes(',') ? `"${str}"` : str;
      }).join(','));
    }

    const blob = new Blob([rows.join('\n')], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${s.settings.csvPrefix}_${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }

  function render() {
    const src = active();
    const items = src.items;
    const cap = Math.max(1, s.settings.logRowCap || 2000);

    // Rebuild the session dropdown whenever there are loaded sessions,
    // so it stays in sync even when sessions are loaded outside importFile().
    const optLive = '<option value="live">Live Session</option>';
    const optSessions = s.loadedSessions
      .map(e => `<option value="${e.id}">${e.session.name}</option>`)
      .join('');
    sessionSelect.innerHTML = optLive + optSessions;

    // Sync the select value to the active session (only when not live).
    // Guard against triggering the change handler during initial render
    // or when the value is already correct.
    if (s.activeSessionId !== 'live') {
      sessionSelect.value = s.activeSessionId;
    }

    totalEl.textContent = items.length;

    const filtered = applyFilters(items);

    countEl.textContent = filtered.length;

    sessionInfo.textContent = src.onboard === null
      ? `${src.label} · onboard records not yet recovered`
      : `${src.label} · ${src.onboard} onboard record(s) not imported`;

    if (filtered.length === 0) {
      tbody.innerHTML = '<tr><td colspan="10" style="text-align:center;color:var(--muted);padding:20px;">No data</td></tr>';
      return;
    }

    const start = Math.max(0, filtered.length - cap);
    const rows = [];
    for (let i = filtered.length - 1; i >= start; i--) {
      const p = filtered[i];
      const t = p.rx_ts ?? p.ts;
      rows.push(
        `<tr>` +
        `<td>${p.seq}</td>` +
        `<td>${t != null ? new Date(t).toISOString() : '—'}</td>` +
        `<td>${p.lat != null ? p.lat.toFixed(6) : '—'}</td>` +
        `<td>${p.lon != null ? p.lon.toFixed(6) : '—'}</td>` +
        `<td>${p.alt != null ? Math.round(p.alt) + ' m' : '—'}</td>` +
        `<td>${p.temp_ext != null ? p.temp_ext.toFixed(1) + ' °C' : '—'}</td>` +
        `<td>${p.pressure != null ? p.pressure.toFixed(1) + ' hPa' : '—'}</td>` +
        `<td>${p.battery != null ? p.battery.toFixed(2) + ' V' : '—'}</td>` +
        `<td>${p.mode}</td>` +
        `<td>${p.fix === 3 ? '3D' : p.fix === 2 ? '2D' : '—'}</td>` +
        `</tr>`
      );
    }
    tbody.innerHTML = rows.join('');
  }

  function importFile(evt) {
    const files = evt.target.files;
    const f = files && files.length ? files[files.length - 1] : null;
    if (!f) return;
    f.text().then(txt => {
      let parsed = null;
      try {
        parsed = parseSessionFile(JSON.parse(txt));
      } catch { /* malformed JSON */ }
      if (!parsed || parsed.telemetry.length === 0) {
        sessionInfo.textContent = 'Import failed — no telemetry in file';
        return;
      }
      const id = `s${Date.now()}`;
      store.loadSession(parsed, id);
      store.setActiveSession(id);
      render();
    });
  }

  searchInput.addEventListener('input', render);
  fromInput.addEventListener('input', render);
  toInput.addEventListener('input', render);
  sessionSelect.addEventListener('change', () => {
    store.setActiveSession(sessionSelect.value);
    render();
  });
  fileInput.addEventListener('change', importFile);
  exportBtn.addEventListener('click', exportCSV);

  const unsubTelemetry = store.on('telemetry', render);
  const unsubSession = store.on('session-select', render);
  render();
  return () => {
    unsubTelemetry();
    unsubSession();
  };
}
