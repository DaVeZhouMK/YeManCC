# TargetCreationLifetimeEvidence pure fixtures

This project links only the real InputHost/TargetCreationLifetimeEvidence.cs and ../target_creation_lifetime_evidence_selftest.cs. It has no PackageReference, SDK binary, Program.cs, HostSession, Windows target, device API or probe. Targets net10.0, warnings are errors, and runs with dotnet; UseAppHost=false.

From the repository root:

```powershell
dotnet build tools/TargetCreationLifetimeEvidenceSelfTest/TargetCreationLifetimeEvidenceSelfTest.csproj --configuration Release --nologo --verbosity minimal -p:RestoreIgnoreFailedSources=true
dotnet tools/TargetCreationLifetimeEvidenceSelfTest/bin/Release/net10.0/TargetCreationLifetimeEvidenceSelfTest.dll
```

stdout is a JSON test receipt; nonzero exit means failure. Build artifacts stay in this pure test project under bin/obj. No production Host selftest is run.

Caller contract: construct one helper per existing host Session with TryCreate(); call BeginAttempt(), Created(ticket, nonnullSdkObject), ReleaseAttempt(), Failure(ticket) under the existing lifecycle lock. Ignore Created's boolean for device/admission policy. Failed diagnostics must never reject or accept a production command. TryAppend preserves the original string or byte-array instance on any failure and only permits PREPARE_TARGET, RECREATE_TARGET, RELEASE_TARGET, SHUTDOWN. Never add calls on SUBMIT_FRAME.

The evidence field is targetCreationLifetimeEvidence, schema/version 1. Counters are exact invariant decimal strings. A current live token is independent of protocol run/host/epoch/power and changes on each successful creation; closing invalidates it before any disposal outcome. A snapshot is historical once the lock is released. It is caller-confirmed SDK-object-lifetime diagnostic only, never OS incarnation, SDK ownership, Steam/game input or Continue evidence.
