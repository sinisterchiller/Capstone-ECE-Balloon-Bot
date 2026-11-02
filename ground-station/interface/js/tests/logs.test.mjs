/**
 * logs.test.mjs — Meaningful tests for the Data Logs page (logs.js).
 *
 * Uses the checked-in synthetic_flight.json fixture (Python exporter output)
 * and exercises the real renderLogs function, session switch, date/search
 * filters, and CSV export.  A minimal fake DOM is used because the dashboard
 * is designed for browser-only ES-module loading.
 *
 * Run:  node ground-station/interface/js/tests/logs.test.mjs
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
// tests/ → js/ → interface/ → ground-station/ → repo root
const repoRoot = join(__dirname, "..", "..", "..", "..");

// ── Minimal DOM that parses HTML innerHTML ──────────────────────────────────

class FakeElement {
  constructor(tag = "div") {
    this.tagName = tag.toUpperCase();
    this._innerHTML = "";
    this.children = [];
    this._ids = {};
    this._classes = {};
    this._events = {};
    this._text = undefined;
    this._attrs = {};
    this._parent = null;
  }
  get id() { return this._attrs["id"]; }
  get value() { return this._attrs["value"] ?? ""; }
  set value(v) { this._attrs["value"] = String(v); }
  get textContent() {
    if (this._text !== undefined) return this._text;
    let text = "";
    const walk = (node) => {
      if (node._text !== undefined) text += node._text;
      for (const c of (node.children || [])) walk(c);
    };
    for (const c of (this.children || [])) walk(c);
    return text;
  }
  set textContent(v) { this._text = String(v); }
  querySelector(sel) {
    if (!sel || typeof sel !== "string") return null;
    if (sel.startsWith("#")) return this._ids[sel.slice(1)] ?? null;
    if (sel.startsWith(".")) {
      const cls = sel.slice(1);
      const result = [];
      const walk = (node) => {
        if (node._classes && node._classes[cls]) result.push(node);
        for (const c of (node.children || [])) walk(c);
      };
      walk(this);
      return result[0] ?? null;
    }
    const result = [];
    const walk = (node) => {
      if (node.tagName === sel.toUpperCase()) result.push(node);
      for (const c of (node.children || [])) walk(c);
    };
    walk(this);
    return result[0] ?? null;
  }
  setAttribute(k, v) {
    this._attrs[k] = v;
    if (k === "id") { this._ids[v] = this; }
    if (k === "class") {
      this._classes = {};
      for (const cls of String(v).split(/\s+/)) this._classes[cls] = true;
    }
  }
  set innerHTML(html) {
    this._innerHTML = html;
    // Preserve this element's own event listeners — real browsers keep them
    // when innerHTML is replaced (listeners live on the element, not its
    // children).  Only clear listeners on the newly-created children.
    const savedEvents = this._events;
    this._events = {};
    const temp = new FakeElement();
    temp._parseHTML(html);
    this.children = temp.children;
    this._classes = {};
    this._events = savedEvents;
    const collectIDs = (node) => {
      if (node.id) this._ids[node.id] = node;
      for (const c of (node.children || [])) collectIDs(c);
    };
    for (const c of (this.children || [])) collectIDs(c);
    const setParent = (node, parent) => {
      node._parent = parent;
      for (const c of (node.children || [])) setParent(c, node);
    };
    for (const c of (this.children || [])) setParent(c, this);
    const collectClasses = (node) => {
      if (node._classes) {
        for (const cls of Object.keys(node._classes)) this._classes[cls] = true;
      }
      for (const c of (node.children || [])) collectClasses(c);
    };
    for (const c of (this.children || [])) collectClasses(c);
  }
  _parseHTML(html) {
    const VOID_ELEMENTS = new Set([
      "area", "base", "br", "col", "embed", "hr", "img", "input",
      "link", "meta", "param", "source", "track", "wbr",
    ]);
    const stack = [this];
    let i = 0;
    while (i < html.length) {
      if (html[i] !== "<") {
        let text = "";
        while (i < html.length && html[i] !== "<") { text += html[i]; i++; }
        if (text.trim()) {
          const textNode = new FakeElement("#text");
          textNode._text = text;
          stack[stack.length - 1].children.push(textNode);
        }
        continue;
      }
      if (html.substring(i, i + 4) === "<!--") {
        const end = html.indexOf("-->", i + 4);
        i = end >= 0 ? end + 3 : html.length;
        continue;
      }
      if (html[i + 1] === "/") {
        const end = html.indexOf(">", i);
        stack.pop();
        i = end + 1;
        continue;
      }
      const end = html.indexOf(">", i);
      const tagStr = html.substring(i + 1, end);
      const explicitSelfClose = tagStr.endsWith("/");
      const [tagRest, ...attrParts] = tagStr.split(/\s+/);
      const tagName = tagRest.replace(/\/$/, "").trim().toLowerCase();
      if (!tagName) { i = end + 1; continue; }
      const node = new FakeElement(tagName.toUpperCase());
      for (const part of attrParts) {
        const eq = part.indexOf("=");
        if (eq < 0) {
          if (part) node.setAttribute(part, "");
        } else {
          const attrName = part.substring(0, eq);
          let attrVal = part.substring(eq + 1);
          if ((attrVal.startsWith('"') && attrVal.endsWith('"')) ||
              (attrVal.startsWith("'") && attrVal.endsWith("'"))) {
            attrVal = attrVal.slice(1, -1);
          }
          node.setAttribute(attrName, attrVal);
        }
      }
      stack[stack.length - 1].children.push(node);
      if (!explicitSelfClose && !VOID_ELEMENTS.has(tagName)) {
        stack.push(node);
      }
      i = end + 1;
    }
  }
  addEventListener(e, fn) { this._events[e] = fn; }
  removeEventListener() {}
  dispatchEvent(evt) {
    if (this._events[evt.type]) this._events[evt.type](evt);
  }
  appendChild(el) {
    el._parent = this;
    this.children.push(el);
    if (el.id) this._ids[el.id] = el;
    return el;
  }
  /** <select> convenience: returns <option> children as array. */
  get options() {
    return this.children.filter(c => c.tagName === "OPTION");
  }
}

