# Offline virtual signal audit fixtures (2026-10-22)

`native-detail.log` and `input-host-signal.log` form a synthetic exact request pair.
They deliberately use protocol power 1, selected-source power 2, and captured sample
6 versus serialized sample 7. `failure-cases.json` mutates this pair; every case
must produce zero exact matches. Additional selftests cover conflicting witnesses,
rotated mirrors, tails, absent boundaries, unknown controls and internal traffic.

`host-sdk-parameter-samples.jsonl` and `host-highrate-samples.jsonl` are verbatim
snapshots of the user's actual-source fake-SDK fixture outputs. They have no
corresponding Native witnesses. Provenance and SHA-256 hashes are recorded in
`host-sample-provenance.json`. These samples cover the declared Sony/Elite SDK
wrappers only; they do not establish SteamDeck partial-raw path completeness,
physical input, encoded control semantics, driver delivery or game consumption.

Run: `python -m unittest discover -s tools -p controller_signal_reverse_selftest.py -v`
