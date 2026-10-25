# OneX X2 protocol library offline validation

Production header: `native/onex_x2_protocol.h`, namespace `ymcc::onex_x2`.
Reference: fixed HC `a250b19a4b7e29dac3b383f2d1ac517d1af43e4e` (not a latest-version claim).

Run from the source repository:

```powershell
node tools/hc_onex_x2_protocol_selftest.mjs
```

The runner writes generated expectations, source snapshots, compiler output and receipts
only under `G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/OneXProtocol`.
`--out` must select that directory or one of its children; use a fresh directory.
`--vcvars` can select an already installed MSVC compiler. No network is accessed.

`oracle.mjs` independently parses pinned C# class inheritance, collection IDs,
button roles, click duration, remap builders, init call arguments/waits and framing.
Each C# input is checked by SHA-256 and Git blob against the pinned manifest and
local fixed Git tree. It does not read the production header. The generated C++
oracle and pure header are compiled in Release and Checked with C++17 and /W4 /WX.
A separate translation unit checks that the header compiles without a precompiled
header or Windows headers. Tests exhaust every byte button ID, every 16-bit value
of each collection field, valid/invalid enums and initialization selectors, 64/65
encoding, payload/report size edges, logical decoding and deterministic fuzz data.
Four deliberately broken copies must compile and then fail the expected semantic
section: OXP3 Home, inherited third page, report ID prefix and input offset guessing.

## API and boundaries

- `modelForClass` explicitly accepts the short HC class names OneXPlayerX2,
  OneXPlayerX2MiniPro and OneXPlayer3, plus each exact canonical
  `HandheldCompanion.Devices.<Class>` used by machineIdentity.deviceClass. It does
  not strip arbitrary namespaces, match prefixes/suffixes or resolve SMBIOS aliases.
- `matchesCollection` matches VID, specific per-model PIDs and FF00:0001.
- `mapButton`, `clickOnly`, `clickPulseMs` describe roles only. X2/3 map 0x21 to
  Home; OneX3 maps 0x24 to Home as well. Mini maps 0x21 to Keyboard. OEM clicks
  have reference duration 100ms; rear buttons are held signals.
- `decodeLogicalFrame` requires **already stripped `HidReport.Data`**. It neither
  strips nor guesses raw input report-ID layout. It follows HC's logical minimum,
  marker checks, command fields and exact pressed-byte comparison. Unknown button
  IDs remain Button::None. Status is data only; 0x20 does not start reconfiguration.
- `initStep` describes three B4 pages (X2/3), or Mini's three B4 pages with 02 38 02,
  F15/F16 68/69, then B2 payload 00 01 02. Delays are returned, never executed.
- `encodeReport` supports only the documented output lengths 64 and 65; the latter
  has report ID zero and the former starts directly with command. Payload <=59.
  Zero-sized null payload is accepted; nonempty null and oversized inputs rejected.

No runtime integration, enumeration, device opens, HID writes, keyboard hook,
WMI/EC/serial access, HC object construction, scheduling, retry, shutdown cleanup,
power operations or actual sleep/wake acceptance is included. Capturing an actual
button or selecting a runtime device remains a separate integration task. This
header does not change captureImplemented flags or reuse the old X1 device route.