# Actual GamepadMotion binary CPU selftest

Run from the repository with Node on Windows x64:

    node tools/gamepad_motion_binary_selftest.mjs

The runner compiles this private C++ helper in Release and Checked using existing MSVC/SDK paths. It copies only the SHA-pinned audited DLL into a fresh NativeBinaryExecution evidence child. It never starts YMCC or any device transport. All build/run artifacts and TEMP/TMP for child commands are inside that evidence run. It does not dump the full environment.

The helper verifies SHA256 independently with BCrypt, holds a read-only file handle denying write/delete sharing, and uses LoadLibraryExW(absolutePrivatePath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32). It resolves exactly ten audited unmangled functions and verifies their RVAs. No unknown decorated ABI or nonexistent readback is called.

Seven cases cover mode0/3 readback through GetCalibrationMode, offset roundtrip/zero weight, Manual compensation/zero samples and dt, two instances, 16 retire/recreate cycles, observed Auto trajectories, and Auto->Manual live offset retention. All created handles are registered before assertions, deleted before FreeLibrary, and the module list is sampled before/after. A process crash/timeout cannot claim successful explicit cleanup; it fails the run and the private child is terminated. Normal success requires created==deleted, no live target module, and the successful cleanup receipt.

Auto data are synthetic and not a physical truth oracle. Confidence/offset evolution is recorded without a convergence threshold or timeout-based quality assertion. Finite readbacks and instance isolation are checked. The helper includes no HID/WinRT/Host, settings/persistence, registry/service/network or real sleep calls. DLL/CRT initialization and dependency effects still occur and are not claimed absent.

reference/ holds exact fixed historical/a250 C# declarations; they corroborate this x64 ABI but are not the original native C++ implementation. Native source/build identity remains unknown. The binary Git blob is pinned by both HC trees. Production code and older tests are outside this test's write scope.

Retained first failure is explained in retained-failure.json: all-six-axis-zero input is an early no-op in this fixed DLL. The tests still call it, require finite outputs and retained state/offset, and separately verify zero-gyro/nonzero-gravity compensation. No production or binary change is involved.