// ── Helpers ─────────────────────────────────────────────────────────────────

/** Count only data rows (exclude "No data" colspan row). */
const dataRows = (tbody) =>
  tbody.children.filter(c => c.tagName === "TR" && c.children.length > 1);

/** Reset the shared store to a clean slate (live mode, no sessions). */
async function resetStore() {
  const { store } = await import(join(__dirname, "../store.js"));
  // Clear imported sessions and reset to live mode.
  store.state.loadedSessions = [];
  store.state.activeSessionId = "live";
  store.state.packets.clear();
  store.state.events.clear();
  store.state.recentBySeq.clear();
  store.state.stats.rx = 0;
  store.state.stats.gaps = 0;
  store.state.stats.badFrames = 0;
  store.state.stats.acked = 0;
  store.state.stats.ackMissed = 0;
  store.state.stats.rssi.clear();
  store.state.stats.snr.clear();
  store.state.stats.delivery.clear();
  store.state.stats.ackRate.clear();
  store.state.lastSeq = 0;
  store.state.stale = false;
  store.state.link.lastPacketAt = 0;
  store.state.link.lastMsgAt = 0;
  store.state.radio = null;
  store.state.session = { name: "", launch: null, launchTs: null, endedAt: null };
}

/** Load the synthetic_flight.json fixture and parse it via the real store. */
async function loadFixtureSession() {
  const fixturePath = join(repoRoot, "ground-station", "server", "fixtures", "synthetic_flight.json");
  const raw = JSON.parse(readFileSync(fixturePath, "utf-8"));

  const { parseSessionFile } = await import(join(__dirname, "../schema.js"));
  const { store } = await import(join(__dirname, "../store.js"));

  const parsed = parseSessionFile(raw);
  if (!parsed) throw new Error("parseSessionFile returned null");

  const id = `test_session_${Date.now()}`;
  store.loadSession(parsed, id);
  store.setActiveSession(id);
  return { store, parsed, id, raw };
}

// ── Test 1: renderLogs renders all packets from imported session ─────────────

