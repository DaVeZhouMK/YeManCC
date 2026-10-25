# USBIP setup evidence software fixture

Run `node tools/usbip_setup_evidence_selftest.mjs --out ABS_FRESH_PRIVATE_DIRECTORY`.
The existing global simulator may use `controller.usbip-setup-evidence`, arguments
`--out {outDir}/usbip-evidence`, and result file `usbip-evidence/RESULT.json`.
Only Node built-ins and the pinned existing .NET 10 SDK/framework are required.
The fixed existing inputhost-protocol/toolchain-inputs.json supplies dotnetRoot;
the absolute dotnet.exe hash and SDK version are checked, with no ambient PATH
or executable override. The spec is included in sourceBindings. NuGet
sources are cleared. All generated sources, build files, logs, private Windows
files, compiler temporary files and receipts are inside the new output directory.
An existing output directory is rejected without writing into it.

The fixture compiles the complete actual `InputHost/UsbipSetupEvidence.cs` class.
Four unique, reversible source substitutions replace only the private log root,
the trusted Windows root, `ReadRegistryValues` body, and `ReadCmStatus` body.
`Fixture.cs` provides a global `XboxSdkDiagnostics.LogLock` static lock carrier;
it does not compile the existing Xbox logger or enable its diagnostic flags.
`SOURCE-BINDINGS.json` lists exact original/generated hashes and substitutions.
The production snapshot flow, instance and Class path validation, SYS path
validation, reparse/final-handle validation, bounded file reads, SHA256, PE version
parser, UTF8 writer, shared-lock acquisition, budgets and rotation execute intact.
No real registry, CM device, SDK, driver installation, service control, Host Main,
Steam or input hardware code is executed.

The scope has at most 128 JSON lines, each including newline at most 1024 UTF8
bytes: two snapshots of at most 19 facts, begin/end, 64 Report rows, 22 ordinary
Stage rows, and one each of prepare-result / prepare-exception. Repeated terminal
stages and flooded Report/Stage calls increment dropped counts. Final scope-end
records counters and invariant-culture elapsed time; Dispose is idempotent.
The ordinary `input-host-xbox-sdk.log` rotates at 4 MiB to the same `.log.1` using
the existing logger's shared lock. IO failures are isolated and counted; an
unwritable destination cannot guarantee a physical final row.

Registered service/Enum/Class values describe registered bindings, not a loaded
kernel image. CM started/problem facts do not derive backend availability.
Only caller-supplied Stage availability is recorded. The SYS fingerprint and
VS_FIXEDFILEINFO version come from the same at-most-2-MiB disk byte copy, never a
loaded driver module or a second version pathname read. Missing/malformed
versions remain versionunknown; future versions are observations only.
The bundled installer version is 0.9.8.1 only for core SHA
8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c;
other core identities get unknown. This mapping never alters setup policy.
SDK-internal EnsureInstalled(null) migration has no observed callback:
migrationBranchObserved=false. No claim is made that migration guards first
appeared in SDK 2.0; guards also exist in 1.12.

This is software evidence. It does not validate a delivered R3 package, real
USBIP setup, live registry/CM, hardware availability or loaded-memory identity.
The result reports pass/passed/status, sourcesStillCurrent, source hashes,
assertions, explicit terminal replacements, commands and no-real-SDK/hardware
fields. Real user testing remains a separate validation step.

If the private output filesystem cannot create symlinks, that optional real
reparse-path case is recorded in skipped with the actual error; all lexical
injection checks, the real file handle check and bounded hash reads still run.

The existing scope-begin row records inherited HIDMAESTRO_KEEP_TRANSPORT,
HIDMAESTRO_TIMEOUT_SCALE, HIDMAESTRO_DIAG and HIDMAESTRO_TIMING before identity
fields, limited to 16/32/16/16 characters with <unset> explicit. Values are only
read; no transport/wait/diagnostic policy is changed. Truncation is flagged on
that row and counted at the end. The fixture temporarily sets and restores its
own process values to execute unset/non-default/overlong cases.
