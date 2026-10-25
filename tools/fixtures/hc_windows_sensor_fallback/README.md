# Standard Windows Sensor API ingress fixture

Run `node tools/hc_windows_sensor_fallback_selftest.mjs` from the repository root. It extracts actual native eligibility/start/stop functions and verifies cold-start/resubscribe/stop/poll wiring. Existing reader SDK mocks supply standard fields; no real SensorManager or HIDMaestro context is created. The real reader MTA thread, callbacks, current matrices and pinned existing GamepadMotion.dll execute in Release and Checked. Independent fixed HC recipes check all six numeric axes.

Model aliases are reconciled with the fixed current-source adoption plan. All 51 current YMCC classes representing the seven remaining Windows families execute the ingress/processing composition. This is source coverage, not hardware acceptance. MSI/ROG/GPD/controller families and a successful complete WinRT pair cannot enter the new source route.

Negative controls `--ignore-retirement` and `--mix-partial-winrt` must compile and produce executable failures only in their respective retirement/partial-source groups. Generation guards, standard-field restrictions, partial-source retirement, no timestamps/missing axes/denial/ambiguity and quarantine are covered. The driver rejects compile failures and unexpected groups as invalid controls.

The separate evidence-local CalibrationHandoffProbe reproduces an unfixed 33-class legacy restore/trust problem. Ingress passing does not establish calibration, actual Host/game consumption, physical axes or sleep acceptance. No new production HC DLL or deployment is performed.