async function testRenderLogs() {
  console.log("\n[Test 1] renderLogs renders imported session packets");
  await resetStore();

  const { parsed, id } = await loadFixtureSession();
  const { renderLogs } = await import(join(__dirname, "../pages/logs.js"));

  const rootEl = new FakeElement();
  const cleanup = renderLogs(rootEl);

  const sessionSelect = rootEl.querySelector("#log-session");
  if (!sessionSelect) throw new Error("Missing #log-session select");
  if (sessionSelect.value !== id)
    throw new Error(`Expected session value="${id}", got "${sessionSelect.value}"`);

  const countEl = rootEl.querySelector("#log-count");
  const totalEl = rootEl.querySelector("#log-total");
  if (!countEl || !totalEl) throw new Error("Missing count elements");

  const tbody = rootEl.querySelector("#log-body");
  if (!tbody) throw new Error("Missing #log-body");

  const rows = dataRows(tbody);
  const expectedPackets = parsed.telemetry.length;
  if (rows.length === 0) throw new Error("No rows rendered");
  if (rows.length < expectedPackets)
    throw new Error(`Expected at least ${expectedPackets} rows, got ${rows.length}`);

  // Verify seq numbers (newest first)
  const seqsInRows = [];
  for (const row of rows) {
    const cells = row.children.filter(c => c.tagName === "TD");
    if (cells.length > 0) {
      const seqText = cells[0]?.textContent?.trim();
      if (seqText) seqsInRows.push(parseInt(seqText, 10));
    }
  }
  if (seqsInRows.length === 0) throw new Error("No seq values found in table rows");
  if (seqsInRows[0] !== 4)
    throw new Error(`Expected first row seq=4 (newest), got seq=${seqsInRows[0]}`);

  const sessionInfo = rootEl.querySelector("#log-session-info");
  if (!sessionInfo || !sessionInfo.textContent.includes("imported"))
    throw new Error(`Session info missing "imported": "${sessionInfo?.textContent}"`);

  cleanup();
  console.log(`  ✓ ${rows.length} rows rendered, seqs: ${seqsInRows.join(", ")}`);
  console.log(`  ✓ Session select shows imported session`);
  console.log(`  ✓ Session info says "imported"`);
}

// ── Test 2: Session switch (live ↔ imported) ────────────────────────────────

async function testSessionSwitch() {
  console.log("\n[Test 2] Session switch between live and imported");
  await resetStore();

  // Seed 3 distinct live packets so the live session has different content
  // than the imported session (which has 4 packets). This ensures switching
  // actually changes the table, rather than showing the same count.
  const { store } = await import(join(__dirname, "../store.js"));
  const livePackets = [
    { seq: 101, ts: Date.now(), rx_ts: Date.now(), mode: "NORMAL", phase: "ASCENT",
      lat: 51.0, lon: -114.0, alt: 1000, temp_ext: 0, pressure: 900, battery: 8.3, fix: 3 },
    { seq: 102, ts: Date.now() + 5000, rx_ts: Date.now() + 5000, mode: "NORMAL", phase: "ASCENT",
      lat: 51.1, lon: -114.1, alt: 2000, temp_ext: -10, pressure: 800, battery: 8.2, fix: 3 },
    { seq: 103, ts: Date.now() + 10000, rx_ts: Date.now() + 10000, mode: "EVENT", phase: "BURST",
      lat: 51.2, lon: -114.2, alt: 15000, temp_ext: -50, pressure: 200, battery: 8.0, fix: 3 },
  ];
  for (const p of livePackets) store.state.packets.push(p);

  const { id, parsed } = await loadFixtureSession();
  const { renderLogs } = await import(join(__dirname, "../pages/logs.js"));

  const rootEl = new FakeElement();
  const cleanup = renderLogs(rootEl);

  const sessionSelect = rootEl.querySelector("#log-session");
  const totalEl = rootEl.querySelector("#log-total");

  // Verify imported session shows 4 packets.
  const importedTotal = parseInt(totalEl.textContent, 10);
  if (isNaN(importedTotal) || importedTotal !== 4)
    throw new Error(`Expected 4 packets for imported session, got "${totalEl.textContent}"`);

  // Switch to live: should show 3 packets (the seeded live data).
  sessionSelect.value = "live";
  sessionSelect.dispatchEvent({ type: "change" });
  const liveTotal = parseInt(totalEl.textContent, 10);
  if (liveTotal !== 3)
    throw new Error(`Expected 3 live packets, got ${liveTotal}`);

  // Verify the table content actually differs (different seq values).
  const tbody = rootEl.querySelector("#log-body");
  const liveSeqs = [];
  for (const row of tbody.children.filter((c) => c.tagName === "TR" && c.children.length > 1)) {
    const cells = row.children.filter((c) => c.tagName === "TD");
    if (cells.length > 0) {
      const seqText = cells[0]?.textContent?.trim();
      if (seqText) liveSeqs.push(parseInt(seqText, 10));
    }
  }
  if (liveSeqs.some((s) => s < 100))
    throw new Error(`Live table contains imported seq values: ${liveSeqs.join(", ")}`);

  // Switch back to imported: should restore 4 packets with imported seq values.
  sessionSelect.value = id;
  sessionSelect.dispatchEvent({ type: "change" });
  const backTotal = parseInt(totalEl.textContent, 10);
  if (backTotal !== importedTotal)
    throw new Error(`Session switch back: expected ${importedTotal}, got ${backTotal}`);

  const backSeqs = [];
  for (const row of tbody.children.filter((c) => c.tagName === "TR" && c.children.length > 1)) {
    const cells = row.children.filter((c) => c.tagName === "TD");
    if (cells.length > 0) {
      const seqText = cells[0]?.textContent?.trim();
      if (seqText) backSeqs.push(parseInt(seqText, 10));
    }
  }
  if (backSeqs.some((s) => s >= 100))
    throw new Error(`Back table contains live seq values: ${backSeqs.join(", ")}`);

  cleanup();
  console.log(`  ✓ Imported: ${importedTotal}, Live: ${liveTotal}, Back: ${backTotal}`);
  console.log(`  ✓ Live seqs: ${liveSeqs.join(", ")}, Back seqs: ${backSeqs.join(", ")}`);
  console.log(`  ✓ Session switch changes table content and restores on round-trip`);
}

