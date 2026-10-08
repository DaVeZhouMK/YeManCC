# T22 BUS P24 — HC ↔ HIDMaestro ↔ YMCC three-way audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / HID-SOURCE-LOCATED / DLL-LOCKED / BINARY-REBUILD-UNPROVEN / CURRENT-DS4-USB-IMU-PATH-UNREACHABLE / SAFE-STOP-RETAINED / RUNTIME-BLOCKED`

Date: 2026-09-05. Read-only hash/metadata/source comparison only. No build, test, YeManCC, InputHost, HIDMaestro API, driver, virtual device, HidHide CLI, Steam, game, physical controller, or hardware operation ran. No product source changed. This is not an RF00 manifest.

## Scope and provenance

| Layer | Read identity |
| --- | --- |
| HC authority | `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`; DS4 target `30A1E270…1927337`; VirtualManager `46D0B081…1219106`; MotionManager `19A9C645…F6FC7E` |
| HID runtime | `HIDMaestro.Core.dll`, 40,755,712 bytes, SHA-256 `BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E`, MVID `c61e2715-47ae-4cf3-87f7-244c330ef80e`, assembly `1.7.0.0` |
| HID source locator | DLL ProductVersion embeds `1.7.0+46054b862830fcec7bc98d72ccb7c4f0c0179fb1`; exact-source reads: `HMGamepadState` `E9812CB1…4B9632`, `HMController` `312652FD…04B95`, `VendorBlobCodec` `27F161EA…81B2`, USB DS4 profile `95E76EFA…EC716` |
| YMCC scope | `native/main.cpp` `8797428B…D808FA`; `InputHost/Program.cs` `9C855378…22A95B` |

The isolated probe, Build, and InputHost Release DLL copies are byte-identical. The exact upstream source revision is `SOURCE-LOCATED`, but no local HIDMaestro source checkout, matching PDB/SourceLink, package source, or reproducible build was found. Therefore source-to-DLL rebuild equivalence remains `BINARY-REBUILD-UNPROVEN`.

The locked-DLL parts of [P12 E03](T22-BUS-P12-E03-HIDMAESTRO-DS4-ENCODER-STATIC-EVIDENCE-20260905.json) remain usable. Its former product-side `GyroDps*` wording was an older source scope: current `Program.cs` assigns no direct IMU field. This audit supersedes only that stale current-product interpretation.

## Facts — three-way chain

1. HC supplies one `GamepadMotion.ProcessMotion` call with gyro **and** accel. `DualShock4Target` reads that raw pair, converts gyro as `round(clamp(dps, ±2048) * 16)` and accel as `round(clamp(g * 9.81, ±64) * 512)`. HC raw means values after its selected sensor-family wrapper, device matrix/remap, and gyro threshold; it is not unmodified register data.

2. HIDMaestro has separate raw-short fields `GyroPitch/Yaw/Roll` and `AccelX/Y/Z`, plus unrelated physical-unit float `GyroDps*`/`AccelG*` fields. The DS4 vendor-blob codec maps the profile's six signed-little-endian fields to those six raw shorts; it does not read `GyroDps*`.

3. Current native state-file transport emits diagnostic `gx/gy/gz` only, with no `ax/ay/az`, provider/pair/matrix/epoch/descriptor proof. `pairProven=false`, and current Host assigns only buttons, hat, and standard axes; it assigns none of `GyroDps*`, raw gyro shorts, or accel shorts. This is a correct no-report safe-stop, not completed IMU transport.

| Question | HC | HIDMaestro | Current YMCC | Result |
| --- | --- | --- | --- | --- |
| DS4 motion payload | raw gyro + raw accel pair | six raw `int16` sinks | diagnostic gyro triplet only | `SAFE_STOP` |
| `GyroDps*` as DS4 IMU sink | not an HC concept | not used by DS4 codec | not assigned now | `NO-DS4-IMU-SINK` |
| Host/submit proves game input | no | no | no receipt/consumer observation | `UNENCLOSED` |

## Critical profile-route correction

Current InputHost maps `--persona dualshock4` to USB `dualshock-4-v2` and calls only `SubmitState`. That profile declares six extended IMU fields, but declares neither `extendedReport.armOn` nor `extendedReport.alwaysArmed`.

At the exact locked HID source revision, `HMController` allocates extended buffer/encoder state only when a profile has `armOn` or is `alwaysArmed`. The `SubmitState` vendor-blob path additionally requires that buffer/state and `_extendedModeArmed`; otherwise it uses the legacy report path. The source explicitly says USB Sony profiles with extended metadata but no `armOn` do not run the extended codec.

Therefore the current fixed USB `dualshock-4-v2` persona has **no statically reachable DS4 vendor-blob IMU encoder path**, even if a later patch filled the six raw shorts. Field metadata is not route reachability.

The locked `dualshock-4-v2-bt` profile has Bluetooth feature-read arm triggers (`0x02`, `0xA3`). That only proves a different handshake-gated backend profile class exists. It does not authorize a persona/profile change, handshake emulation, raw-report write, USB identity change, or Steam/game compatibility claim.

For that unselected BT candidate, static source gives two more bounded facts: it declares a 78-byte extended report with raw fields at 17/19/21 and 23/25/27 plus a CRC at 74–77; and HIDMaestro starts an internal output-reader thread best-effort at controller construction. That reader flips `_extendedModeArmed` after a matching feature-read even if the consumer has not subscribed to `OutputReceived`. Thus an eventual BT experiment would require proof that the output reader/mapping exists **and** that a named consumer issued the matching feature read; InputHost event subscription is not itself the arm mechanism. None of that is runtime evidence, and it cannot make the current USB profile eligible.

## Required disposition and remaining evidence

```text
dualshock4 / ds4-imu = SAFE_STOP
  paired HC-equivalent raw six-axis input absent
  + Host six-axis envelope absent
  + current USB profile encoder route unreachable
  + encoded-byte decode/readback and named consumer observation absent

xbox360 / any IMU = persona-motion-unsupported
```

Before any later implementation, evidence must establish: (1) a permitted persona/profile and activation rule; (2) T12 same-provider/same-generation six-axis envelope with HC matrix/threshold semantics; (3) active encoder-route capture plus independent decode/readback; (4) T10 Coordinator/Host ACK, neutral/release and recovery receipts; and (5) if required, source-to-DLL reproducible-build provenance. No local `GyroDps → GyroPitch` patch can close these independent gaps.

No DGF is created; no runtime task closes; `runtimeUpgrade=false` remains.
