# Battery host dependencies and protocol provenance

YeManBatteryHost is a separate, framework-dependent Windows x64 .NET 10 component.
It uses the .NET Windows Desktop runtime, as do the existing YMCC helper components.

- System.Management 10.0.9: Microsoft / dotnet/runtime, MIT license, restored by NuGet.
- LibreHardwareMonitorLib.dll: copied without modifications from the existing pinned
  `deps/ymcc-shared-runtime/bin` snapshot (previous distributor:
  `PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902`); LibreHardwareMonitor and contributors, MPL 2.0. The same signed PawnIO
  modules and already-installed driver are used. See LICENSE.LibreHardwareMonitor.txt.
  Source: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor .
- RAMSPDToolkit-NDD.dll 1.5.0: transitive assembly reference copied from the same
  unchanged third-party snapshot; Florian K. / Blacktempel/RAMSPDToolkit, MPL 2.0 (same license text).
  Its RAM/SPD interfaces are not initialized or used by this component.
  Source commit from NuGet: 713038f919d85397214e17644d78534dc15a2fd2.
- Steam Deck physical-memory support uses the existing separately packaged InpOut
  library only when its driver is already installed. This component installs no driver.

Protocol/identity evidence is recorded in docs/battery-charge-capabilities.json and
its source manifest (HandheldCompanion, G-Helper and the separately reviewed GPD
firmware driver). HandheldCompanion's device initialization is never invoked.
No full HC runtime DLL is newly bundled into this component.

The research catalogue is an immutable, read-only evidence snapshot. The embedded
battery-routes.json is the deliberately narrower Windows runtime allowlist, generated
by tools/battery-charge-generate-routes.mjs. Neither source evidence nor successful
compilation asserts validation on a physical handheld.

Pinned HC binary commit: 06c0b9544db2b1f39abf9cd3796225ccfb096103.
LHM SHA256: f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf.

The independent snapshot manifest and upstream license/source provenance are in
`deps/ymcc-shared-runtime/manifest.json`. LibreHardwareMonitor source commit from
its embedded ProductVersion: d91ec6ea8c6b06eba338a47cad6f56e67113d6e6 (not replaced
by the different NuGet 0.9.6 binary). The legacy Steam Deck InpOut load location in
Providers.cs is retained pending a separate EC packaging decision.
