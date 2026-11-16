// Transport layer: WebSocket client + simulator source selection,
// reconnect with exponential backoff, and the 1 s store.tick() heartbeat.
// Simulator and WS are interchangeable sources behind one store.ingest().

import { Simulator } from './simulator.js';

export class TelemetryLink {
  constructor(store) {
    this.store = store;
    this.ws = null;
    this.sim = null;
    this._url = null;
    this._intentionalClose = false;
    this._reconnectDelay = 1000;
    this._reconnectTimer = null;
    this._tickTimer = null;
    this._simOpts = {};
    // Settings-page controls drive the live source through the store.
    store.on('connect-ws', url => this.connect(url));
    store.on('disconnect', () => { this.stopSimulator(); this.disconnect(); });
    store.on('start-sim', () => this.startSimulator(this._simOpts));
  }

  startTicker() {
    if (this._tickTimer) return;
    this._tickTimer = setInterval(() => this.store.tick(Date.now()), 1000);
  }

  startSimulator(opts = {}) {
    this._simOpts = opts;
    this.stopSimulator();
    this.disconnect();
    this.sim = new Simulator({
      onMessage: m => this.store.ingest(m),
      opts,
    });
    this.sim.start();
    const s = this.store.state;
    s.source = 'simulator';
    s.wsState = 'idle';
    this.store.emit('link-state');
  }

  stopSimulator() {
    if (this.sim) {
      this.sim.stop();
      this.sim = null;
    }
    const s = this.store.state;
    if (s.source === 'simulator') {
      s.source = 'none';
      this.store.emit('link-state');
    }
  }

  connect(url) {
    this.stopSimulator();
    this.disconnect();
    this._url = url;
    this._intentionalClose = false;
    this._reconnectDelay = 1000;
    this._open();
  }

  _open() {
    const s = this.store.state;
    s.wsState = 'connecting';
    s.source = 'ws';
    this.store.emit('link-state');
    let ws;
    try {
      ws = new WebSocket(this._url);
    } catch (err) {
      console.error('[link] WebSocket construction failed', err);
      this._scheduleReconnect();
      return;
    }
    this.ws = ws;
    ws.onmessage = e => {
      try {
        this.store.ingest(JSON.parse(e.data));
      } catch (err) {
        console.error('[link] bad frame', err);
      }
    };
    ws.onopen = () => {
      s.wsState = 'open';
      this._reconnectDelay = 1000;
      this.store.emit('link-state');
    };
    ws.onclose = () => {
      s.wsState = 'closed';
      this.store.emit('link-state');
      if (!this._intentionalClose) this._scheduleReconnect();
    };
    ws.onerror = () => { /* onclose follows */ };
  }

  disconnect() {
    this._intentionalClose = true;
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.onclose = null;
      try { this.ws.close(); } catch { /* ignore */ }
      this.ws = null;
    }
    const s = this.store.state;
    s.wsState = 'idle';
    if (s.source === 'ws') s.source = 'none';
    this.store.emit('link-state');
  }

  _scheduleReconnect() {
    if (this._intentionalClose || !this._url) return;
    const delay = this._reconnectDelay;
    this._reconnectDelay = Math.min(30000, this._reconnectDelay * 2);
    this._reconnectTimer = setTimeout(() => this._open(), delay);
  }

  dispose() {
    this.stopSimulator();
    this.disconnect();
    if (this._tickTimer) {
      clearInterval(this._tickTimer);
      this._tickTimer = null;
    }
  }
}
