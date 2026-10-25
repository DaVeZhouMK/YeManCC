# Fan initialization diagnostics and cold listener self-test

Run with PowerShell 7 and .NET 10. All build outputs and logs stay outside the source repository:

```powershell
./tools/FanHcInitializationDiagnosticsSelfTest/Run-SelfTest.ps1 -EvidenceRoot G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-ROGX-ColdAfterWake-20261023/repair/new-run
```

The existing diagnostic checks load the actual compiled Host as a library and check exception identity, stage binding, metadata, allowlisted diagnostics and the real HostDiagnosticLog disk format. Synthetic secrets in exception messages/Data must never enter diagnostic payloads. Initialization must preserve validation, assembly setup, motherboard identification, the device factory, Initialize(false, false) and identity. Fan loading must not explicitly call PullSensors.

FanColdStartupChecks executes the real LocalFanHostServer, FanHostEngine and OnStaBounded boundary on an ephemeral loopback port. Its metadata device is an inert fixture preloaded by reflection; it does not run the real HC loader/factory. It verifies authenticated health before and during initialization, refusal before unauthenticated initialization, one explicit handshake completing after six seconds, the real fifteen-second timeout, rejection of a second in-flight initializer, harmless late results and bounded owned shutdown. Handshake metadata never grants hardware write permission.

Host Main, the production process lifetime mutex, HC assembly, real device factories, ManagerFactory, EC/ACPI/WMI and motion/serial sensor discovery are not executed. These checks establish the local server/engine behavior; they cannot identify the exact blocking hardware/HC subcall in a user's old log or establish physical fan behavior. Direct generic factory consumers are maintained separately in tools/ymcc_sim/global/onex-fan-coexistence.

Evidence: host-build.log, self-test-build.log, self-test.log, self-test/result.json, self-test/nested-exception-diagnostic.json and the isolated diagnostic log directory. Failure diagnostics follow the existing runtime logging switch; only the isolated test log gate is enabled here.
