# Gyro sensor power and virtual-link consumer

Canonical ID remains `sleep.gyro-power`. Run only this registered consumer:

```powershell
node tools/ymcc_sim/global/cli.mjs --mode run --profile sleep --test sleep.gyro-power --out-dir G:/YeManCC-Work/Testing/YMCC/Evidence/<new-round>
```

The smallest producer entry is the same runner with its existing output-directory argument:

```powershell
node tools/ymcc_sim/global/sensor-power-consumer/run.mjs G:/YeManCC-Work/Testing/YMCC/Evidence/<new-round>/sensor-power
```

The normal variant retains all 31 original power assertions and adds eighteen virtual-link scenario groups. The three original faults still execute exactly the original 31 assertions. Three new mutations each execute only their selected existing virtual-link scenario; they do not repeat the real-time STA scenario. The runner validates exact scenario names, the original assertion names, targeted mutation counts and a required semantic failure. Unknown native case selections fail. There is no new canonical ID, parent gate or module minimum.

## Actual code and explicit ports

Nineteen exact production functions are extracted from `native/main.cpp`: the original thirteen, plus `inputCaptureConfigureVirtualGyroWake`, `inputCaptureNoteVirtualGyroReady`, `inputCaptureVirtualGyroWakeBusy`, `inputCaptureVirtualGyroWakeStep`, `inputCaptureVirtualGyroHostMayWake` and `inputHostNoteVirtualGyroAcceptedLocked`. The actual `InputFrameSubmission` structure also compiles, and the exact `readCurrent` function is extracted from `virtual_switch_verification_runtime.h`. The original STA message pump, OS suspend callback, display handler, unsubscribe, same-object resubscribe, queue admission and shutdown continue to execute. The real `sensor_virtual_link_wake.h` and `windows_motion_submission.h` implement the state and binding contracts. Each variant compiles a private copy of the new header; only a named mutation changes that copy. Source hashes and verbatim function bindings remain in `consumer-result.json`.

The effective enable argument and game-override owner admission are declared memory ports. Configure's actual context construction executes; settings files, configuration derivation, Host IPC and successful-start callers are not executed. The ACK helper executes actual frameSubmission `decide/source`, `readCurrent`, and `sameSource`; its frame-current/context/latest-source getters are memory snapshot ports. Unused feedback payload types in the actual frame structure are inert substitutes, and no production ABI claim is made. Note is invoked at its accepted-frame entry with a memory Host epoch and the configuration generation captured before the represented frame. The actual HostMayWake helper consumes memory fault-hold, power-revocation, intent-reset and exit-fence atomics. Input readiness, exit, external priority, output-wanted and power generation are memory atomics. The actual Busy helper consumes memory calibration enums, selection/probe flags and route-presence pointers. Sensor objects, callbacks, cache samples, readers, logging and inventory are memory ports. The cache contains both validity flags, receipt ticks, timestamps, binding generations and sample sequences; injected samples are not physical freshness evidence.

A scoped clock macro supplies exact boundary times only to the extracted functions in direct-method scenarios. The `sta-deadline` scenario sets that port to pass through `GetTickCount64`, starts the actual production STA with private Windows events, and waits for its real 750ms check and 1500ms verification. Both old polling switches are OFF, so another polling task cannot supply the wake. The STA is joined before its handles close. No YMCC, Steam or InputHost is launched; no real sensor, PnP, driver, sleep or game API is invoked.

## Virtual-link coverage

- OFF admission, repeated ready deduplication, 749/750ms boundary and healthy-pair no-op. A pair at exactly 250ms old is healthy; 251ms, future ticks, invalid flags, missing timestamps, sequence one and stale binding are rejected.
- Configuration replacement and OFF cancel old work; a newly admitted configuration receives its own settle window. Host, power, sensor generation and sensor-owner replacement invalidate old tickets.
- Actual accepted-frame helper: one current-lineage positive and fourteen rejections covering old config, inactive/mismatched Host, stale frame/neutral source, context, route, incarnation, continuity, ordering and stale reads.
- Commit rejects prior OFF, a closed Host/output predicate, duplicate tickets, and the original expiry boundary. A memory revoke hook closes output/configuration during the actual Step transaction: both same-object cleanup paths finish, while verification and retry remain forbidden. Uncommitted tickets cannot schedule verification.
- An ACK from an old configuration cannot admit work or consume current readiness. Tickets expire 5000ms after the original ready; verification preserves that expiry. A faulted Host blocks admission and cancels work even when its epoch is unchanged.
- Frozen sequence 431 triggers one same-object resubscribe, resets the old cache, then accepts sequence two in the new binding. Verification waits 1500ms. Persistent staleness records failure and leaves no repeating deadline, cold start or second attempt for the same Host/configuration.
- ROG and complete WinRT-pair scope; eight actual Busy-helper conditions: old Sampling, existing wake poll, requested calibration, active calibration, selection work, probe, selected default route and private projection.
- Exit, input not ready, external-priority suspension, display OFF and output not wanted block both ready admission and pending work. The actual unsubscribe cancels the deadline; a late Step cannot revive subscriptions.
- The real STA takes its deadline with an otherwise infinite idle wait, performs its finite verification, and shuts down with test-owned handles returned.

