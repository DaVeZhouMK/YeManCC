# Offline OneX capture shutdown regression

Run from the source checkout:

```powershell
node tools/onex_capture_shutdown_selftest.mjs --mutations
```

The runner compiles the actual current production session, source cancel/current ledger, shutdown, close, zero-time service, retirement, pump, finalizer and bind-admission prefix. Read/WriteFile enumeration and actual open are excluded; only the production bind admission prefix runs, followed by an in-memory bind counter. Raw parser is an observed sink to distinguish current admission from stale publication. OS wait/cancel/result/close are deterministic stubs; every wait is asserted to use zero timeout.

The ten cases cover ownership withdrawal, FIFO preservation across source generations, quiescent close, both pending operations, rapid restart, actual incomplete results, late success/abort, temporary observation failure, active current read/fault controls, and completion-order combinations. Four semantic mutants must compile before their expected section rejects them.

Not a Windows driver cancellation, real OS sleep/wake, HID endpoint or virtual output/Steam consumer test. Results do not establish that this source defect caused a field incident.
