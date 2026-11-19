# gyro.game-haptics sidecar

This registration runs the unchanged `tools/gyro_game_haptics_selftest.ts` against `src/gyroSimulation/haptics.ts`. The eight original cases use injected memory native callbacks and browser-actuator ports; no browser is started. This proves the named haptics producer contracts only. It does not prove `GyroSimulationWindow` SFC exit, downstream hardware neutral, or physical vibration.

The single descriptor in `global/adapters.mjs` keeps the normal TypeScript adapter's 30000 ms budget, empty arguments and desktop/full profiles. Its completion contract requires the original `Game haptics selftest: 8/8 passed` line. Exact source dependencies are the original producer, `src/gyroSimulation/haptics.ts`, `src/bridge/ipc.ts`, `tsconfig.json` and `package.json`.

Compilation, source tracking, outputs, complete tools and completion verification are the existing `global/runner.mjs` contract. The runner automatically adds the six `BUNDLED_AUTHORITY_SOURCES`: runner, bundled-node-tool-authority, snapshot-node-tools, source-snapshot, battery_ui_native_recovery/snapshot-node-binding and execution-completion/index. No compiler or authority is implemented in this sidecar. Original G Node/package files are reused read-only and their complete graph is verified before and after execution; product inputs stay repository-relative, tools and bundle/map outputs remain artifacts.

The D validation view selects original bytes by the B42 manifest. Its historical creation FAIL is preserved; this finite view has `sourceApproval=false`, does not approve the complete snapshot or current G, and does not modify G/B42/B43. Only the adapter and this README belong in the formal sidecar. The historical receipt maintenance control is retained at the explicit Evidence entry below; never merge fault views, launchers or delivery evidence.

The two D-only faults each change one literal in the real haptics source, leaving the producer untouched: stop keeps active -> the original H5 post-stop effect count rejects 4 instead of 3; the late native reply emits strength 1 -> the original late-exit last-strength assertion rejects 1 instead of 0. Compilation or prerequisite failures are not detections. Case names/order, original assertions and budget stay intact.

The original exported `runAdapters(getAdapters(root).filter(a => a.id === 'gyro.game-haptics'), options)` is the candidate acceptance entry; no full CLI inventory is required for this sidecar. The delivery's owned launcher records the original adapter, exact Node command/PID/exit, raw streams, source/tool graphs and generated entry/map identities. The explicit historical receipt maintenance entry below checks those positive and two fault receipts, maps stack assertions to the original case names, and reuses the generic tool/output verifier. It does not execute the producer or compile another model. Evidence-root is the delivery evidence directory containing positive and both named fault runs; SOURCE-IDENTITY.json, FAULT-MANIFEST.json and capture/ are adjacent delivery metadata.

No UI/model/TDP expansion, installed App/Host/Steam, device, network, actual vibration or OS sleep is involved.

Historical receipt maintenance is deliberately outside standard `node --test` discovery. Run it only against the matching original delivery and source repository:

```powershell
& 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe' `
  'G:/YeManCC-Work/Testing/YMCC/Evidence/GyroGameHaptics-Maintenance-Relocation-20261109/receipt-maintenance.mjs' `
  --evidence-root '<original-delivery>/evidence' `
  --repo-root '<matching-original-source-repository>'
```

`--repo-root` must be absolute and selects the original source view and its three verifier imports. `SOURCE-IDENTITY.json`, `FAULT-MANIFEST.json` and `capture/` remain adjacent to the original evidence directory. The archived verification body, eight producer cases, two fault checks and historical fixed 26/749/753 counts remain unchanged. These counts bind the original epoch; do not substitute current graph counts or treat this maintenance script as a generic current-source test. A source, tool or receipt mismatch keeps its original rejection. Original receipts are not rewritten or requalified by relocation.

This relocation was checked only for syntax, exact preserved source body, references and discovery names. The historical maintenance script, complete tool graph and original business producers were not executed.
