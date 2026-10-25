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
