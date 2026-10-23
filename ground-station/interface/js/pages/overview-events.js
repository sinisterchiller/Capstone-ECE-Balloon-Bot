// Event monitor panel — recent events timeline.

import { store } from '../store.js';
import { fmtDuration } from '../format.js';

const MAX_EVENTS = 20;

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

export function renderEvents(container, store) {
  const s = store.state;

  container.innerHTML = `
    <h2 class="panel-title">Event Monitor</h2>
    <div class="event-list" id="event-list"></div>`;

  const list = container.querySelector('#event-list');

  function render() {
    const events = s.events;
    const count = Math.min(MAX_EVENTS, events.length);
    if (count === 0) {
      list.innerHTML = '<p style="padding:12px;color:var(--muted);text-align:center;font-size:12px">Awaiting events</p>';
      return;
    }

    const items = [];
    for (let i = 0; i < count; i++) {
      const idx = events.length - count + i;
      const e = events.get(idx);
      if (!e) continue;
      const elapsed = fmtDuration(e.ts - s.session.launchTs ?? e.ts);
      items.push(
        `<div class="event-item ${e.severity}">` +
          `<span class="event-sev-dot"></span>` +
          `<div class="event-body">` +
            `<div class="event-meta">` +
              `<span class="event-time">${elapsed}</span>` +
              `<span class="event-type">${shortenCategory(e.category)}</span>` +
            `</div>` +
            `<div class="event-msg" title="${e.message}">${e.message}</div>` +
          `</div>` +
        `</div>`
      );
    }
    list.innerHTML = items.join('');
    // Scroll to bottom
    list.scrollTop = list.scrollHeight;
  }

  const unsub = store.on('event', render);
  // Also re-render on hello (session start clears events)
  const unsubHello = store.on('hello', render);
  render();
  return () => { unsub(); unsubHello(); };
}
