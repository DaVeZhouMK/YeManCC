# SteamDeck physical capability audit fixtures

Run from the YMCC source directory:

```powershell
node tools/hc_steamdeck_physical_capability_audit.mjs
node tools/hc_steamdeck_physical_capability_audit.mjs --selftest-only
```

The normal command writes a fresh run below
`G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SteamDeck/`.
`--out` must name an empty child directory in that authorized evidence area.
The default command fails if the real query lacks the explicit missing-reader
contract. `--allow-query-gap` preserves a known pre-fix query difference while
still recording `queryContract: false`; it never marks physical capture present.

The script uses only local source and installed compilers. There is no restore,
package download, HC/HIDMaestro DLL load, HID/DeviceIO/Steam/OEM call or power event.
The only executed binaries are generated value-only test fixtures.

- `source-lock.json` pins the actual fixed baseline and its primary files.
  Every one of the 1,048 baseline files is checked against the real SHA index;
  metadata/index hashes, core files, project version and omitted index are checked.
  The frozen Git revision is UNKNOWN and is not replaced with a candidate revision.
- `source-graph.mjs` removes comments/strings before identifying calls, extracts
  balanced exact method/lambda bodies, and classifies connected, directional,
  physical Neptune paths. This is a conservative lexical source graph, not a
  language compiler, alias analyzer or runtime control-flow proof.
- `classification-tests.mjs` uses a working physical positive control, disconnects
  edges, changes origin/protocol/realm/capability, inserts target/declaration nodes,
  and supplies malformed graphs/results. No source grep-string mirror tests.
- `reviewed-physical-roots.json` pins reviewed native capture/WM_INPUT roots and
  Host output boundaries. A changed root or new Neptune protocol identifier is
  a review-required failure, not permission to retain an old absence conclusion.
  Review code and recorded source bindings before updating those pins.
- `MappingHarness.cs` compiles the exact fixed HC Tick, callback, Open, Close and
  Gone methods. Input events are already-decoded dictionaries; the transport is
  fake. Tests cover keys, all four rear buttons, pad touch/release/thresholds,
  IMU scales/orientation, guards and Lizard request order. Enum names from the
  missing dependency label seam fields only; **no numeric HID layout is implied**.
- `QueryHarness.cpp` compiles the actual production oem.keys.get lambda and its
  real catalog/helper code. Identity is controlled in memory. It compares actual
  return/log values, log-off behavior and a legacy family control. There are no
  fabricated SteamDeck capability answers in the harness.

The missing `../steam-hidapi.net` primary implementation is reported as unknown.
Neither SteamDeckTarget nor BuildSteamDeckState is a physical packet oracle.
The evidence includes source hashes, generated compile units, logs, graph,
production source scan, source slices, actual query JSON and contract.json.
Source/consumer evidence is not physical acceptance. Hardware verification remains
false for every capability. No production file or existing test is changed.
