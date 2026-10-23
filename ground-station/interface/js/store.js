// Single state store for the dashboard. Both the WebSocket client and the
// simulator feed messages through store.ingest(); pages subscribe to channels
// and do minimal DOM updates.

import { normalize } from './schema.js';

export const CAPS = { packets: 10000, events: 2000, recent: 200, spark: 300 };

/** Capped FIFO buffer (oldest evicted when over capacity). */
export class PushBuffer {
  constructor(cap) {
    this.cap = cap;
    this.items = [];
  }
  push(item) {
    this.items.push(item);
    if (this.items.length > this.cap) this.items.shift();
  }
  get length() { return this.items.length; }
  get(i) { return this.items[i] ?? null; }
  slice(a, b) { return this.items.slice(a, b); }
  last(n = 1) {
    const s = this.items;
    return s.slice(Math.max(0, s.length - n));
  }
  toArray() { return this.items.slice(); }
  clear() { this.items.length = 0; }
}

export const DEFAULT_SETTINGS = {
  wsUrl: 'ws://localhost:8765/ws',
  autoSim: true,
  simIntervalMs: 1000,
  simLossRate: 0.02,
  simSeed: 42,
  simTimeScale: 1000,
  staleSec: 15,
  defaultWindowSec: 600,
  unitSystem: 'metric',
  logRowCap: 2000,
  missionName: 'Balloon Bot — Test Flight',
  launchLat: 51.078,
  launchLon: -114.123,
  csvPrefix: 'balloonbot',
};

const SETTINGS_KEY = 'bb.settings.v1';

export function loadSettings() {
  const out = { ...DEFAULT_SETTINGS };
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (raw) Object.assign(out, JSON.parse(raw));
  } catch { /* storage unavailable — use defaults */ }
  return out;
}

export function saveSettings(settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
  } catch { /* ignore */ }
}

function makeState() {
  return {
    source: 'none',          // 'none' | 'simulator' | 'ws'
    wsState: 'idle',         // 'idle' | 'connecting' | 'open' | 'closed'
    link: { lastPacketAt: 0, lastMsgAt: 0 },
    radio: null,             // 'connected' | 'degraded' | 'lost'
    session: { name: '', launch: null, launchTs: null, endedAt: null },
    activeSessionId: 'live', // 'live' or an imported-session id
    packets: new PushBuffer(CAPS.packets),
    events: new PushBuffer(CAPS.events),
    recentBySeq: new Map(),  // last ~200 packets, keyed by seq
    stats: {
      rx: 0, gaps: 0, badFrames: 0, acked: 0, ackMissed: 0,
      rssi: new PushBuffer(CAPS.spark),
      snr: new PushBuffer(CAPS.spark),
      delivery: new PushBuffer(CAPS.spark),
      ackRate: new PushBuffer(CAPS.spark),
    },
    stale: false,
    lastSeq: 0,
    settings: loadSettings(),
    loadedSessions: [], // { id, session, telemetry, events, onboardRecords }
  };
}

const channelListeners = new Map(); // channel -> Set<fn>
const globalListeners = new Set();

function on(channel, fn) {
  if (!channelListeners.has(channel)) channelListeners.set(channel, new Set());
  channelListeners.get(channel).add(fn);
  return () => channelListeners.get(channel)?.delete(fn);
}

function subscribe(fn) {
  globalListeners.add(fn);
  return () => globalListeners.delete(fn);
}

function emit(channel, ...args) {
  const fns = channelListeners.get(channel);
  if (fns) {
    for (const fn of fns) {
      try { fn(...args); } catch (err) { console.error(`[store:${channel}]`, err); }
    }
  }
  for (const fn of globalListeners) {
    try { fn(); } catch (err) { console.error('[store:*]', err); }
  }
}

/** Binary search a seq-ordered PushBuffer for a packet by seq. */
function findPacket(packets, seq) {
  let lo = 0, hi = packets.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    const p = packets.get(mid);
    if (p.seq === seq) return p;
    if (p.seq < seq) lo = mid + 1;
    else hi = mid - 1;
  }
  return null;
}

