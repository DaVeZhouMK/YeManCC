# InputHost terminal cleanup — bounded production patch, 2026-10-23

Production changes are restricted to `InputHost/Program.cs` Main terminal tail, RunPipeClientAsync termination, LogFatalExit kind expression, plus new `InputHost/HostTerminalCleanup.cs`. No native/global/USB configuration/SDK-worker change. `DELIVERY.json` verifies those exact source boundaries and all other pre-existing Host production files.

`Main` invokes one `HostTerminalCleanup.RunAsync` and clears only its stage observer in finally. Pipe branches return their origin outcome without calling ReleaseLocal. An accepted SHUTDOWN result carries its already performed release attempt into the same final settlement; it is not repeated on healthy shutdown. The release result is captured before reply IO, and retained across reply failure or parent cancellation. A failed accepted SHUTDOWN is terminal, with at most one extra attempt for an exact stop timeout. Invalid identity/protocol SHUTDOWN replies do not terminate the session.

Retry is synchronous, on the same continuation and original session, at most once. There is no Task.Run, sleep, target rebuild or second lifecycle state machine in the helper. Exact retry detail whitelist:

- `controller-output-stop:sdk-output-stop-timeout`
- `controller-output-stop:sdk-output-stop-lock-timeout`
- `steamdeck-idle-stop-failed:InvalidOperationException:steamdeck-idle-handoff:sdk-idle-clock-timeout`
- `controller-idle-cts-capture:sdk-idle-clock-timeout`

These are verified pre-Dispose barriers retaining the owner. Composite details, neutral/detach failures, SDK Dispose exceptions, identity/field/cancel/join exceptions and endpoint failures are not retried. First and final details remain in the receipt. Unresolved cleanup converts an otherwise-zero outcome to exit 7; existing nonzero transport/exception codes remain nonzero. Known permanent barrier failure emits `ManagedReleaseComplete:false` and `ResourceDisposition:owner-retained`; arbitrary other failures remain `release-unconfirmed`. No real process-exit fallback is claimed.

Both `terminal-cleanup-complete` and `terminal-cleanup-failed` use the log kind `resource-settlement`. Their actual result is the serialized receipt's ManagedReleaseComplete/ExitCode. Previous orderly/parent-lost/fatal mappings remain unchanged. `shutdown-ordered` is only a trigger/cause event, never final resource evidence.

Tests compile current complete HostSession release functions, unchanged production stop/receipt helpers with the existing IRuntime seams, pinned SDK 2.0 method fixtures, real owned BCL threads/CTS/events/locks, real pipes and owned helper parent process handles. The exact Main terminal try/finally and complete RunPipeClientAsync execute. Full production LogFatalExit executes too, with only its method name disambiguated and output directory redirected to fixture-owned evidence; assertions read the actual emitted JSON lines. The SDK binary is hash-checked, never loaded. PnP/backend/neutral/report-chain/diagnostic/USB terminals remain explicit fakes.

Fresh full regression:

```powershell
node tools/input_host_managed_lifecycle_terminal_selftest.mjs --out <absolute-new-evidence-directory>
```

Negative `--fault` values: `terminal-suppress-failure`, `terminal-no-retry`, `terminal-retry-sdk`, `terminal-repeat-shutdown`, `terminal-log-fatal`. Each uses the same assertions, must build, and must exit 1. The existing historical managed selftest is preserved as its old consumer, rather than silently changing old expected failures.

Other exclusive entries:

```powershell
node tools/input_host_managed_lifecycle_terminal_build.mjs <absolute-new-build-directory>
node tools/input_host_managed_lifecycle_terminal_old_replay.mjs <absolute-new-directory-under-terminal-patch>
node tools/input_host_managed_lifecycle_terminal_evidence.mjs G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/terminal-patch
```

The last entry seals the dated delivered paths, validates real negative rejection, source identity and exact production patch scope. It does not register tests or grant whole-goal acceptance.

Formal parent continuation must consume `input_host_managed_lifecycle_terminal_selftest.mjs` and the complete `current-006/inputs-before.json` closure. The old `inputhost_parent_lifecycle_selftest.mjs` binds `Task<int> RunPipeClientAsync` and mocks ReleaseLocal; production now returns a typed LoopResult and the final settlement is in Main. That old extraction is not the correct postpatch terminal consumer. No old/global file is changed here.


## 2026-11-05: opt-in retained-owner handoff to the original native exit owner

The existing permanent-output terminal case can now remain alive after its real
Main terminal block returns exit 7, with the original owner/output worker/CTS
still retained. This is an explicit uncooperative fixture wrapper. It does not
change the normal case list, module minimum, global IDs, or release defaults.

From the repository root, prepare once in a fresh core evidence directory, then
select the existing native scenario explicitly:

```powershell
node tools/input_host_managed_lifecycle_terminal_selftest.mjs --prepare-owned-child --case terminal-shutdown-output-fixed --out "<fresh-core-evidence>/managed"
python tools/input_host_native_exit_parent_selftest.py --expected-main-sha256 <reviewed-current-main-SHA256> --profiles Release Checked --scenarios unprotected-uncooperative --managed-retained-child "<fresh-core-evidence>/managed/owned-retained-child.json" --out "<fresh-core-evidence>/native"
```

Preparation only builds Release, with shared compilation/node reuse disabled and
the pinned runtime version/roll-forward policy. Its descriptor binds the actual
assembly, runtime files, complete existing toolchain set, source snapshots,
generated inputs and build receipts. PREPARED means zero executed cases and no
behavioral PASS. Changed inputs require a new matching preparation; unchanged
preparation can be reused for the affected native stage.

The child runs the original shutdown/output/fixed case with the actual native
parent PID. Before fixture cleanup, it exclusively creates and flushes a proof
containing the actual case result, retained resources, nonce, PIDs and process
creation identities. It never terminates the external native parent. The native
reader waits until it can acquire the proof after the writer closes, verifies
that proof against its real child, then executes the original production exit
owner. The existing assertions require force-once, observed process/primary exit,
pipe/local-owner return, checked observation-handle close and zero child Job
processes before successful close. No fixture gate relief or kill backstop may
produce success. Parent loss or the child's 30-second observation fallback is
non-success; the original native build/run budgets remain 60/30 seconds.

The SDK 2.0 method fixtures and real private BCL workers execute; the SDK binary
is checked but not loaded. Native request/visibility/physical endpoints remain
explicit memory ports. This proves the private retained-lifetime handoff and
native OS ownership return, not full Host.exe/production-pipe transport, arbitrary
permanent OS/SDK blockage, physical controller return, or all model behavior.

The 2026-11-05 sample passed Release/Checked with 444 observed assertions, plus a
wrong-nonce semantic rejection reusing the same Release binary without compiling
again. Preparation, the preserved first compile/metadata failure, repaired native
run and rejection consumed about 58.7 seconds of the one 300-second execution
account. That sample adds cross-consumer coverage; it is not a same-coverage
speed comparison or proof that complete publication finishes within 30 minutes.
Canonical receipts: `Testing/YMCC/Evidence/VirtualManagedNativeHandoff-20261105`.
Ordinary/Fast exports remain NOT_RUN; this opt-in entry adds no publication gate.
