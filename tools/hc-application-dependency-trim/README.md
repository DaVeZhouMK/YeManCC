# HC application companion trimming experiments

This tool never edits the production runtime. The original 109-file manifest is locked to SHA256 `65a3ed817b38ba36ef974d9c005bd4ca7057b222efa920d25a4fa16e74b2eb18` and HC SHA256 `08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583`.

`build-candidates.ps1 -InputRuntimeRoot <preserved-original-runtime> -OutputDirectory <new-evidence> -Mode leaf -Profile extended` builds the offline Cecil inspector and runs installed ILLink 10.0.11. It snapshots and checks every input file. The focused profile tries 32 app libraries. The extended profile tries 66 and fully roots reflection-sensitive network models, Sentry, TaskScheduler, Serilog, configuration, options, logging, dependency context and serializers. Every unchanged managed consumer is rooted in leaf mode so helper-library calls from immutable shared originals are retained. Device mode is a separate experiment and must not be promoted over another agent's HC work.

`create-overlay.ps1` accepts the successful candidate record and copies only its named replacements/removals over a new original snapshot. This overlay deliberately retains the old manifest: it is test evidence, not a shippable runtime. The main integration owner must promote individual reviewed files and rebuild the authoritative manifest/profile normally.

The inspector subcommands are:

- `auditcom <complete-original-input> <report> [roots.xml]`: metadata-only ABI inventory. Protect complete assemblies containing ComImport/Guid+InterfaceType, COM-visible/generated contracts, native imports, annotated or native-signature delegates, and supplied inherited interfaces/implementations. Dependency references are listed separately; unrelated dependency assemblies are not recursively protected. Missing external references are reported, not declared satisfied.
- `verifycom <original-input-or-dll> <candidate-input-or-dll> <report>`: require exact hashes and identity for every protected original assembly, with ordered interface/callback diagnostics. Missing and changed assemblies fail, regardless of which slots managed code calls.
- `selftestcom <new-output>`: emit metadata fixtures, reject deleted unused COM slots, callback-signature/implementation changes and missing callback libraries; verify unmarked inheritance/base contracts and avoid protecting unrelated dependencies. Inspected code never executes.
- `scan <runtime> <json>`: file/hash/assembly/resource/PInvoke inventory plus external metadata and IL edges. Framework/Modern internal edges are omitted to keep evidence bounded.
- `roots <runtime> <repo> <xml>`: all Devices and nested callbacks, named Host/Setter reflection types, every Factory field's manager type, MotherboardInfo, profiles, Shared.LogManager. App paths are fields only.
- `verify <before-hc> <after-hc> <json> [root-json]`: exact device and optionally reflected-root method bodies, fields, constants and native imports; framework forwarded types resolve to their actual definitions before comparison.
- `contracts <runtime> <target-names-json> <report> [shared-bin]`: resolve all cross-library type/member/export references into the selected libraries. With shared-bin, verify its sibling-parent manifest and include those immutable shared originals as sources and resolution authority.
- `leafverify <before-runtime> <after-runtime> <targets-json> <report>`: compare every retained method and native structure layout, including generic method arity.
- `surface <before-runtime> <after-runtime> <targets-json> <report>`: compare complete original type/member/field/property/event tables and decoded attribute values for the reflection families. Only framework type-forward equivalence and BeforeFieldInit on types without any static constructor are normalized.
- `dump <assembly> <method-regex> <report>` / `strings <assembly> <string-regex> <report>`: bounded evidence extraction, not whole-library decompilation.

`RuntimeProbe` copies the existing software ACPI callback fixture from `tools/hc-devicecore-trim/probes/CallbackPathProbe`. Changes are limited to explicit candidate HC/Sentry pin arguments and an optional manifest-pinned shared authority. Its ACPI transport remains in memory and all non-framework native imports are denied. It never constructs IDevice or starts Host.

`SoftwareProbe` executes the actual Shared.LogManager with both saved production consumer JSON configurations. It changes only log destinations to its evidence folder, runs actual log levels and literal-brace handling, and checks real log files. It rejects non-framework native imports. It does not start Host, a controller, telemetry, network requests, devices or a driver. Optional shared resolution is restricted to the eight manifest-pinned source originals.

Runtime probes are bounded software evidence. No standalone handheld validation package is emitted. Physical lifecycle, sleep, native protocols and normal final release validation remain the main integration owner's responsibility. Whole-runtime overlays and arbitrary scratch linker byproducts must never overwrite production.

## COM/interop compatibility gate (updated 2026-10-23)

The upstream-only/five-exception policy and mandatory restoration of all 34
ordinary libraries were explicitly revoked by the user on October 23, 2026.
Different DLL bytes are not a compatibility failure. The actual ordered COM
methods, inheritance, attributes, PreserveSig/marshalling, native import binding,
callback implementation IL, ordered fields, packing and native layout remain
checked against the complete reference input.

The synthetic regression includes a **smaller** derivative that removes an
unused managed type and unused resource in an ABI-owning assembly while keeping
its boundary unchanged. It must pass. Deleted unused COM slots, changed native
fields/packing/import entry points, broken callback signatures and removed
callback implementations must fail. These are metadata-only fixtures; no device,
constructor or inspected assembly is executed.

The current linker experiment uses conservative whole-assembly roots for
identified native owners. That rooting strategy is an experiment detail; the
verifier and overlay publisher no longer require byte-identical protected DLLs.
New trimming can use finer reviewed roots while retaining the same structural
negative cases. Dependency availability, reflection/dynamic users and actual
manager/Host regressions remain separate requirements.

The pre-overlay check runs the actual structural comparison before creating an
overlay. It then checks the complete resulting overlay. Candidate JSON booleans
and hashes are not a substitute for ABI checks; output and evidence directories
must be new. A real historical broken NAudio.Wasapi candidate must still fail on
its damaged contract, not merely because its SHA256 differs.

The historical 34-file restoration list is a regression coverage list, **not**
a command to replace those libraries with larger originals. Current output
identity follows the current profile's replacements and hashes. The test gate
checks actual ABI against the reference plus profile identity; a non-ABI companion
with zero native boundaries still has to match its declared profile output.

Run the standalone gate against explicit current payload roots:

    ./test-com-gate.ps1 -RuntimeRoot <current-runtime> -UpstreamRuntimeRoot <frozen-reference> -KnownBadWasapi <historical-bad-audio> -OutputDirectory <new-evidence>

Use the existing fan simulator for actual manager construction, SharpDX callback
and Host lifecycle consumers before promotion. Do not restore old failed trimmed
files or substitute a historical whole-runtime overlay for the current mainline.
