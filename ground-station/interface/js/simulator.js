// Built-in telemetry simulator. Emits the exact same wire schema as the
// future Python server, so every dashboard page is testable without hardware.
//
// Deterministic (seeded PRNG) for reproducible test runs. `timeScale`
// fast-forwards sim time so a 6 h mission plays in minutes.
//
// Step 1 emits `hello` + `telemetry` only; events and ACKs land in a later
// step. (Acks are now emitted alongside telemetry — see Communication page.)

import { makeHello, makeTelemetry, makeEvent, makeAck } from './schema.js';

function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const SCALE_HEIGHT = 8430;          // m, standard-atmosphere scale height
const SEA_PRESSURE = 1013.25;       // hPa
const BURST_PRESSURE = 195;         // hPa
const BATTERY_LOW = 7.0;            // V

function pressureAt(alt) {
  return SEA_PRESSURE * Math.exp(-alt / SCALE_HEIGHT);
}

function tempAt(alt) {
  return 15 - 6.5 * alt / 1000;
}

export class Simulator {
  constructor({ onMessage, opts = {} }) {
    this.onMessage = onMessage;
    this.opts = {
      intervalMs: 1000,
      lossRate: 0.02,
      seed: 42,
      timeScale: 1,
      mission: 'Balloon Bot — Test Flight',
      launchLat: 51.078,
      launchLon: -114.123,
      ...opts,
    };
    this.running = false;
    this.timer = null;
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.rand = mulberry32(this.opts.seed);
    const launchTs = Date.now() - 10_000; // 10 s of PRELAUNCH already elapsed
    this.simNow = launchTs;
    this.tSim = 10;
    this.seq = 1;
    this.alt = 0;
    this.prevAlt = 0;
    this.phase = 'PRELAUNCH';
    this.burstElapsed = 0;
    this.lat = this.opts.launchLat;
    this.lon = this.opts.launchLon;
    this.heading = this.rand() * Math.PI * 2;
    this.battery = 8.4;
    this.rh = 45;
    this.tempInt = 3;
    this.fixLossRemaining = 0;
    this.onMessage(makeHello({
      mission: this.opts.mission,
      launchTs,
      launch: { lat: this.lat, lon: this.lon },
      firmware: 'sim-1.0',
    }));
    this._schedule();
  }

  stop() {
    this.running = false;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
  }

  _schedule() {
    if (!this.running) return;
    this.timer = setTimeout(() => {
      this._tick();
      this._schedule();
    }, this.opts.intervalMs);
  }

