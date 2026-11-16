// Telemetry log panel — recent packets table.

import { store } from '../store.js';
import { fmtClock } from '../format.js';

const MAX_ROWS = 50;

function statusClass(valid) {
  return valid ? 'ok-text' : 'fault-text';
}

function statusText(valid) {
  return valid ? 'OK' : 'BAD';
}

export function renderLog(container, store) {
  const s = store.state;

  container.innerHTML = `
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
    </div>`;

  const tbody = container.querySelector('#log-body');

  function render() {
    const packets = s.packets;
    const count = Math.min(MAX_ROWS, packets.length);
    if (count === 0) {
      tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--muted)">Awaiting telemetry</td></tr>';
      return;
    }

    const rows = [];
    for (let i = 0; i < count; i++) {
      const idx = packets.length - count + i;
      const p = packets.get(idx);
      if (!p) continue;
      const alt = p.alt != null ? Math.round(p.alt).toLocaleString() + ' m' : '—';
      const temp = p.temp_ext != null ? p.temp_ext.toFixed(1) + ' °C' : '—';
      rows.push(
        `<tr>` +
        `<td>${fmtClock(p.rx_ts)}</td>` +
        `<td>${p.seq}</td>` +
        `<td>${p.mode}</td>` +
        `<td>${alt}</td>` +
        `<td>${temp}</td>` +
        `<td class="${statusClass(p.valid)}">${statusText(p.valid)}</td>` +
        `</tr>`
      );
    }
    tbody.innerHTML = rows.join('');
  }

  const unsub = store.on('telemetry', render);
  render();
  return () => unsub();
}
