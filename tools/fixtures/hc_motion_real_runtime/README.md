# HC motion real runtime verification

Runs the existing, source-index-pinned GamepadMotion.dll in CPU-only processes.
No HC managed assembly, device factory, physical handle, sensor, OEM mode or Host is used.
No product DLL is created/copied/replaced.

The runner freezes exact production matrix functions, the full Windows ProcessMotion/readback block,
the Madgwick overlay, and the Controller Session/Owner headers. An independent HC C#/JSON oracle
supplies legal matrices. Windows and Controller outputs are compared to separate actual-library
handles, including offsets in dps, gravity, player/world output, normalized orientation and retired
source tokens. Four controller-preferred Windows catalog tuples are not admitted by this fixture.

The latest three supplied family ZIPs provide raw six-axis vectors. Replaying these is a CPU numeric
regression, not a reconstruction of the historical calibration/filter state, provider admission,
source generations, physical axes, complete BUS, Host submission, game consumption or sleep/wake.

The intentional pre-fix run is accepted only when the observed failures are the reproducible
zero-gradient Windows orientation failure. Mutation fixtures never modify product source.
