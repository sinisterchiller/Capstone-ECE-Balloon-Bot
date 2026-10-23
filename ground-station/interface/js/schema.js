// Wire-format contract for the Balloon Bot ground station dashboard.
//
// One JSON object per WebSocket text frame. All timestamps are ISO 8601 UTC
// strings. Missing/unavailable measurements are null, never omitted.
//
// The make*() builders below are used by the built-in simulator; the future
// Python server in ground-station/server/ should emit the same flat shape.

export const SCHEMA_VERSION = 1;
export const MODES = ['NORMAL', 'EVENT', 'LOW_POWER'];
export const PHASES = ['PRELAUNCH', 'ASCENT', 'CRUISE', 'BURST', 'DESCENT', 'RECOVERED'];
export const SEVERITIES = ['info', 'warning', 'critical'];
export const CATEGORIES = ['mode_change', 'threshold', 'gnss', 'battery', 'link', 'system'];
const FIX_VALUES = [0, 2, 3];
const ACK_STATES = ['pending', 'acked', 'missed'];
const RADIO_STATES = ['connected', 'degraded', 'lost'];

export function parseTs(value) {
  if (value == null) return null;
  const t = Date.parse(value);
  return Number.isFinite(t) ? t : null;
}

function num(value) {
  if (value == null) return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function str(value, fallback = null) {
  return typeof value === 'string' ? value : fallback;
}

function oneOf(value, list, fallback) {
  return list.includes(value) ? value : fallback;
}

/**
 * Validate + coerce one wire message.
 * @param {unknown} raw parsed JSON frame
 * @returns {{kind: string, data: object} | null} null when structurally invalid
 */
export function normalize(raw) {
  if (!raw || typeof raw !== 'object') return null;
  switch (raw.type) {
    case 'hello': {
      const launch = raw.launch && typeof raw.launch === 'object'
        ? { lat: num(raw.launch.lat), lon: num(raw.launch.lon) }
        : null;
      return {
        kind: 'hello',
        data: {
          schema_version: num(raw.schema_version),
          mission: str(raw.mission, ''),
          launch_ts: parseTs(raw.launch_ts),
          launch,
          firmware: str(raw.firmware, ''),
        },
      };
    }
    case 'telemetry': {
      const seq = num(raw.seq);
      if (!Number.isInteger(seq) || seq < 0) return null;
      const ts = parseTs(raw.ts);
      if (ts === null) return null;
      const fix = oneOf(raw.fix, FIX_VALUES, 0);
      const hasFix = fix > 0;
      return {
        kind: 'telemetry',
        data: {
          seq,
          ts,
          rx_ts: parseTs(raw.rx_ts) ?? ts,
          pkt_type: str(raw.pkt_type, 'telemetry'),
          valid: raw.valid !== false,
          fix,
          lat: hasFix ? num(raw.lat) : null,
          lon: hasFix ? num(raw.lon) : null,
          alt: hasFix ? num(raw.alt) : null,
          alt_baro: num(raw.alt_baro),
          temp_ext: num(raw.temp_ext),
          temp_int: num(raw.temp_int),
          rh: raw.rh == null ? null : Math.min(100, Math.max(0, Number(raw.rh))),
          pressure: num(raw.pressure),
          vspeed: num(raw.vspeed),
          battery: num(raw.battery),
          battery_pct: num(raw.battery_pct),
          mode: oneOf(raw.mode, MODES, 'NORMAL'),
          phase: str(raw.phase),
          rssi: num(raw.rssi),
          snr: num(raw.snr),
          ack: oneOf(raw.ack, ACK_STATES, 'pending'),
        },
      };
    }
    case 'event': {
      const ts = parseTs(raw.ts);
      if (ts === null) return null;
      const seq = num(raw.seq);
      return {
        kind: 'event',
        data: {
          id: str(raw.id, `evt_${ts}`),
          ts,
          category: oneOf(raw.category, CATEGORIES, 'system'),
          severity: oneOf(raw.severity, SEVERITIES, 'info'),
          code: str(raw.code, ''),
          message: str(raw.message, ''),
          measurement: str(raw.measurement),
          value: num(raw.value),
          threshold: num(raw.threshold),
          unit: str(raw.unit),
          mode: oneOf(raw.mode, MODES, null),
          seq: Number.isInteger(seq) ? seq : null,
        },
      };
    }
    case 'link':
      return {
        kind: 'link',
        data: {
          ts: parseTs(raw.ts),
          radio: oneOf(raw.radio, RADIO_STATES, 'lost'),
        },
      };
    case 'ack': {
      const seq = num(raw.seq);
      if (!Number.isInteger(seq) || seq < 0) return null;
      return {
        kind: 'ack',
        data: {
          ts: parseTs(raw.ts),
          seq,
          ack: oneOf(raw.ack, ['acked', 'missed'], 'missed'),
        },
      };
    }
    default:
      return null;
  }
}

// ---- Builders (used by the simulator; mirror these in the Python server) ----

export function iso(ms) {
  return new Date(ms).toISOString();
}

export function makeHello({ mission, launchTs, launch, firmware = '' }) {
  return {
    type: 'hello',
    schema_version: SCHEMA_VERSION,
    mission,
    launch_ts: iso(launchTs),
    launch,
    firmware,
  };
}

export function makeTelemetry(p) {
  return {
    type: 'telemetry',
    seq: p.seq,
    ts: iso(p.ts),
    rx_ts: iso(p.rx_ts ?? p.ts),
    pkt_type: 'telemetry',
    valid: p.valid !== false,
    fix: p.fix,
    lat: p.fix > 0 ? p.lat : null,
    lon: p.fix > 0 ? p.lon : null,
    alt: p.fix > 0 ? p.alt : null,
    alt_baro: p.alt_baro ?? null,
    temp_ext: p.temp_ext ?? null,
    temp_int: p.temp_int ?? null,
    rh: p.rh ?? null,
    pressure: p.pressure ?? null,
    vspeed: p.vspeed ?? null,
    battery: p.battery ?? null,
    battery_pct: p.battery_pct ?? null,
    mode: p.mode,
    phase: p.phase ?? null,
    rssi: p.rssi ?? null,
    snr: p.snr ?? null,
    ack: 'pending',
  };
}

export function makeEvent(e) {
  return {
    type: 'event',
    id: e.id,
    ts: iso(e.ts),
    category: e.category,
    severity: e.severity,
    code: e.code,
    message: e.message,
    measurement: e.measurement ?? null,
    value: e.value ?? null,
    threshold: e.threshold ?? null,
    unit: e.unit ?? null,
    mode: e.mode ?? null,
    seq: e.seq ?? null,
  };
}

export function makeLink(radio, ts) {
  return { type: 'link', ts: iso(ts), radio };
}

export function makeAck(seq, ack, ts) {
  return { type: 'ack', ts: iso(ts), seq, ack };
}

/**
 * Parse a session file (historical mission) loaded via <input type="file">.
 * Expected shape:
 *   { schema_version, mission, launch_ts, launch, telemetry: [], events: [],
 *     onboard_records }
 * Returns { session, telemetry, events, onboardRecords } or null.
 */
export function parseSessionFile(json) {
  if (!json || typeof json !== 'object') return null;
  const telemetry = (Array.isArray(json.telemetry) ? json.telemetry : [])
    .map(normalize)
    .filter(n => n && n.kind === 'telemetry')
    .map(n => n.data);
  const events = (Array.isArray(json.events) ? json.events : [])
    .map(normalize)
    .filter(n => n && n.kind === 'event')
    .map(n => n.data);
  telemetry.sort((a, b) => a.seq - b.seq);
  return {
    session: {
      name: typeof json.mission === 'string' ? json.mission : 'Imported session',
      launch: json.launch && typeof json.launch === 'object'
        ? { lat: num(json.launch.lat), lon: num(json.launch.lon) }
        : null,
      launch_ts: parseTs(json.launch_ts),
      ended_at: telemetry.length ? telemetry[telemetry.length - 1].rx_ts : null,
    },
    telemetry,
    events,
    onboardRecords: Number.isInteger(json.onboard_records) ? json.onboard_records : 0,
  };
}
