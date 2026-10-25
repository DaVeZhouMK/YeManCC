# Host owned-cleanup / pipe-retirement composition

This consumer compiles the complete actual `inputHostOwnedCleanupProvenLocked`
and `inputHostDisabledCleanupProvenLocked` functions with the actual pipe Runtime.
The explicit IO/fault leaves are taken from the existing pipe retirement memory
fixture; successful event/duplicate/close leaves use private real Win32 HANDLEs.
Host process, target/visibility, durable history and MSI-mode facts are explicit
memory ports. No Host, SDK, input device, service, Steam or driver is invoked.

The native `inputHostPipeRetirementProvenLocked` wrapper is extracted verbatim
when present. Before that parent edit is integrated, a clearly marked read-only
fixture adapter permits preliminary helper composition only: `pass` remains
false. Use `--require-wrapper` for integrated execution; it refuses a missing
wrapper. This does not execute full Stop/Clear/Start or approve a package.

Run from the repository with its existing captured MSVC toolchain:

```powershell
python tools/input_host_owned_cleanup_composition_selftest.py --require-wrapper
```

Only fresh evidence below `Testing/YMCC/Evidence/VC-Lifecycle-Integration-20261023`
is created. `--out` selects a fresh absolute directory outside source.
Captured compiler tools, actual compiler dependencies and searched linker
libraries are bound to the unchanged existing capture. Source changes during
execution reject the run. Old receipts are never rewritten.

Release and Checked execute real predicates for no owner; ON/OFF distinction;
a pre-adoption raw slot with Close failure and retry before/after first Runtime;
active lease / terminal cache with zero pending IO; event/original/duplicate
close failures and fault clearance; pending cancellation failure and late query;
contended snapshot; seal with failed close; and independent Host/target/physical
obligations. Every run requires a zero independent owned HANDLE oracle and the
same process HANDLE count before program exit. Explicit compiled controls remove
the upper pipe gate, use pending-only, mistake historical failed closes for
live debt, and restore unchecked raw-slot Close. The last control counts lost
ownership before explicit negative-only salvage; current positives require no salvage. Each control must compile and actually fail assertions.
