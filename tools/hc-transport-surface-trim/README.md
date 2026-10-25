# HC transport surface experiments

This directory is the only new source write scope for the six remaining transport/RTSS attempts. Existing application trim tools are read-only dependencies. No production asset, profile, Host, driver or device is modified or started.

## Reproduce

```powershell
& tools/hc-transport-surface-trim/run-experiments.ps1 -EvidenceDirectory '<new-output-directory>'
& tools/hc-transport-surface-trim/verify-software.ps1 -EvidenceRoot '<same-output-directory>'
```

The default baseline is the preserved original 109-file application snapshot. Its runtime manifest is locked to `65a3ed817b38ba36ef974d9c005bd4ca7057b222efa920d25a4fa16e74b2eb18`. Each run separately freezes the current production manifest and files. Eight exact shared originals are copied from `deps/ymcc-shared-runtime/bin`, checked against its parent manifest, into the analysis snapshot. Their original absence from the split production runtime is expected. New tool restore uses an empty local package feed and installed ILLink/Cecil 10.0.11.

Each of six experiments links one target and roots every member of the other 92 managed assemblies, including the nested mixed RTSS assembly. Target roots additionally retain all value/native layout types, unmanaged delegate types, every PInvoke container and all constructors/destructors/Dispose/Disconnect/Close/Stop/Cleanup/ReleaseHandle/event accessors. Native ABI, every original PInvoke, protected lifecycle presence, managed method IL and remaining cross-library consumers are verified. Candidate DLLs are not executed as transport implementations.

`TransportSurface.dll` modes:

- `inventory <root> <json>`: metadata-only module/native/managed classification.
- `roots <original-dll> <xml>`: complete native shape, PInvoke and lifecycle roots.
- `nativeverify <original-dll> <candidate-dll> <json>`: original native types, fields/offset/pack/class size/marshalling/attributes, imports, protected lifecycle and embedded resources.
- `reverse <root> <target-names-json> <json>`: actual AssemblyRef and metadata/IL consumers plus dynamic reflection sites and matching literal names.
- `mutation <original-dll> <layout|pinvoke|lifecycle> <bad-dll> <json>`: deliberately invalid ABI/API fixtures. These are metadata-only negative controls and must never be executed. Mixed/native assemblies are refused by this writer.

Mixed C++/CLI RTSS produces ILLink IL2106: Link is invalid; it can only be copied or skipped. A zero process exit with a byte-identical RTSS copy is recorded as a rejected trim action and zero saving, never as successful native trimming.

Software controls reuse the original strict callback and logger fixtures without editing them. They run real HC software callbacks over an explicitly in-memory ACPI boundary and actual Shared.LogManager with the two production JSON files, with only evidence log destinations redirected. Non-framework native imports are blocked. A deliberate missing-HIDAPI control fails static dependency checks despite passing the narrow callback/log checks. Hence no-file callback success alone is not deletion approval.

When using the original inspector/logger's optional shared argument, use `deps/ymcc-shared-runtime/bin`; its parent contains `manifest.json`. Do not pass the flat packaged `PowerControl/ymcc-shared-runtime` folder. These experiments instead embed the eight exact original copies into their analysis snapshots and omit the optional shared argument.

The four positive leaf replacements and the two Bluetooth/ViGEm omission experiments are distinct alternatives. Only individually reviewed files should be promoted by the main integration owner, with normal manifest, consumer and packaging gates rebuilt. No standalone handheld validation package is produced. CPU/power and physical lifecycle are not measured here.
