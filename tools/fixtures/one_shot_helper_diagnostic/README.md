# R114/R115 one-shot helper diagnostic Windows fixture

Run from the repository with:

```powershell
node tools/one_shot_helper_diagnostic_selftest.mjs
```

Default evidence directory: `G:/YeManCC-Work/Build/Validation/VirtualInput-HelperLease-R115-20261016/native-helper-diagnostic`.
`--output <directory>`, `--rounds 8..256` (default 96), `--vcvars <vcvars64.bat>`, and `--producer-results <completed C# results.json>` are optional. The default producer input is the sibling R114 `helper-diagnostics/offline/candidate/results.json`, independent of the selected evidence output.

The harness discovers MSVC with vswhere and compiles x64 C++20 twice:

- Release: `/O2 /DNDEBUG /MT`.
- Checked: `/Od /RTC1 /Zi /MTd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2`.

The fixture includes the real `native/one_shot_helper_diagnostic.h` unchanged. It launches only itself through real Win32 process/startup/standard handle APIs. Child modes emit synthetic stdout, stderr or no output; a bounded hold mode waits on fixture-owned named events. The parent proves process termination before consuming terminal output, and terminates only its exact private held child.

Exact production lease, Cycle, Settle, poll and ordinary receipt source spans are extracted into the evidence directory, hashed and compiled. Device data types, profile naming, hash calculation and lifecycle destination are inert fixture declarations. Cycle selects only the fixture image or a nonexistent path inside its private directory. Its CreateProcess boundary forwards to the real Win32 API except an explicit injected failure. No product/Host, Steam, SDK, device, driver, deployment or sleep action runs.

Coverage includes first/after-prepare exit8 records; exit0/empty output; prefix, field, type, stage, result and exit mismatch; malformed/truncated/duplicate records; numeric conversion and conflicting-key probes; NUL trailing bytes; exactly 2048 bytes and oversize output through 2 MiB; prepare/launch fallback; handle-list isolation with an inheritable event and a positive control; actual runtime timeout ownership, settlement and receipt consumption; retained failure lifecycle before later restore fields replace the helper; lifecycle sink exceptions; root-only no-spool policy; and actual Cycle attempt attribution across seven early failures.

Per-process TMP/TEMP are set only in the private parent to its own newly named directory. DELETE_ON_CLOSE alone removes spool files. The fixture removes only the empty directory it successfully created, never recursively deletes, and fails rather than reuses an existing directory.

A capture-free missing-image control records any first-call Win32 handle-count change. Four control failures, sixteen prepared failures, eight abandoned preparations and the resource matrix establish subsequent stability. This control must be reported separately from capture resource growth.

`source-binding.json`, `tested-source/`, per-configuration `build.log` and `results.json`, and `validation.json` preserve exact inputs and results. Source hashes are checked after each run; a concurrent source change prevents a passing aggregate. The process exits nonzero for defects and retains the evidence. A parser exception returning JSON null is rejected without requiring any specific error state. Only an accepted record with reported facts is treated as diagnostic evidence, and it never proves hardware success.

The producer replay selects actual `first/locate` and `after-prepare/locate` Facts (where present), preserves property order and PascalCase, adds the fixed prefix and CRLF, and emits the bytes from a C++ private child. It is a field-level replay of the completed no-device C# fixture, not a product C# process run. Generated producer input hashes, wire byte counts and exact record equality are retained separately.

R115 adds pending-operation reentry protection: the same live process/capture and target/hash facts survive a second request, and early new attempts clear prior exit/error/failure observations. The default evidence destination advances to R115 so a plain invocation cannot overwrite completed R114 receipts. Prior source and results remain frozen in their original evidence directories.
