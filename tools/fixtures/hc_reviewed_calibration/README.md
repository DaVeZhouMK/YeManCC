This fixture executes current native calibration file/key/load/store/restore
functions and real GamepadMotion offset/mode/process exports. Model/source
setup comes from the actual extracted consumer fixture. Only LOCALAPPDATA is
redirected for the child process into the evidence directory. No installed
calibration file, sensor, controller, Host or device operation is used.

The existing `tools/hc_reviewed_calibration_selftest.mjs` entry also covers the
reviewed GPDWin5 recipe. Reviewed model counts follow the oracle's exact selected
model set, with Win5 explicitly required. Its bare device-ID bias is rejected and
left untouched; default and explicit restores must clear the actual DLL offset
and trusted state until their own current-recipe record exists. Explicit-source
cases keep the same bound reviewed recipe while checking independent keys,
profile and lease rejection, old-matrix rejection, and return to the default
offset. No old bias is transformed or automatically migrated. A user with only
the unqualified historical record needs one new stationary calibration.

To consume a fresh actual-consumer run and frozen main, use the same runner with
`--consumer <actual-consumer-directory> --source <frozen-main.cpp> --out <fresh-Evidence-directory>`.
Omitting these options retains the historical defaults. An existing output
directory is rejected, files use exclusive creation, progress snapshots have
separate names, and the terminal RESULT.json is written once. The context wiring
extracts the actual PrivateStandardApiBound body; it does not replace that guard
with a fixture approximation. Consumer generated-source and oracle hashes remain
bound to its original source-bindings.json receipt.

`--prepared-consumer-only` additionally requires all three explicit path options
and accepts only a `generated-only` consumer receipt. Its frozen main SHA256 must
match that receipt; generated-source, extracted-definition, oracle, DLL and final
input-identity checks remain active. Failed, running or merely prepared receipts
are not accepted. This mode still compiles and executes all of this calibration
consumer's assertions in Release and Checked. Its report marks the consumer full
producer `NOT_RUN` and not accepted; calibration success does not turn that
producer or any retained historical failure into a pass.

The runner verifies every generatedInputsAtCreation entry by its full relative
name and creation hash, then requires that set to equal the consumer's actual
quoted-include closure (json.hpp remains the explicitly captured external header).
Those original bytes, including unified_binding.cpp, are copied with exclusive
creation; no directory scan supplies undeclared dependencies. The calibration
storage closure extracts the actual archive/read/shared-writer functions and
uses settings_baseline.h, as in the existing transaction consumer. Wiring these
dependencies does not claim its damaged-file or atomic-fault scenarios ran.

The Windows consumer view fixture opts into
`calibrationToolchainPreamble(vcvars, {boundInheritedPath:true})`. It limits PATH
inside its child batch before vcvars runs, then retains the paths supplied by
vcvars and the existing compiler/SDK settings. This avoids inherited PATH
overflow during preparation. Other callers keep the original default preamble;
the option changes no global environment or Release/Checked assertions.
