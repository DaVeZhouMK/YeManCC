# FanHost Runtime Omissions

This record distinguishes the current FanHost runtime package from the frozen
HandheldCompanion source baseline. It is not permission to change HC behavior
or to substitute a different device lifecycle.

The current `PowerControl/fan-host` package intentionally does not contain the
following HC native helpers:

- `IGCL_Wrapper.dll`
- `JoyShockLibrary.dll`
- `libVIIPER.dll`
- `SapientiaUsb.dll`
- `SDL3.dll`
- `Xinput1_4.dll`

They remain unverified for a future input/gyro runtime materialization. They
are not silently treated as FanHost dependencies, and they must not be added
to or removed from a release package without a separate closure audit. The
current FanHost source and its 70 HC factory-route mappings remain under the
same frozen HC source baseline; the only approved behavioral exception is the
HWiNFO shared-memory temperature source.
