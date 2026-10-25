# YMCC device-core runtime (2026-10-18)

The normal source build/package chain uses `YMCC-DEVICECORE-20261018-v1`.
This is a headless derivative of the frozen local HC 0.32.4.0 binary, with
existing EC/device protocols and local fixes. It still uses HC code. No CPU or
power improvement, installed deployment, or physical hardware validation is
claimed.

Current checked inventory: 86 runtime files including the manifest, 14,358,887 bytes.
The earlier 11,849,647-byte / 41-replacement layout was partially reverted; see
`interopAbiRepair` in the profile. This is an inventory description, not approval
to restore large files during export.
`profile.json` currently declares 7 generated replacements, 35 exclusions and eight exact
external shared dependencies. `upstream.runtime.json` is the original frozen
manifest. Retained files come byte-for-byte from the hash-checked development
input under `deps/hc-runtime-upstream/`; that input must never be exported.
The profile, generated runtime manifest and binary hashes govern actual output.
Licensing, provenance and third-party notices remain required.

Changes already integrated:

- HC resources were compacted while retaining device data and hardware protocol
  code. The passive SDLController timer uses BCL QPC; zero-handle touchpad queries
  return zero before the native bridge, whose nonzero path is preserved. The HC
  DLL remains 2,774,016 bytes / SHA256
  `08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583`.
  The later 2,238,976-byte HC type-pruning experiment is NOT approved or included:
  dynamic reflection/serialization roots remain insufficiently classified.
- SDK projection: 1,074,688 bytes. The latest pass removes 5,088 unused private
  compiler RVA constants while retaining all 1,892 SDK metadata types, all
  10,396 methods, properties/events, retained field data, native layouts and
  dynamic WinRT helper surfaces. The existing modified SDK already failed its
  original strong-name verification before this pass; this output must not be
  described as an original Microsoft-signed file. WinRT.Runtime remains the
  original 528,944 bytes with its original valid strong name and certificate.
- Modern/Controls: 820,224 bytes combined. After resource/type/table trimming,
  the latest pass removes 54 unreachable methods; 4,642 retained method bodies,
  type shapes, WPF dependency properties, callbacks and interface maps match
  the preceding approved derivative. Existing development strong-name identity
  is retained; it is not publisher authentication or an Authenticode signature.
- The two HC JSON metadata/config files are compacted without changing any
  parsed value, dependency entry, version or logging setting (24,411 bytes saved).
- Eight identical local dependencies now resolve through the explicit
  `PowerControl/ymcc-shared-runtime` contract. Both consumers and actual installer
  validation reject missing, altered, mislocated or duplicate copies.
- Application and transport leaves were individually trimmed or retained after
  complete retained-IL, cross-library reference and ABI checks. Reflection,
  logging and serialization families without sufficient gain retain original
  bytes. The 109-file attempt ledger distinguishes experiments from promotion.
- Unconsumed HC Hook/XInputPlus, GPU/JSL/KX native payloads, duplicate framework
  assets, debug CRT, VIIPER and native SDL3 were removed. SDL3-CS retains the
  actual managed ABI needed by HC; it is not a fake hardware-success adapter.

Use only the approved FanHost/LightSetter headless device lifecycle. Do not
start HC App/UI, ProfileManager.Start, UpdateProfileWrapper, Hook injection,
HC controller/virtual-output services, VIIPER targets, GPU manager lifecycle or
arbitrary diagnostic types. Exact forbidden entries are recorded in the profile
and generated manifest. Full retained HC type loading and reviewed software
constructors pass; this does not qualify arbitrary HC UI or physical devices.

Build a new empty output:

    python tools/hc-device-runtime/build-runtime.py --output <new-empty-directory>

Verify an existing output:

    python tools/hc-device-runtime/build-runtime.py --check --output <runtime-directory>

