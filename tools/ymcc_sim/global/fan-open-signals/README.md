# Fan Open Signals

`run.mjs` builds the current `FanLab/real-host` candidate into a fresh evidence directory, derives the complete MSI parent Open fixture from the pinned HC source, and executes the old Host and the new candidate through inert leaf IO. The original 25 behavior cases and the C# consumers are unchanged.

The default negative control is stored in `fixtures/legacy-host-5e941/`: the exact pre-fix Host DLL (`5e941701...`) and Backend DLL (`3b2d584d...`), with SHA-256 and origin metadata in `provenance.json`. Both hashes are hard-pinned in the entry. This fixture contains no user settings or tokens. Installing a newer Host does not replace the negative control. `--legacy-host-root` can override its location, but still requires both exact old hashes; it must never point at the candidate.

Cecil is discovered from the dependency hint of the existing repository tool `tools/hc-application-dependency-trim/ApplicationTrim.csproj`. The runner checks that approved version in `NUGET_PACKAGES`, then the local user's `.nuget/packages` cache. It does not search historical evidence directories or download a new dependency. `--cecil-path` explicitly overrides discovery; an invalid override does not silently fall back.

The default HC reference is the repository's `PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll`. Use `--hc-assembly` for a different explicit candidate reference. Payload templates are read from the repository's `PowerControl/fan-host` rather than from the old installed Host. The new payload remains separate from the fixed old baseline and contains the executable, Host DLL/PDB, Backend DLL/PDB, deps/runtimeconfig, manifest and build dependencies.

```text
node tools/ymcc_sim/global/fan-open-signals/run.mjs --evidence-dir <fresh directory>

node tools/ymcc_sim/global/fan-open-signals/run.mjs \
  --evidence-dir <fresh directory> \
  --legacy-host-root <exact old baseline directory> \
  --hc-assembly <HandheldCompanion.dll> \
  --cecil-path <Mono.Cecil.dll>
```

Only those four flags are accepted. Unknown flags, positional arguments, missing or empty values, and duplicate flags fail before creating evidence. `--evidence-dir` is required. Missing baseline DLL/provenance, Cecil, or HC prerequisites produce an explicit `BLOCKED` receipt and exit code 3. Incorrect baseline identities produce `FAIL`, never a replacement negative control. The static descriptor opts into `blockedExitCodes: [3]` for the global runner.

`getFanOpenAdapters()` is exported by the pure static `adapter.mjs` and re-exported by `run.mjs`. Descriptor import/calls do not read the filesystem. Registration remains owned by the parent global runner.

The fixture preserves actual `IDevice.Open`, `ClawA1M.Open`, A2VM/CG3EM overrides, BZ2EM block 82, and relevant helpers. WMI, HID, UEFI, waits, release and disposal remain inert and counted. The original harness rejects parent opcode/call sequence drift and a base-only model.

`node --test tools/ymcc_sim/global/fan-open-signals/entry.test.mjs` checks portable baseline/Cecil discovery, strict CLI rejection, static descriptors, hard-pinned identity failures, and real BLOCKED receipts. Set `YMCC_FAN_OPEN_TEST_ROOT` to retain scratch evidence in a chosen workspace.

A PASS proves the bounded software consumers and complete source-derived parent model. Physical EC, firmware, HID, WMI and fan RPM behavior remain unverified. The entry does not start Host Main or install the candidate.
