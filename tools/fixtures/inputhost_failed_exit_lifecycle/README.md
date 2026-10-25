# Failed-exit lifecycle extracted-fragment regression

Run from the repository root, using a fresh output directory:

```powershell
node tools/inputhost_failed_exit_lifecycle_selftest.mjs --out G:/YeManCC-Work/Build/Validation/VirtualInput-BugPriority-R103-20261016/failed-exit-integration/next-run
```

The default baseline is `failed-exit-integration/before/native/main.cpp`; `--before` and `--root` can select another frozen baseline/evidence root. Existing output directories are rejected to preserve prior results.

The runner extracts the actual full Clear, Recreate and persona-transition predicate, and actual Stop ordered-command/wait/visibility and Start old-process/post-Stop/tuple/assignment fragments. The fixture supplies inert requests, liveness, EOF/cleanup and creation facts. It compares before/current behavior and compiles with MSVC Release and Checked, `/W4 /WX`.

It also reruns 16 isolated real Windows pipe cases per configuration using the unchanged production transport header. No real InputHost, SDK, input device, Steam, deployment or production mutation executes.

`result.json.pass` requires behavioral assertions, all five structural features, stable captured inputs, and passing pipe tests. A successful compile or five present source features is insufficient. Current failures are retained as failures; the runner exits nonzero.

The late-cleanup case keeps the requested persona, verifies that process exit alone still blocks creation, and executes the actual existing quarantine probe and lifecycle-clear functions. It rejects missing endpoint proof, failed physical return, and wake hold. Only full existing proof clears debt. The current common path then requires an additional explicit rearm through the extracted production settings gate before creation; this is not automatic continuation of the original request. If no production auto-recovery helper exists, the result says so. No ACKs or policy gates are fabricated. --source can select a frozen source snapshot; existing output directories remain protected.