Normal scripts call `Assert-YmccDeviceCoreRuntime` from
`tools/hc-device-runtime/verify-runtime.ps1`. It verifies the frozen manifest,
profile identity, every retained/replacement hash, external shared contract and
exact recursive inventory. The exporter rejects old files, omissions, changed
bits and mismatched manifests; it does not require development inputs in the
release package. Runtime selftests exercise accepted and missing/altered/extra/
path-escape/manifest-tamper cases in scratch copies.

Latest reproducible tools/evidence: `tools/hc-metadata-compact`,
`tools/hc-ui-second-pass`, `tools/hc-winrt-second-pass`,
`tools/hc-shared-device-runtime`; rejected HC experiments are retained in
`tools/hc-core-second-pass`. Frozen input pins are intentional historical inputs,
not an instruction to replace the current approved runtime with an old overlay.

The ordinary deliverable remains the full `YeManCC.zip`. No separate handheld
validation package or physical-device execution is required by these tools.

## Compatibility repair and mandatory runtime simulation (2026-10-18)

The historical ABI repair restored exact frozen upstream NAudio Core/Vorbis/Wasapi/WinMM,
WpfScreenHelper, Interop.IWshRuntimeLibrary, and WindowsAPICodePack Core/Shell.
These eight files are no longer replacements. The old short NAudio files in bin
are retained only for the hash-locked negative regression, not for generation.
The actual old library reproduces ManagerFactory -> MultimediaManager ->
IMMDeviceEnumerator.RegisterEndpointNotificationCallback / 0x80070057; the restored
runtime constructs all 13 managers on STA. Shell/shortcut/DPI interface ordering
had the same trimming defect and is restored with its paired dependency.

Use the existing simulator before promoting dependency changes:

    pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain fan -Mode connect -EvidenceDir <fresh-directory>

The gate combines real dependency constructor execution, bad-library rejection,
and the shipped mock Host public open/control/restore/close/natural-exit cycle.
It also runs from tests/run_all.ps1 and the global fan.runtime-connection adapter.
Type/MemberRef metadata and a null backend alone are not runtime compatibility
acceptance. Real EC/OEM/firmware/RPM behavior is still a physical-device boundary.

For a dependency-only normal full-package repair, repair-package.py accepts a
hash-verified base archive plus the exact generated runtime. It preserves native
build provenance, updates incoming runtime binding and every payload hash in
export-identity.json, and declares dependency-only derivation explicitly. It does
not pretend the inherited executable is a fresh build of changed sources. A stale
base export inventory is rejected before producing an archive.
## Profile-governed DLL replacements (2026-10-23)

The generator, standalone verifier and package-repair tool no longer restrict
DLL derivatives to five hard-coded assembly names. Any retained upstream entry
may select a replacement explicitly declared in `profile.json`, provided its
source bytes, declared size, SHA-256, generated manifest and final exact inventory
all agree. Entries without a replacement retain their frozen baseline pins.
Missing, altered, extra or mislocated files remain errors; shared dependencies
still follow their independent pinned contract. Identity acceptance is not COM,
callback, lifecycle or physical-device acceptance.

The mainline now selects 34 individually pinned compact derivatives from the r4
candidate, in addition to the seven established replacements. The official
generator produces the standard 86-file runtime, without analysis-only shared
copies. It saves 1,736,120 bytes against the restored upstream files. The
773,120-byte difference from the historical compact reference preserves actual
native/COM layout and SharpDX shadow roots; exact reviewed hashes are recorded
in export-baseline/approved-20261023-compact34.json. The original reference
profile and manifest remain immutable.

The 24-case ABI regression resolves framework facade scopes to their actual
definition while still rejecting changed signatures, native slots, packing,
fields, import entry points and unresolved/unrelated assembly owners. The
standard five-phase fan gate passed against the newly generated runtime,
including all 13 real manager constructors, known-bad audio reproduction, full
SharpDX callbacks, and public mock Host natural shutdown. Historical five-DLL
negative fixtures are stored separately in regression-fixtures/pre-abi-repair;
they are not production inputs. Full Build/Package/Verify and final exported
archive checks remain required. Software checks do not prove physical hardware
behavior or authorize installation/publication.
