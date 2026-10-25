# Decky mirror host consumer

ID: `plugins.decky-mirror-host-consumer`. This additive leaf reuses all four original software producers without copying, reducing or rewriting their assertions: host 8, readonly 6, client 49, connection probe 7. It does not reopen the fixed HC 24 cases.

The production consumer is `startDeckyMirrorHost` with the original relay, actions and store, and the shipped `MirrorClient`. App.vue's non-standalone startup call and the connection producer's native restoration checks are recorded as static evidence. Main App, Host Main, Steam, OS sleep, devices, SDK/driver/PnP, installation and physical acceptance are outside this leaf.

Run after integration:

```powershell
node tools/ymcc_sim/global/decky-mirror-host-consumer/run.mjs --evidence-dir D:/YOUR-OWN-FRESH-EVIDENCE
```

A D candidate can validate unchanged stable product sources with `--repo-root ABS_B40 --fixed-input-bindings ABS_B40_BINDINGS_JSON --evidence-dir ABS_FRESH_OWN_ROOT`. The default product root is the repository containing this module. The optional fixed binding is checked against the supplied manifest SHA, tool binding SHA, original 26-package/749-file graph identity and each consumed package member. The leaf separately hashes the 142 members of the three consumed tool packages plus the actual Node executable. It does not claim to have reverified or loaded all 749 tools. Both tool inputs and sourceHashes have a local 640-entry ceiling; shared runner budgets are untouched.

Outputs use a fresh evidence directory. Original scripts execute directly from the explicit repo with their real paths; no product tree copy or path masquerading is used. `YMCC_DECKY_ARTIFACT_ROOT` points to a distinct per-suite artifact directory. Source overrides are cleared for normal runs. For faults the existing fixture override reads an honestly identified, case-owned TS file. No shared fixture patch is necessary on B40.

Each suite preserves stdout, stderr, natural exit, exact original JSON, source reads/ESM loads and actual transform input/output. The compiler observer observes the original transformSync calls. Only the pinned esbuild binary's memory service invocation is allowed; its synchronous API completion is recorded, with PID explicitly unobserved. No network or native addon/installed program entry is allowed. Source, candidate module and external tool hashes are checked before/after. External paths stay in tool/module identity records; sourceHashes contains repo relatives and exact case-owned absolute artifacts only.

Two single-method host controls preserve every other byte: ready never commits `hostReady`, and stop omits `relay.dispose()`. A control passes only after successful original host transformation, natural Node exit 1 and the expected original business assertion at its original script line. Timeout, import/fixture drift, compilation error or another assertion is a failure of the control. Missing exports/unmocked imports are classified `SIMULATOR_CONTRACT_DRIFT`, preserving raw stderr; this module does not repair production code automatically.

Run the maintenance contract test with `YMCC_DECKY_TEST_ROOT` set to a fresh absolute directory you own, then `node --test .../contract.test.mjs`. Its checks target identity drift, source boundary/budget, unobserved inputs and false fault detection. Actual original business tests and precise product faults remain required.

Registration suggestion for the parent writer: import `getDeckyMirrorHostConsumerAdapters` from `./decky-mirror-host-consumer/adapter.mjs`, then append `...getDeckyMirrorHostConsumerAdapters(repo)` in the existing extra adapter collection. No extra-adapters file is included here. Revalidate from the parent's final frozen source before treating the integrated leaf as current. No full-product or HC fixed-batch completion claim is made.
