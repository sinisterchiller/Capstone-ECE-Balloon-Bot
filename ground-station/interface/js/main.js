// Entry point: boot sequence, top-bar binding, global 1 s ticker.

import { store } from './store.js';
import { Router } from './router.js';
import { TelemetryLink } from './link.js';
import { fmtDuration, fmtAgo } from './format.js';
import { renderOverview } from './pages/overview.js';
import { renderGps } from './pages/gps.js';
import { renderAtmospheric } from './pages/atmospheric.js';
import { renderEvents } from './pages/events.js';
import { renderCommPage } from './pages/comm-page.js';
import { renderLogs } from './pages/logs.js';
import { renderSettings } from './pages/settings.js';

const routes = [
  { hash: '#/overview', render: renderOverview },
  { hash: '#/gps', render: renderGps },
  { hash: '#/atmospheric', render: renderAtmospheric },
  { hash: '#/events', render: renderEvents },
  { hash: '#/comm', render: renderCommPage },
  { hash: '#/logs', render: renderLogs },
  { hash: '#/settings', render: renderSettings },
];

function bindTopBar() {
  const s = store.state;
  const els = {
    mission: document.getElementById('tb-mission'),
    elapsed: document.getElementById('tb-elapsed'),
    lastPkt: document.getElementById('tb-lastpkt'),
    mode: document.getElementById('tb-mode'),
    link: document.getElementById('tb-link'),
    linkLabel: document.getElementById('tb-link-label'),
    battery: document.getElementById('tb-battery'),
    batteryVal: document.getElementById('tb-battery-val'),
    batteryFill: document.getElementById('tb-battery-fill'),
    stale: document.getElementById('tb-stale'),
    staleAgo: document.getElementById('tb-stale-ago'),
  };

  const setLink = () => {
    let label, cls;
    if (s.source === 'simulator') { label = 'SIM'; cls = 'ok'; }
    else if (s.wsState === 'connecting') { label = 'CONNECTING'; cls = 'warn'; }
    else if (s.wsState === 'open' && s.stale) { label = 'STALE'; cls = 'warn'; }
    else if (s.wsState === 'open') { label = 'LINK'; cls = 'ok'; }
    else { label = 'NO LINK'; cls = 'fault'; }
    els.linkLabel.textContent = label;
    els.link.className = `tb-chip link ${cls}`;
  };

  const onTick = now => {
    const launchTs = s.session.launchTs;
    els.elapsed.textContent = launchTs ? fmtDuration(now - launchTs) : '—';
    els.lastPkt.textContent = s.link.lastPacketAt
      ? fmtAgo(now - s.link.lastPacketAt)
      : '—';
    if (s.stale) {
      els.stale.hidden = false;
      els.staleAgo.textContent = fmtAgo(now - s.link.lastPacketAt);
    } else {
      els.stale.hidden = true;
    }
  };

  const onTelemetry = p => {
    els.mode.textContent = p.mode;
    els.mode.className = `tb-chip mode-${p.mode.toLowerCase()}`;
    if (p.battery_pct != null) {
      els.batteryVal.textContent = `${Math.round(p.battery_pct)}%`;
      els.batteryFill.style.width = `${Math.max(0, Math.min(100, p.battery_pct))}%`;
      const cls = p.battery_pct < 10 ? 'fault' : p.battery_pct < 20 ? 'warn' : 'ok';
      els.battery.className = `tb-chip battery ${cls}`;
    }
  };

  const onHello = d => {
    els.mission.textContent = d.mission || s.settings.missionName;
  };

  store.on('tick', onTick);
  store.on('telemetry', onTelemetry);
  store.on('hello', onHello);
  store.on('link-state', setLink);
  store.on('stale', v => {
    document.body.classList.toggle('stale', v);
    setLink();
  });

  els.mission.textContent = s.settings.missionName;
  setLink();
  onTick(Date.now());
}

function boot() {
  bindTopBar();
  Router.register(routes);
  Router.start();

  const link = new TelemetryLink(store);
  link.startTicker();
  const s = store.state;
  if (s.settings.autoSim) {
    link.startSimulator({
      intervalMs: s.settings.simIntervalMs,
      lossRate: s.settings.simLossRate,
      seed: s.settings.simSeed,
      timeScale: s.settings.simTimeScale,
      mission: s.settings.missionName,
      launchLat: s.settings.launchLat,
      launchLon: s.settings.launchLon,
    });
  }
  window.addEventListener('beforeunload', () => link.dispose());
}

boot();