// ── Test 3: Search filter narrows displayed rows ────────────────────────────

async function testSearchFilter() {
  console.log("\n[Test 3] Search filter narrows displayed rows");
  await resetStore();

  const { parsed } = await loadFixtureSession();
  const { renderLogs } = await import(join(__dirname, "../pages/logs.js"));

  const rootEl = new FakeElement();
  const cleanup = renderLogs(rootEl);

  const searchInput = rootEl.querySelector("#log-search");
  const tbody = rootEl.querySelector("#log-body");

  const initialRows = dataRows(tbody).length;
  if (initialRows === 0) throw new Error("No initial rows");

  searchInput.value = "EVENT";
  searchInput.dispatchEvent({ type: "input" });

  const filteredRows = dataRows(tbody).length;
  if (filteredRows >= initialRows)
    throw new Error(`Search "EVENT": expected fewer rows (${filteredRows}) than initial (${initialRows})`);

  // Verify only EVENT rows are shown
  const eventRowSeqs = [];
  for (const row of dataRows(tbody)) {
    const cells = row.children.filter(c => c.tagName === "TD");
    if (cells.length > 8) {
      const modeCell = cells[8]?.textContent?.trim();
      if (modeCell === "EVENT") eventRowSeqs.push(parseInt(cells[0]?.textContent, 10));
    }
  }
  if (eventRowSeqs.length !== filteredRows)
    throw new Error(`Expected all ${filteredRows} rows to be EVENT, found ${eventRowSeqs.length}`);

  searchInput.value = "";
  searchInput.dispatchEvent({ type: "input" });
  const clearedRows = dataRows(tbody).length;
  if (clearedRows !== initialRows)
    throw new Error(`Clear search: expected ${initialRows}, got ${clearedRows}`);

  cleanup();
  console.log(`  ✓ Initial: ${initialRows}, After "EVENT": ${filteredRows}, Cleared: ${clearedRows}`);
  console.log(`  ✓ All filtered rows are EVENT packets`);
}

// ── Test 4: Date (From/To) filters narrow displayed rows ────────────────────

