# Synthetic test fixtures for ground-station server testing.
#
# All data below is **synthetic / fabricated** — it does not represent
# real flight telemetry. Filenames and file contents are explicitly
# labelled "synthetic" so they can be distinguished from any future
# real mission data.
#
# Files
# -----
# `sample.jsonl` — 8-line S2 fixture (telemetry/events/radio_status/radio_ack)
#   for manual and automated testing.
# `synthetic_flight.jsonl` — 5-line synthetic flight (prelaunch→ascent→burst→descent)
#   with ground receive timestamps. Used as input to the recorder's export path.
# `synthetic_flight.json` — exact dashboard-compatible export produced by
#   `Recorder.export()` from `synthetic_flight.jsonl` and verified against
#   the dashboard's `parseSessionFile` via Node.js dynamic import.
