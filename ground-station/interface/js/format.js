// Null-safe formatting helpers. Every card/panel goes through these so
// "—" handling for missing measurements is centralized.

const pad2 = n => String(n).padStart(2, '0');

export function fmtNum(v, digits = 1, unit = '') {
  if (v == null || !Number.isFinite(v)) return '—';
  const s = v.toFixed(digits);
  return unit ? `${s} ${unit}` : s;
}

export function fmtDuration(ms) {
  if (ms == null || !Number.isFinite(ms) || ms < 0) return '—';
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return h > 0 ? `${h}:${pad2(m)}:${pad2(sec)}` : `${pad2(m)}:${pad2(sec)}`;
}

export const fmtAgo = fmtDuration;

export function fmtClock(ms) {
  if (ms == null || !Number.isFinite(ms)) return '—';
  const d = new Date(ms);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

export function fmtDateTime(ms) {
  if (ms == null || !Number.isFinite(ms)) return '—';
  const d = new Date(ms);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ` +
    `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

export function fmtLat(v) {
  if (v == null) return '—';
  return `${Math.abs(v).toFixed(6)}° ${v >= 0 ? 'N' : 'S'}`;
}

export function fmtLon(v) {
  if (v == null) return '—';
  return `${Math.abs(v).toFixed(6)}° ${v >= 0 ? 'E' : 'W'}`;
}

export function fmtAlt(v, unitSystem = 'metric') {
  if (v == null) return '—';
  return unitSystem === 'imperial'
    ? `${Math.round(v * 3.28084).toLocaleString()} ft`
    : `${Math.round(v).toLocaleString()} m`;
}

export function fmtVSpeed(v, unitSystem = 'metric') {
  if (v == null) return '—';
  return unitSystem === 'imperial'
    ? `${(v * 3.28084).toFixed(1)} ft/s`
    : `${v.toFixed(1)} m/s`;
}

export function fmtTemp(v, unitSystem = 'metric') {
  if (v == null) return '—';
  return unitSystem === 'imperial'
    ? `${(v * 9 / 5 + 32).toFixed(1)} °F`
    : `${v.toFixed(1)} °C`;
}

export function fmtPress(v) {
  if (v == null) return '—';
  return `${v.toFixed(1)} hPa`;
}

export function fmtBattery(v) {
  if (v == null) return '—';
  return `${v.toFixed(2)} V`;
}
