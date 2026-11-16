/**
 * parse_session.test.mjs — Verify Python exporter output loads in dashboard parser.
 *
 * Reads the actual synthetic_flight.json produced by `Recorder.export()` and
 * feeds it through the dashboard's parseSessionFile to confirm schema
 * compatibility.  No fabricated records — this test only validates the
 * importer contract.
 *
 * Run:  node ground-station/interface/js/tests/parse_session.test.mjs
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
// tests/ → js/ → interface/ → ground-station/ → repo root
const repoRoot = join(__dirname, "..", "..", "..", "..");

// ── Main ───────────────────────────────────────────────────────────────────

async function main() {
  // Read the actual fixture produced by Python's Recorder.export().
  const fixturePath = join(
    repoRoot, "ground-station", "server", "fixtures", "synthetic_flight.json"
  );
  const raw = JSON.parse(readFileSync(fixturePath, "utf-8"));

  console.log("Testing dashboard parseSessionFile with Python exporter output…");
  console.log(`  Fixture: synthetic_flight.json`);
  console.log(`  Telemetry packets: ${raw.telemetry.length}`);
  console.log(`  Events: ${raw.events.length}`);
  console.log(`  Mission: ${raw.mission}`);

  const { parseSessionFile } = await import(join(__dirname, "../schema.js"));

  // Parse the session.
  const parsed = parseSessionFile(raw);
  if (!parsed) throw new Error("parseSessionFile returned null");

  console.log("\n✓ parseSessionFile accepted the export shape");
  console.log(`  Session name: ${parsed.session.name}`);
  console.log(`  Launch: ${parsed.session.launch.lat}, ${parsed.session.launch.lon}`);
  console.log(`  Launch ts: ${parsed.session.launch_ts}`);
  console.log(`  Ended at: ${parsed.session.ended_at}`);
  console.log(`  Telemetry packets: ${parsed.telemetry.length}`);
  console.log(`  Events: ${parsed.events.length}`);
  console.log(`  Onboard records: ${parsed.onboardRecords}`);

  // Verify field mapping against the known fixture contents.
  const checks = [
    ["session.name", parsed.session.name === "synthetic_flight", "mission name"],
    ["telemetry.length", parsed.telemetry.length === 4, "4 telemetry packets"],
    ["events.length", parsed.events.length === 1, "1 event"],
    ["telemetry[0].seq", parsed.telemetry[0].seq === 1, "seq=1 (first)"],
    ["telemetry[3].seq", parsed.telemetry[3].seq === 4, "seq=4 (last, sorted)"],
    ["telemetry[0].rx_ts", parsed.telemetry[0].rx_ts != null, "rx_ts present"],
    ["telemetry[0].lat", parsed.telemetry[0].lat != null, "lat present"],
    ["telemetry[0].alt", parsed.telemetry[0].alt != null, "alt present"],
    ["telemetry[0].mode", parsed.telemetry[0].mode === "NORMAL", "mode=NORMAL"],
    ["telemetry[2].mode", parsed.telemetry[2].mode === "EVENT", "mode=EVENT (burst)"],
    ["telemetry[0].phase", parsed.telemetry[0].phase === "PRELAUNCH", "phase=PRELAUNCH"],
    ["telemetry[2].phase", parsed.telemetry[2].phase === "BURST", "phase=BURST"],
    ["event[0].code", parsed.events[0].code === "BURST_DETECT", "burst code"],
    ["onboardRecords", parsed.onboardRecords === 0, "onboard_records=0"],
    ["launch.lat", parsed.session.launch.lat === 51.078, "launch lat"],
    ["launch.lon", parsed.session.launch.lon === -114.123, "launch lon"],
    ["launch_ts", parsed.session.launch_ts != null, "launch_ts present"],
  ];

  let failed = 0;
  for (const [name, ok, desc] of checks) {
    if (ok) {
      console.log(`  ✓ ${name}: ${desc}`);
    } else {
      console.error(`  ✗ ${name}: ${desc}`);
      failed++;
    }
  }

  if (failed > 0) {
    console.error(`\n✗ ${failed} check(s) failed`);
    process.exit(1);
  }

  console.log("\n✓ All parseSessionFile checks passed!");
}

main().catch(err => {
  console.error("TEST FAILED:", err.message);
  console.error(err.stack);
  process.exit(1);
});
