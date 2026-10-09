# P19 P12 encoder current-source line map

Manifest: [T22-RF00-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-current-source-20260905-manifest.json](T22-RF00-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-current-source-20260905-manifest.json)
Manifest ID: T22-RF00-20260905-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-STATIC-SNAPSHOT
Capture UTC: 2026-09-05T05:13:43.7657621Z
Key files: 58
Source digest: EDA2452CA7FF6447A134FBE7EF4A1C27737E150B3EFA5876EC409B7D3889BE8D
YMCC HEAD: aa8f38b83cc960267d2f68bf78d0e8adaca2234f
YMCC porcelain entries: 416
YMCC porcelain SHA-256: C1DA185C45C77B0332A3CC6344B7B8AF9071D838C845ED151D04E1AD8842CB96
HC baseline: 0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103
HC worktree clean at capture: True
Raw static search: [T22-RF00-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-current-source-20260905-raw-search.txt](T22-RF00-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-current-source-20260905-raw-search.txt)
Raw static search SHA-256: 221881BEBAE0CD8F29E2488CBE07EFB990D88CF6EA5606CD5FB4DF97069C2B21

This is static source/provenance evidence only. No build, test, InputHost, HIDMaestro, HidHide, ROG report, Steam, game, virtual device, or hardware operation occurred.

## P01 provider/default-sensor and threshold authority

Native still selects Windows gyrometer and accelerometer independently. HC device identity, default-sensor choice, matrix binding and per-device calibration key are not proven.

## P02 paired raw motion versus calibrated gyro

Locked HC GetRawGyro and GetRawAcceleration return the ProcessMotion input values, and DualShock4Target encodes those raw values. Native keeps pairProven false. Its later candidate branch obtains calibrated gyro and transports no acceleration to Host. The mismatch remains safe-stopped.

## P03 DS4 descriptor, encoder and Host transport

E02 captures DS4 v2 fields. E03 proves that the locked encoder reads HMGamepadState GyroPitch/Yaw/Roll and AccelX/Y/Z int16 fields. InputHost writes only GyroDps float fields and native transports gx/gy/gz only. No runtime encoded-report readback exists.

## P04 P-HID and ROG boundary

Legacy HidHide remains production-unadmitted. No P-HID result may imply P-XINPUT or external game-consumer isolation.

## P05 owner and consumer observation boundary

Native stateWrite written means local atomic file replacement only. It is not Host ACK, HID readback, Steam observation, or game-consumer observation.

## P06 release and recovery boundary

There is no host instance/epoch/revision/config hash ACK, first-frame receipt, neutral/release proof, OS enumeration, suspend, PnP, or crash recovery proof.

## P07 provenance and render quality

P17 remains a historical snapshot. Its original Markdown line-map/report retain a documented template/NUL defect. P19 output was prevalidated for zero NUL and zero literal template placeholders.