The three new private faults ignore the effective link switch, retain the pending old configuration, and resubscribe an already healthy source. Each must compile and fail its named unchanged assertion. The original faults still remove early power pause, remove display-OFF pause, and admit resume while OFF. Compile and execution limits remain 120000ms and 30000ms per invocation; the registered parent retains its own existing budget. Normal and fault execution costs are reported separately; additional compilation is not a passing runtime result.

Device-change hints still use the production weak binding/power scope and the actual unsubscribe/early-pause reset paths. The memory device stays outside the ONEX route; ONEX event/late-source cases remain in `sensorapi_inventory_selftest`. Software checks do not establish physical recovery, power-button latency, low-power residency or Host/game consumption.

The full `native/main.cpp` hash remains mandatory. Compiled main bytes and declared identity come from the same snapshot. End-of-run `sourceCurrentness`, `sourceDrift` and `wholeMainUnchanged` record current comparisons separately; any drift fails the receipt, including unrelated whole-main changes. A later isolated source run must use its own frozen input closure and evidence; no live-main drift exemption or backfilled PASS is provided.

For a bounded follow-up after a failed invocation, the same runner accepts `--variants name[,name]` after its evidence directory. It rejects unknown, duplicate or empty selections, validates the exact executed scenario set, and marks a partial successful invocation `TARGETED_PASS`, never full consumer acceptance. Use a fresh evidence directory and retain prior failures; this is not permission to repeat already green variants.

## Recorded bounded run, 2026-11-03

`G:/YeManCC-Work/Testing/YMCC/Evidence/ROG-Gyro-Link-20261103/Consumer/ActualR1/consumer-result.json` records the first invocation as PASS: one normal variant, three retained original faults, and three new targeted faults. The normal variant passed 124 assertions (31 original plus 93 new); fault variants executed 31/31/31 and 3/3/4 assertions, each with native exit 1 and its required semantic failure. There were 227 assertion executions across seven variants, not 227 unique normal checks. Build time summed to 24.499s, native execution to 14.930s, and outer wall time was 39.709s. The normal native run took 2.444s, including its real 750ms + 1500ms STA path.

All 18 declared source inputs matched at completion, with no source drift and `wholeMainUnchanged=true`; main SHA256 was `c883b97464a2b46952dd414f5b675c2004dbb43a8641c9c8961184c4535d0dce`. No already-green variant was repeated. This records only the original consumer producer, not global-parent or physical-device acceptance.

## ROG 百式 targeted consumer, 2026-11-03

The existing entry accepts `--variants gyro-cadence` as an independent explicit selection. It compiles `cadence.cpp.in` with verbatim current native sink, cadence state machine, trigger consumer and status-log projection through `cadence.mjs`. It does not add the new selection to the default suite or a release minimum. The parent remains `sleep.gyro-power`; this is a targeted receipt, not execution of the original power suite.

Memory terminals own the clock, sensor callbacks, machine-family observation and PnP/WinRT calls. The current 73 assertions cover first linked enable, independent presets and legacy triggers, source timestamps, healthy 33/100/200/500/1000Hz and interval-limited sources, delayed pumping, binding replacement, OFF/power pause, finite success, status semantics and ROG-only recovery. Generic-provider cases still run inside an explicit ROG machine fixture. A non-ROG case with the same BOSC0200 ID proves no observation/recovery starts while ordinary raw motion keeps flowing. Other physical brands are not claimed compatible or modified.

One private fixed-120Hz-cap fault must fail the exact 500Hz assertion. Optional `YMCC_GYRO_BEFORE_HEADER` compiles the supplied original shortcut header and must fail the exact first-enable assertion, without copying old files into the product tree. All sensor/device/process operations are memory ports; no installed YMCC, PnP node, Steam, game or physical controller is started or changed.

The ROG-only policy and the distinction between source cadence and effective output are recorded in `docs/rog-gyro-recovery.md`. Later brand adaptation requires player evidence; it cannot be enabled merely by lowering a frequency threshold.
