# MSI cost window dispatcher selftest

Pure .NET 8 console project. It links the actual dispatcher source; it does not
reference or execute InputHost, the SDK, Native, a pipe, or a device. All callbacks
are fake and all waits are test synchronization. No runtime installation is needed.

From the repository root:

```powershell
dotnet build tools/msi_cost_dispatcher_selftest/MsiCostDispatcherSelftest.csproj -c Release --artifacts-path tools/msi_cost_dispatcher_selftest/artifacts/build
dotnet tools/msi_cost_dispatcher_selftest/artifacts/build/bin/MsiCostDispatcherSelftest/release/MsiCostDispatcherSelftest.dll tools/msi_cost_dispatcher_selftest/artifacts/run-unique
```

Use a fresh run directory for each run. The runner saves a JSON receipt with the
linked source SHA-256 and assertion counts. Its two-second caller checks detect
waiting for a deliberately blocked fake callback or admission gate. They are
regression test budgets, not hard real-time or OS scheduling guarantees.

`SnapshotCounters()` reads fields independently and atomically. `Completed`
counts successful callbacks, `Failed` counts throwing callbacks, and `Discarded`
counts accepted pending windows removed during close. `Stopped` means the single
worker thread has actually terminated. Dispose returns while a blocked callback
is still active; its background thread and event remain owned until that callback
returns. No second writer is started. A post racing with close can be accepted
before close wins the atomic slot, then be discarded; posts after close reject.