  _tick() {
    const r = this.rand;
    const dt = (this.opts.intervalMs * this.opts.timeScale) / 1000; // sim seconds
    this.tSim += dt;
    this.simNow += dt * 1000;

    // ---- Phase machine ----
    const vTarget =
      this.phase === 'ASCENT' ? 5 + 1.5 * Math.sin(this.tSim / 47) :
      this.phase === 'BURST' ? -0.05 :
      this.phase === 'DESCENT' ? -5.5 : 0;
    this.prevAlt = this.alt;
    this.alt = Math.max(0, this.alt + vTarget * dt);

    if (this.phase === 'PRELAUNCH' && this.tSim >= 10) this.phase = 'ASCENT';
    if (this.phase === 'ASCENT' && pressureAt(this.alt) < BURST_PRESSURE) {
      this.phase = 'BURST';
      this.burstElapsed = 0;
    }
    if (this.phase === 'BURST') {
      this.burstElapsed += dt;
      if (this.burstElapsed > 7200) this.phase = 'DESCENT'; // ~2 h burst float
    }
    if (this.phase === 'DESCENT' && this.alt <= 0) this.phase = 'RECOVERED';

    // ---- Operating mode (NORMAL / EVENT / LOW_POWER) ----
    const eventish = this.phase === 'BURST' || this.phase === 'DESCENT'
      || this.fixLossRemaining > 0;
    const mode = this.battery < BATTERY_LOW
      ? (eventish ? 'EVENT' : 'LOW_POWER')
      : (eventish ? 'EVENT' : 'NORMAL');

    // ---- GPS ----
    if (this.fixLossRemaining > 0) {
      this.fixLossRemaining -= dt;
    } else if (r() < 0.04) {
      this.fixLossRemaining = 5 + r() * 15; // 5–20 s fix-loss episode
    }
    const inFix = this.fixLossRemaining <= 0;
    if (inFix && this.phase !== 'PRELAUNCH' && this.phase !== 'RECOVERED') {
      this.heading += (r() - 0.5) * 0.05;
      const drift = 3 + r() * 5; // m/s horizontal drift
      const d = drift * dt;
      this.lat += (Math.cos(this.heading) * d) / 111320;
      this.lon += (Math.sin(this.heading) * d) / (111320 * Math.cos(this.lat * Math.PI / 180));
    }

    // ---- Power ----
    this.battery = Math.max(6.0, 8.4 - 0.0004 * this.tSim + (r() - 0.5) * 0.01);
    const batteryPct = Math.round(Math.min(100, Math.max(0, ((this.battery - 6.6) / (8.4 - 6.6)) * 100)));

    // ---- Sensors ----
    const pressure = pressureAt(this.alt);
    const tempExt = tempAt(this.alt) + 2 * Math.sin(this.alt / 150) + (r() - 0.5) * 0.6;
    this.rh = Math.min(85, Math.max(30, this.rh + (r() - 0.5) * 2));
    this.tempInt = Math.min(9, Math.max(-2,
      this.tempInt + (r() - 0.5) * 0.4 + (3 - this.tempInt) * 0.05));
    const vspeed = (this.alt - this.prevAlt) / Math.max(dt, 1e-6) + (r() - 0.5) * 0.4;
    const altBaro = -SCALE_HEIGHT * Math.log(
      Math.max(1, pressure + (r() - 0.5) * 1.5) / SEA_PRESSURE);
    const rssi = Math.round(Math.max(-118, Math.min(-60,
      -82 - (this.alt / 1000) * 4 - r() * 8)));
    const snr = Math.round((4 + r() * 8) * 10) / 10;
    const valid = r() >= 0.01; // ~1% corrupted frames

    // ---- Packet drop (LoRa loss) ----
    const dropped = r() < this.opts.lossRate * (1 + this.alt / 3000);
    if (!dropped) {
      this.onMessage(makeTelemetry({
        seq: this.seq,
        ts: this.simNow,
        rx_ts: this.simNow,
        fix: inFix ? 3 : 0,
        lat: this.lat,
        lon: this.lon,
        alt: this.alt,
        alt_baro: altBaro,
        temp_ext: tempExt,
        temp_int: this.tempInt,
        rh: this.rh,
        pressure,
        vspeed,
        battery: this.battery,
        battery_pct: batteryPct,
        mode,
        phase: this.phase,
        rssi,
        snr,
        valid,
      }));
      // The ground station ACKs each packet it receives; ~5% of ACKs fail.
      this.onMessage(makeAck(this.seq, r() < 0.95 ? 'acked' : 'missed', this.simNow));
    }
    this.seq++;

    // ---- Emit events at phase transitions and threshold crossings ----
    if (this.phase === 'BURST' && !this._burstEmitted) {
      this._burstEmitted = true;
      this.onMessage(makeEvent({
        id: `evt_burst_${this.seq}`,
        ts: this.simNow,
        category: 'threshold',
        severity: 'warning',
        code: 'BURST_DETECT',
        message: 'Burst detected — pressure below threshold',
        measurement: 'pressure',
        value: pressure,
        threshold: BURST_PRESSURE,
        unit: 'hPa',
        mode: 'EVENT',
        seq: this.seq,
      }));
    }
    if (this.phase === 'DESCENT' && !this._descentEmitted) {
      this._descentEmitted = true;
      this.onMessage(makeEvent({
        id: `evt_descent_${this.seq}`,
        ts: this.simNow,
        category: 'mode_change',
        severity: 'info',
        code: 'MODE_DESCENT',
        message: 'Mode transitioned to DESCENT',
        measurement: 'phase',
        value: 1,
        threshold: null,
        unit: null,
        mode: 'EVENT',
        seq: this.seq,
      }));
    }
    if (this.battery < BATTERY_LOW && !this._batteryEmitted) {
      this._batteryEmitted = true;
      this.onMessage(makeEvent({
        id: `evt_batt_${this.seq}`,
        ts: this.simNow,
        category: 'battery',
        severity: 'critical',
        code: 'BATT_LOW',
        message: 'Battery voltage below 7.0V',
        measurement: 'battery',
        value: this.battery,
        threshold: BATTERY_LOW,
        unit: 'V',
        mode: 'EVENT',
        seq: this.seq,
      }));
    }
    // Dropped packets are not emitted, but seq still advances, so the gap is
    // visible to the dashboard (delivery % + gap detection).
  }
}