async function testDateFilters() {
  console.log("\n[Test 4] Date (From/To) filters narrow displayed rows");
  await resetStore();

  const { parsed } = await loadFixtureSession();
  const { renderLogs } = await import(join(__dirname, "../pages/logs.js"));

  const rootEl = new FakeElement();
  const cleanup = renderLogs(rootEl);

  const fromInput = rootEl.querySelector("#log-from");
  const toInput = rootEl.querySelector("#log-to");
  const tbody = rootEl.querySelector("#log-body");

  const initialRows = dataRows(tbody).length;
  if (initialRows === 0) throw new Error("No initial rows");

  // From 2026-01-16 (next day) should give 0 data rows
  fromInput.value = "2026-01-16T00:00:00";
  fromInput.dispatchEvent({ type: "input" });
  const fromRows = dataRows(tbody).length;
  if (fromRows !== 0)
    throw new Error(`From next day: expected 0 data rows, got ${fromRows}`);

  // To 2026-01-14 (previous day) should give 0 data rows
  fromInput.value = "";
  toInput.value = "2026-01-14T00:00:00";
  toInput.dispatchEvent({ type: "input" });
  const toRows = dataRows(tbody).length;
  if (toRows !== 0)
    throw new Error(`To previous day: expected 0 data rows, got ${toRows}`);

  // Clear filters
  fromInput.value = "";
  toInput.value = "";
  fromInput.dispatchEvent({ type: "input" });
  toInput.dispatchEvent({ type: "input" });
  const restoredRows = dataRows(tbody).length;
  if (restoredRows !== initialRows)
    throw new Error(`Clear date filters: expected ${initialRows}, got ${restoredRows}`);

  cleanup();
  console.log(`  ✓ Initial: ${initialRows}, From next day: ${fromRows}, To prev day: ${toRows}, Restored: ${restoredRows}`);
  console.log(`  ✓ Date filter range works correctly`);
}

// ── Test 5: CSV export via file-input import → filter → click Export ────────

