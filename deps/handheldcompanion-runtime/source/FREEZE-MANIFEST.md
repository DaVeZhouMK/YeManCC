# Frozen HC Baseline Manifest

- Snapshot ID: `HC-BINARY-BATCH12-20260831`
- Historical source: `C:\SOFT\YeManCC-Work\FanLab\hc-upstream\HandheldCompanion`
- Imported to: `G:\YeManCC-Work\Reference\HandheldCompanion\Frozen-HC-BINARY-BATCH12-20260831`
- Source file count: `1,242`
- Source byte count: `147,293,276`
- Git revision: `UNKNOWN`; source repository reports `bad object HEAD`
- Copy result: `1,242/1,242`, failed `0`, mismatched `0`

## Core Hashes

| File | SHA-256 |
|---|---|
| `Managers/PowerProfileManager.cs` | `A0289A25C2B909BE624ED5B0B52DC1F6C1908C37324C6D4D2E1971D4F936EDB1` |
| `Managers/ProfileManager.cs` | `DD9B003DE3CD242993EADD42E78C913531B81F0841A5204689AAE26102BDC9A7` |
| `Managers/SystemManager.cs` | `8986AC3AE2767406AB29FBDC3DEB4581160A54105639EB94E0A6006310A815E9` |
| `Misc/FanProfile.cs` | `A8C8921B324020940FAC42D4284C94F74139844B676D6D6BEB1ABB5FD1CCE8A5` |
| `Devices/IDevice.cs` | `FBAED1DFD329E3328376488B8EC70C3A7FDCF97D70DCB37F1525EE33AA169C20` |

This snapshot is source evidence for isolated Fan V5 HC comparison. It is not automatically loaded, executed, rebuilt, or treated as a current upstream revision. Native DLLs and drivers remain audit material only.

## Domain boundary

This is the Fan/assembly historical freeze. It is not the A3 Gamepad/Gyro
source baseline. A3 controller and gyro comparison uses the separate pinned
source checkout registered as `HC-SOURCE-A3-GAMEPAD-GYRO-ROUND4-20260901`.
