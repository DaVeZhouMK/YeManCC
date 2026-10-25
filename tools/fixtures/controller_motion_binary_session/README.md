# Controller Session/Owner and native binary integration sidecar

Run on Windows x64:

    node tools/controller_motion_binary_session_selftest.mjs

Both Release and Checked compile helper.cpp directly against the current production native/controller_motion_session.h and controller_motion_processor.h; frozen copies plus hashes are saved for reproducibility. The DLL remains the SHA e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9 audit snapshot. Existing binary runner/fixtures are read only, protected by pre/post hashes.

The platform loader/hash/module-observation code was copied from the successful direct binary fixture into this new private fixture, not imported as an executing runner. The helper independently BCrypt-hashes and read-locks the DLL, then loads its absolute evidence path with DLL_LOAD_DIR|SYSTEM32. Actual lifetime/process/read callbacks are forwarding observers; numeric processing and resource allocation/deallocation run in the real DLL. GetCalibrationMode is genuine native readback. Player/world ABI is corroborated by fixed wrappers and PE audit; Session outputs are checked against direct DLL reads.

scenarios.json states what is actual vs synthetic per case. Keys, timestamps, in-memory Calibration records, manual borrower actions and callback-pointer changes are controlled fixture inputs. No main registry/persistence/transaction or true PBT is executed. Supplied offsets can be restored across new generations; this does not prove complete native state restoration, firmware timing or auto convergence.

Three private header mutants check same-key dt continuity, invalidation/reconcile with real mode readback, and Owner key gating with native process-call observation. Normal exit1 + exact assertion + complete handle/module cleanup is required; crash/timeouts/compiler/runner failures do not qualify. Original production headers and all earlier tests are never edited.

All Session/Owner scopes end before unload, real native create/delete counts balance, and current-helper module lists cover before/after load/unload. A 30-second child timeout is enforced. DLL/CRT initialization and OS effects are not claimed absent. No normal YMCC app, device/HID/WinRT/Host, registry/service/settings/network or actual sleep call is used. Neptune remains deferred.
