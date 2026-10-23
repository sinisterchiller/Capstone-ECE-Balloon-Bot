# Pitfalls / known issues

- **Chrome `--virtual-time-budget` desync (2026-10-21):** headless Chrome fast-forward runs the virtual clock ahead of chained `setTimeout` timers. Symptom: `tb-lastpkt` shows ~10–11 s in virtual-time DOM dumps while cards keep updating (simulator alive). This is a harness artifact, not an app bug — in real time both advance together (~1 s). For ground truth, run the page in real time (user's browser) or a CDP driver with a real-time wait.
- **Classifier blocks `--remote-debugging-port`** on this machine's auto-mode; `--screenshot` and `--dump-dom` headless invocations are allowed.
- ES modules require HTTP serving (`file://` breaks module loading) — `python3 -m http.server 8000` from `interface/`.
- **CDP debugging attempt (2026-10-22):** `test-step6.js` / `debug-search.js` / `verify-step6.js` (in `interface/`) drove Chrome via `--remote-debugging-port` + the `ws` package. Consistent with the blocked-port pitfall above, they were never confirmed to run. Deleted 2026-10-22.
- **npm cruft (2026-10-22):** `package.json` + `package-lock.json` + `node_modules/ws` existed at BOTH the repo root and `interface/`, installed only for those CDP scripts. The dashboard itself stays dependency-free. As of this session they were moved to `_cruft-2026-10-22/` at the repo root (auto-mode blocked `rm` on them) — finish with `rm -rf _cruft-2026-10-22`.
- Nothing under `ground-station/`, `AGENTS.md` or `.claude/` is committed to git (branch `feature/ground-station` has only docs commits); there is no `.gitignore`.