async function testCSVExport() {
  console.log("\n[Test 5] CSV export via file-input import → filter → click Export");
  await resetStore();

  const fixturePath = join(repoRoot, "ground-station", "server", "fixtures", "synthetic_flight.json");
  const fixtureContent = readFileSync(fixturePath, "utf-8");

  // ── Mock browser globals for exportCSV ──────────────────────────────────
  let capturedBlob = null;
  class MockBlob {
    constructor(parts, opts) {
      this._data = parts.join("");
      this.type = (opts && opts.type) || "";
      capturedBlob = this;
    }
  }
  const mockCreateObjectURL = () => "blob:test";
  const mockRevokeObjectURL = () => {};

  const mockAnchor = new FakeElement("a");
  mockAnchor.click = () => {}; // simulate anchor click for download
  const mockDocument = {
    createElement(tag) {
      if (tag === "a") return mockAnchor;
      return new FakeElement(tag);
    },
  };

  // ── Build the full pipeline ─────────────────────────────────────────────
  const { store } = await import(join(__dirname, "../store.js"));
  const { renderLogs } = await import(join(__dirname, "../pages/logs.js"));

  // Render the logs page (initially live session).
  const rootEl = new FakeElement();
  const cleanup = renderLogs(rootEl);

  // ── Drive the actual #log-file change handler ───────────────────────────
  // Create a File-like object whose text() resolves to the fixture JSON.
  const fileLike = {
    name: "synthetic_flight.json",
    text() { return Promise.resolve(fixtureContent); },
  };

  const fileInput = rootEl.querySelector("#log-file");
  if (!fileInput) throw new Error("Missing #log-file input");

  // Dispatch a change event with the file-like object in evt.target.files.
  const changeEvt = { type: "change", target: { files: [fileLike] } };
  fileInput.dispatchEvent(changeEvt);

  // The importFile handler is async (f.text().then(...)). Wait for the
  // store to reflect the newly loaded session AND for render() to have run.
  await new Promise(resolve => {
    const poll = setInterval(() => {
      if (store.state.loadedSessions.length > 0) {
        // Also verify render has populated the DOM.
        const totalEl = rootEl.querySelector("#log-total");
        if (totalEl && parseInt(totalEl.textContent, 10) === 4) {
          clearInterval(poll);
          resolve();
        }
      }
    }, 20);
    // Safety timeout
    setTimeout(() => {
      clearInterval(poll);
      resolve();
    }, 3000);
  });

  // ── Assert session imported and selected ────────────────────────────────
  const sessionSelect = rootEl.querySelector("#log-session");
  if (sessionSelect.options.length < 2)
    throw new Error(`Expected ≥2 options (live + imported), got ${sessionSelect.options.length}`);

  const importedId = store.state.loadedSessions[0].id;
  if (sessionSelect.value !== importedId)
    throw new Error(`Session not auto-selected: expected "${importedId}", got "${sessionSelect.value}"`);

  const totalEl = rootEl.querySelector("#log-total");
  if (parseInt(totalEl.textContent, 10) !== 4)
    throw new Error(`Expected 4 packets, got ${totalEl.textContent}`);

  // ── Apply date filter and verify count ──────────────────────────────────
  const fromInput = rootEl.querySelector("#log-from");
  fromInput.value = "2026-01-15T01:01";
  fromInput.dispatchEvent({ type: "input" });

  const countEl = rootEl.querySelector("#log-count");
  if (countEl.textContent !== "2")
    throw new Error(`Expected 2 filtered packets, got "${countEl.textContent}"`);

  // Collect seq numbers from the filtered table.
  const tbody = rootEl.querySelector("#log-body");
  const tableSeqs = [];
  for (const row of dataRows(tbody)) {
    const cells = row.children.filter(c => c.tagName === "TD");
    if (cells.length > 0) {
      const seqText = cells[0]?.textContent?.trim();
      if (seqText) tableSeqs.push(parseInt(seqText, 10));
    }
  }
  if (tableSeqs.length !== 2)
    throw new Error(`Expected 2 table rows, got ${tableSeqs.length}`);
  if (tableSeqs[0] !== 4)
    throw new Error(`Expected first row seq=4, got ${tableSeqs[0]}`);
  if (tableSeqs[1] !== 3)
    throw new Error(`Expected second row seq=3, got ${tableSeqs[1]}`);

  // ── Click Export CSV and verify Blob ────────────────────────────────────
  const exportBtn = rootEl.querySelector("#log-export");
  if (!exportBtn) throw new Error("Missing #log-export button");

  // Set up global mocks so exportCSV can run.
  const origBlob = globalThis.Blob;
  const origURL = globalThis.URL;
  const origDoc = globalThis.document;
  globalThis.Blob = MockBlob;
  globalThis.URL = { createObjectURL: mockCreateObjectURL, revokeObjectURL: mockRevokeObjectURL };
  globalThis.document = mockDocument;

  try {
    exportBtn.dispatchEvent({ type: "click" });

    if (!capturedBlob)
      throw new Error("Blob was not captured — exportCSV may not have run");

    // Parse the CSV.
    const lines = capturedBlob._data.split("\n").filter(l => l.length > 0);
    if (lines.length < 2)
      throw new Error(`CSV has only ${lines.length} line(s), expected header + data rows`);

    const header = lines[0].split(",");
    const expectedHeaders = ["seq", "ts", "rx_ts", "lat", "lon", "alt", "temp_ext",
                             "pressure", "battery", "battery_pct", "mode", "phase",
                             "rssi", "snr", "fix"];
    if (header.length !== expectedHeaders.length)
      throw new Error(`CSV header has ${header.length} cols, expected ${expectedHeaders.length}`);

    const dataLines = lines.slice(1);
    if (dataLines.length !== 2)
      throw new Error(`CSV data rows ${dataLines.length} != expected 2`);

    // Verify seq values match the table (CSV is chronological, table newest-first).
    const csvSeqs = dataLines.map(line => parseInt(line.split(",")[0], 10));
    if (csvSeqs[0] !== 3 || csvSeqs[1] !== 4)
      throw new Error(`CSV seqs ${csvSeqs.join(", ")} — expected 3, 4 (chronological)`);

    console.log(`  ✓ File import: session "${store.state.loadedSessions[0].session.name}" loaded`);
    console.log(`  ✓ Date filter: ${countEl.textContent} of ${totalEl.textContent} packets`);
    console.log(`  ✓ Table seqs: ${tableSeqs.join(", ")} (newest first)`);
    console.log(`  ✓ CSV export: ${dataLines.length} rows, seqs ${csvSeqs.join(", ")}`);
  } finally {
    globalThis.Blob = origBlob;
    globalThis.URL = origURL;
    globalThis.document = origDoc;
  }

  cleanup();
}

// ── Main ────────────────────────────────────────────────────────────────────

async function main() {
  console.log("=== Data Logs Page Tests ===");
  console.log(`Using synthetic_flight.json fixture`);

  try {
    await testRenderLogs();
    await testSessionSwitch();
    await testSearchFilter();
    await testDateFilters();
    await testCSVExport();

    console.log("\n✓ All 5 Data Logs tests passed!");
  } catch (err) {
    console.error("\n✗ TEST FAILED:", err.message);
    console.error(err.stack);
    process.exit(1);
  }
}

main();