export const store = {
  state: makeState(),
  on,
  subscribe,
  emit,

  /** Single entry point for every wire message (WS or simulator). */
  ingest(raw) {
    const norm = normalize(raw);
    if (!norm) {
      this.state.stats.badFrames++;
      emit('badframe');
      return;
    }
    const { kind, data } = norm;
    const s = this.state;
    // Staleness is measured against the ground station's wall clock at the
    // moment of reception — never against mission/payload timestamps, which
    // can be in a different clock domain (simulator, clock skew).
    s.link.lastMsgAt = Date.now();
    switch (kind) {
      case 'hello': {
        // A new launch_ts means a new mission: reset accumulated state so a
        // restarted mission (seq back at 1) is not deduped away. The same
        // launch_ts (reconnect) keeps the session intact.
        const isNewMission = data.launch_ts !== null
          && (s.session.launchTs === null || data.launch_ts !== s.session.launchTs);
        if (isNewMission) {
          s.packets.clear();
          s.events.clear();
          s.recentBySeq.clear();
          s.stats.rx = 0;
          s.stats.gaps = 0;
          s.stats.badFrames = 0;
          s.stats.acked = 0;
          s.stats.ackMissed = 0;
          s.stats.rssi.clear();
          s.stats.snr.clear();
          s.stats.delivery.clear();
          s.stats.ackRate.clear();
          s.lastSeq = 0;
          s.stale = false;
        }
        s.session = {
          name: data.mission || s.session.name,
          launch: data.launch,
          launchTs: data.launch_ts,
          endedAt: null,
        };
        emit('hello', data);
        break;
      }
      case 'telemetry': {
        if (data.seq <= s.lastSeq) return; // dedupe / out-of-order
        const prevSeq = s.lastSeq;
        s.lastSeq = data.seq;
        s.packets.push(data);
        s.recentBySeq.set(data.seq, data);
        while (s.recentBySeq.size > CAPS.recent) {
          let min = Infinity;
          for (const k of s.recentBySeq.keys()) if (k < min) min = k;
          s.recentBySeq.delete(min);
        }
        s.link.lastPacketAt = Date.now();
        if (data.valid) {
          s.stats.rx++;
          if (data.rssi != null) s.stats.rssi.push({ t: data.rx_ts, v: data.rssi });
          if (data.snr != null) s.stats.snr.push({ t: data.rx_ts, v: data.snr });
          s.stats.delivery.push({ t: data.rx_ts, v: (s.stats.rx / data.seq) * 100 });
        } else {
          s.stats.badFrames++;
        }
        if (prevSeq > 0) s.stats.gaps += data.seq - prevSeq - 1;
        emit('telemetry', data);
        break;
      }
      case 'event': {
        s.events.push(data);
        emit('event', data);
        break;
      }
      case 'ack': {
        if (data.ack === 'acked') s.stats.acked++;
        else s.stats.ackMissed++;
        const denom = s.stats.acked + s.stats.ackMissed;
        if (denom > 0) {
          s.stats.ackRate.push({ t: data.ts ?? Date.now(), v: (s.stats.acked / denom) * 100 });
        }
        const p = findPacket(s.packets, data.seq) ?? s.recentBySeq.get(data.seq) ?? null;
        if (p && p.ack === 'pending') p.ack = data.ack;
        emit('ack', data);
        break;
      }
      case 'link': {
        s.radio = data.radio;
        emit('link', data);
        break;
      }
    }
  },

  /** 1 s heartbeat: drives elapsed time, last-packet clock and stale flag. */
  tick(now) {
    const s = this.state;
    const stale = s.link.lastPacketAt > 0
      && (now - s.link.lastPacketAt) > s.settings.staleSec * 1000;
    if (stale !== s.stale) {
      s.stale = stale;
      emit('stale', stale);
    }
    emit('tick', now);
  },

  /** Register an imported (historical) session. */
  loadSession(parsed, id) {
    const entry = { id, ...parsed, loadedAt: Date.now() };
    this.state.loadedSessions.push(entry);
    emit('session', entry);
    return entry;
  },

  /** Switch the active session ('live' or an imported-session id). */
  setActiveSession(id) {
    if (this.state.activeSessionId === id) return;
    this.state.activeSessionId = id;
    emit('session-select', id);
  },
};
