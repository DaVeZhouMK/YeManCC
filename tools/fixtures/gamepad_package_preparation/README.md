# SDK 2.0 official startup preparation fixture

The fixture compiles and exercises the shipping `Program.InspectCurrentPackage`,
`DecideForPreparation`, and `HidMaestroPackagePreparation.Execute`. The approved
original SDK is loaded for metadata/resources only. Every preparation call in
this fixture terminates at `FakePreparationRuntime`; it never invokes a real SDK
method, setup main, device, driver, service, certificate or registry operation.

`--check` must issue zero SDK method calls. `--prepare` for exact known old,
partial/current packages uses the SDK 2.0 official deploy policy after identity,
startup owner, elevation, clients and initializer mutex checks, then repeats the
observations immediately before the single call. Unknown/foreign packages never
enter that call. The original empty-store first-install decision remains.

The public FullDeploy shim discards the official outcome. Shipping validates its
exact nine-byte IL forwarding contract under the pinned SHA/MVID and calls its
single actual `DriverDeploy.Run(null)` terminal once, preserving the real
`HMDriverInstallOutcome`. It does not stitch together signing/catalog/install
steps or invoke Everything cleanup. Actual deployment, coexistence and upgrade
policy remain in the unmodified SDK.

The tests distinguish AlreadyCurrent, Installed, Upgraded, KeptNewer, changed
owner/client/identity, failures/defer, and unproven postconditions. A newer stored
version is preserved; it is not proof the live driver's protocol serves this SDK.
A completed store preparation is not proof a live device was rebound. Existing
or in-flight owners are explicitly reported as binding unproven.

The protected raw `driver` directory and signed `package` directory are observed
independently with real private file IO. Raw driver staging intentionally needs
no generated CAT. CAT file presence does not prove signature trust, catalog
membership or device binding.

The runner separately compiles (1) original old name-only readiness code, which
must reproduce its false ready; (2) the preserved earlier SDK200 implementation,
which must reproduce its wrongly blocked prepare. Compiled mutations catch old
version acceptance, the invalid raw-CAT requirement, bypassed official prepare,
and skipped postcondition checks.

Run:

    python tools/gamepad_package_preparation_selftest.py --out ABSOLUTE_DIR --legacy-source ORIGINAL_PRE_FIX_Program.cs --prior-source-dir PREVIOUS_SDK200_SOURCE_ROOT

The independent official policy fixture under `gamepad_official_policy` binds
actual official selector/lock source to private fake terminals; no system SDK
installation is performed by either test runner.
