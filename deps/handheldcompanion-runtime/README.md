# HandheldCompanion Runtime Source Baseline

This directory is the one controlled HC source baseline for future shared
runtime work. It is not a runtime payload and is not copied to
`PowerControl` by the current release flow.

`source` is an exact, cache-free copy of frozen baseline
`HC-BINARY-BATCH12-20260831`, with the Fan-owned MSI reconciliation described
below. The file list and SHA-256 values are recorded in `source-files.sha256`;
`HC-BASELINE.json` records the upstream project and core lifecycle identity.

## Fan MSI reconciliation (FAN-936-R2)

Under FAN-936-R2 this directory is the single controlled baseline, and it is
updated in place to match the pinned Fan HC candidate
(`Isolated/Tasks/Fan/hc-candidate-920v16`) so the shipped HC DLL is
reproducible from an in-tree source baseline. Only two Fan-owned files were
replaced: `Devices/MSI/ClawA1M.cs` and `WMI.cs`. The baseline id, upstream
project version, file count, and `HandheldCompanion.csproj` are unchanged. The
old/new hashes and the disclosed (not replaced) non-Fan deltas are recorded in
`HC-BASELINE.json` under `fanReconciliation`. Future fan, coordinator, and input
work must still compare to this baseline rather than introducing another HC
source snapshot.

`omitted-from-freeze.sha256` records 195 intentionally omitted `obj/` files
(NuGet restore and compiler outputs, 50,844,610 bytes). No HC source file,
resource asset, or current FanHost closure file was omitted. An absent asset is
recorded as a future import requirement, not treated as an HC behavior change.

The current mainline `PowerControl/fan-host` remains the validated fan resource
baseline. Its payload manifest is a separate deployment-integrity mechanism.
This directory must not be used to replace, trim, or relocate that payload until
a separately approved runtime-materialization stage.

HC upstream identity is based on a coherent source/project version and published
closure. YMCC hash gates prevent deployment drift; they are not claimed to be
HC scheduling behavior. Future fan, coordinator, and input work must compare to
this baseline rather than introducing another HC source snapshot.

Run `pnpm run test:hc-source-baseline` to verify this source tree. The check is
read-only and does not load HC, start FanHost, or access hardware.
