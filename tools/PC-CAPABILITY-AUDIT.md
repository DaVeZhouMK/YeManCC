# PC/OEM capability audit tools

Read-only source audit. These tools never load HC assemblies, run OEM code, or write hardware.

- `pc-capability-audit.mjs` verifies pinned source bytes against SHA256 and Git blob SHA1, builds a syntax extractor, runs five existing domain audits against the in-tree HC baseline and a separately pinned upstream snapshot, and emits JSON/Markdown inventories.
- `PcCapabilityAudit/` uses Roslyn from the installed .NET SDK (net10.0); no NuGet dependency or device assembly is loaded. It records actual C# classes/namespaces, base aliases, `GetCurrent` construction sites, constructor flag operations, runtime mutations, method hashes/anchors, OEM chord operations and hardware parameters.
- `pc-capability-audit-selftest.mjs` checks inheritance reset, conditional flags, callbacks, comments, namespace aliases, unknown source expressions, empty/missing JSON sources, and invalid gyro mappings. Output goes under workspace `Build/Validation/PcCapabilityAudit-20261014`.

Run from the source root:

```powershell
node tools/pc-capability-audit.mjs --references 'G:\YeManCC-Work\Docs\Research\PcCapabilities-HC-CTW-20261014\upstream' --out 'G:\YeManCC-Work\Docs\Research\PcCapabilities-HC-CTW-20261014\results'
node tools/pc-capability-audit-selftest.mjs
```

The references directory contains the official repository/tree/release API snapshots, downloaded sources, and `source-manifest.json`. No network refresh happens implicitly. The orchestrator requires Node, PowerShell 7 (`pwsh`) and .NET SDK 10 on PATH. Repository docs link to the dated report; they do not promote an upstream count into runtime support.

Limits:

- Constructor `minimum`/`possible` arrays describe source declarations. They are not firmware results, device availability or lifecycle validation. Conditional `=` drops the inferred minimum conservatively. General interprocedural control flow, preprocessor configurations, multiple constructors and arbitrary expressions are not fully evaluated; raw operations remain available for review. Unknown constructor capability expressions cause failure.
- The audited HC OneX battery wrapper returns the virtual `IsBatteryProtectionSupported` result or false. The report excludes a constant-false override only while that exact return structure remains present. It preserves the raw inherited flags alongside the exclusion.
- Native identity literal matches do not include classes assembled by string concatenation. Missing a literal is not an identity bug; fixtures and the production resolver must be read together.
- Legacy lighting/key/input audits have narrower parsers. Their PASS is limited to their assertions. The syntax inventory is the class census; auxiliary `instantiables` counts are not substituted for it.
- JSON gyro lookup follows HC filename and class inheritance, not the JSON `DeviceClass` label. Metadata mismatches and unreachable filenames are distinguished. Windows sensor-property formats and real sensor axes remain a separate review.
- Upstream audit differences stay recorded as FAIL in their domain logs. The orchestrator exits nonzero for failed current-baseline gates, reference integrity failure or sources changing during the run; an upstream difference is an audit finding, not a product regression.
- CTW public default-branch source, archived 0.4.0 source/helper evidence, and 0.4.2 release identity are separate. UI property defaults do not establish helper support.

## Current locked HC execution checks

- `node tools/pc80-identity-selftest.mjs`: rebuilds the syntax inventory; executes the locked HC factory switch with inert stub classes and extracted production C++ resolver, matrices, lighting tables, OEM catalogs and profile selection. Checks all 80 factory classes, explicit legacy aliases and empty/unknown identities. It never loads HC or probes devices. An unexplained mismatch returns nonzero.
- `node tools/pc80-identity-mutation-selftest.mjs`: reintroduces the four fixed defects into isolated source fixtures and requires each to fail the real-function comparison.
- `dotnet run --project tools/LightSetterSelfTest/LightSetterSelfTest.csproj`: links production LightSetter, invokes only action/signature/exit-code methods with fake devices, and rejects false or partial writes as success. No production factory or Run entry point is called.
- Output: workspace `Build/Validation/Pc80-Software-20261014`. The 80 includes generic ASUS and DefaultDevice. Native CPU/year class normalization is explicitly recorded and checked for matrix, lighting and OEM catalog/profile equivalence; the real HC hosts still resolve exact classes.

These gates establish software behavior and payload consistency. They cannot establish physical charge cutoff, fan ownership acknowledgement, real input consumption or firmware support without those devices.
