# HC function attribution

`PinnedOneXHidV1.g.cs` reuses seven functions from Handheld Companion, as recorded
in `tools/fixtures/lightsetter_devicecore/source-lock.json`:

- `Devices/OneXAOKZOE.cs`: `BuildV1Command`, `SendV1Brightness`, `SendV1SolidColor`, `SendV1Rainbow`.
- `Devices/IDevice.cs`: `WithReportID`.
- `Devices/OneXPlayer/OneXPlayerX1Mini.cs`: `SetLedColor`, `SetLedBrightness`.

Source snapshot: `deps/handheldcompanion-runtime/source`, baseline
`HC-BINARY-BATCH12-20260831`. Its freeze manifest records no known Git revision;
identity here is the exact per-file SHA256, not a guessed upstream commit.
Original function text is retained. Only type aliases and the minimal base class,
capability field and terminal-injection constructor are supplied by YMCC.

This component is named `YeManLightDeviceCore`; it is a YMCC derivative, not an
upstream HandheldCompanion binary. The functions' upstream provenance is preserved
here and in the lock file and extraction tool.

## X2 Mini Pro consumer (HC063, additive API)

`X2MiniProDeviceCore.cs` and `PinnedX2MiniProProtocol.cs` adapt only the
fixed newer source at commit `f2e58468d9917189d4be7ccfc242385ce2c3035b`.
They do not change the frozen extraction or its lock above. Exact inputs:

| Fixed source relative to HandheldCompanion | SHA256 |
| --- | --- |
| Devices/OneXPlayer/OneXPlayerX2MiniPro.cs | 7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67 |
| Devices/OneXPlayer/OneXPlayerX2.cs | 41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b |
| Devices/OneXPlayer/OneXPlayerX1.cs | 3b80d932227c3beddbb6b3a4007c57fc19e0fb55df0db54d389ca8834eae3485 |
| Devices/OneXPlayer/OneXAOKZOE.cs | 30661f7bfba6b67ba9f025be97158c274fe28cabdcd0faac08ca3fb94c34d718 |
| Devices/IDevice.cs | f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47 |

The adapter preserves `OneXPlayerX2MiniPro.SetLedColor` and
`SetLedBrightness`: main zones 01/02/07, secondary zones 05/06, brightness
zones 01/02/07/05/06, and 200 ms after each zone, including the last.
`OneXAOKZOE.SendV1SolidColor` selects FE solid or F0 breathing; its RGB
payload is 18 triples then R/G. `SendV1Brightness` maps 0-100 percent to
an enabled bit and levels 01/03/04 at thresholds 33/66. Zero sends disabled.
The existing `BuildV1Command` / `WithReportID` bodies are unchanged in these
newer inputs and are reused: report 00 + 64 bytes, B8/3F/01 header, zero
padding, 3F/B8 footer. No side 00 aggregate report is emitted.

The X2 constructor inherits VID 1A86 and FF00:0001 filtering from X1 and
adds PID 1305 alongside FE00. The transport opens only the exact admitted
native-owner path, verifies HID attributes/usage and 65-byte output length,
and rechecks before writing. This is a conservative descriptor requirement,
not a claim that a physical descriptor has been observed.

This is an adaptation, not a verbatim extraction: BCL RGB and the existing
injected HID terminal replace HC types/WriteReport; the native owner supplies
serialization instead of HC's HidWriteLock; cancellable 20 ms slices preserve
the source's 200 ms delays and check generation/heartbeat between writes.
Brightness inputs are bounded to the documented 0-100 range. Speed is ignored
by the fixed source and is reported as unapplied. The UI off operation maps
to the sourced five-zone brightness-zero operation followed by five-zone
solid black, in the existing ExecuteLightingOff order without SetLedStatus.
A false brightness result still allows black writes; any false result yields
`off-partial`, with the first failed stage retained in `firstFailure`. Status and presets
remain unsupported: their inherited X1 implementations use serial transport.

Open/initialization was reviewed together with the writer: X2.Open calls
X1.Open; MiniPro disables WMI EC but enables serial; IDevice.Open initializes
OpenLibSys. X1.Device_Inserted opens/reads vendor HID, waits 4000 ms, then
MiniPro.ConfigureController sends three B4 remap pages with 50 ms gaps and
B2 BuildIntercept(false). The lighting helper does not repeat any of those
controller/EC/serial side effects. Native must finish the source-specific
initialization before publishing its existing OneX owner admission.

The additive `YeManCC.Lighting.X2MiniProDeviceCore.ExecuteOwned` API requires
that owner endpoint, deadline/revocation token and generation check. The
existing owner handoff implementation is currently named `Oxp3OwnerLease`;
only its ownership/cancellation ABI is reused. No OXP3 transport, writer or
zone/preset behavior is used. Policy, native admission and the compiled
component pin still require the parent task's integration. This source patch
is not registration, packaging, test, build or physical acceptance.

Per-symbol text SHA256, source file snapshots, actual call chain, patch and
remaining integration suggestions are retained in HC-CODEX-063 evidence at
`x2mini-lighting/SOURCE-IDENTITIES.json` and `REPORT.zh-CN.md`.

Parent integration contract: source adapter `onex-x2mini-hid-v1`, managed/native
adapter enum `OneXX2MiniHid`, exact class `OneXPlayerX2MiniPro`, and the source
SHA above must all agree. The independent core policy's X1Mini action set stays
brightness plus solid/rainbow; X2MiniPro adds brightness and solid/breathing.
The existing UI sends action `off` and expects operation `lighting.off`, so X2
must also explicitly admit `off` in that same per-model policy
(`actions.off: true`, X2 only). Its implementation here is brightness zero,
then solid black (ten reports); it never calls SetLedStatus or reads/guesses
current colors. The off receipt remains `lighting.off` / `ok` or `off-partial`.
Status/preset remain false. The parent owns the enum/policy/generated changes;
this subtask never changes their admission or the compiled component pin.

`fixedSpeed` / `speedIgnored` are consumer facts because this exact five-zone
SetLedColor implementation never uses its speed parameter. They do not assert
an explicit upstream DynamicLightingFixedSpeed capability declaration.
Ancestor LEDPreset is retained as `presetDeclared=true` source provenance;
`presetAvailable=false` and only solid/breathing are effective color effects.
All receipts describe completed software writes/release, never observed LEDs.
