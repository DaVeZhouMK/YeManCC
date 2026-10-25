# Windows motion binding regression

Extracts the entire real InputCaptureSensorSink class, raw callback handling,
the real BUS snapshot copy block and inputCaptureMotionPairAdmitted verbatim.
WinRT adapter methods compile against the installed SDK but are never invoked.
Explicit in-memory raw reports execute actual source code without sensor, OEM,
Host, application launch, dependency DLL loading or power actions.

The required invariant is that a new binding is not itself a sensor observation.
Both current channels must deliver data before a pair can admit. Tests include
subscription-failure fallback, same-generation idempotence, retired callbacks,
existing motion episode preservation, 4096 monotonic-token transition paths and
16 two-thread late delivery schedules. Source generations are never reused.
