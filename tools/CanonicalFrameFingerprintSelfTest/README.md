# CanonicalFrameFingerprintSelfTest

Offline .NET 10 / Python 3 comparison of the actual saved CanonicalFrameHash and AppendFrameHashBits functions against the production memo helper. No Host launch, vendor assembly execution, device access, report submission, or upstream code execution occurs.

Run from G:/YeManCC-Work, using a new evidence directory for each run:

```powershell
python Mainline/YeManCC-source/YeManCC/tools/CanonicalFrameFingerprintSelfTest/run.py `
  --before G:/YeManCC-Work/Build/Validation/VirtualGamepadPerf-20261014/before/InputHost/Program.cs `
  --output G:/YeManCC-Work/Build/Validation/VirtualGamepadPerf-20261014/upstream/fingerprint-evidence/new-offline-run
```

Both paths are required. The output directory contains generated comparison functions, fault fixture, build/run logs, results, compiler outputs, CLI home, package cache, and temporary files. Keep existing evidence directories unchanged.

The runner extracts the two real saved functions, changes only the SHA tail for the measured candidate, and checks that the actual integrated function matches it (including the permitted comment update). The fault fixture is the exact helper source with only its class name and SHA call target replaced; it adds no production hooks.

Coverage includes random IEEE bits, signed zero, subnormals, NaN payloads, infinity, CurrentCulture buttons and Unicode negative signs, per-byte key mutation, one-entry ownership, failed SHA, tampered normalized hashes, and eight separate threads. Tampered-hash checks cover the unchanged comparison expression; this tool does not execute the complete command/ACK protocol.

Benchmarks use 30,000 warmup calls and nine alternating-order samples of 100,000 calls per implementation/scenario. The dynamic gate fails above 1.10 median candidate/before ratio. Fresh-thread first calls separately show cold allocation cost. Calls are unpaced: arithmetic 125/250 Hz projections in results are not measured real system CPU savings. Inspect raw samples as well as medians; runtime tier transitions can affect early samples.

Accepted evidence is upstream/fingerprint-evidence/run-2-limit10, with 222,324 assertions and a worst dynamic median ratio of 1.0322. Separate Host source-snapshot compilation verifies compilation without launching the application.
