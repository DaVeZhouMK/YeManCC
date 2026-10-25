# Actual Host finalize and recovery regression

Run `Run-SelfTest.ps1 -HostPayloadRoot <actual payload> -EvidenceRoot <fresh folder>`.

The child loads the supplied actual Host DLL, enters its existing self-test log isolation and executes its production bounded-retry, parent-exit acceptance, budget-exhaustion and explicit-retry self-tests. Two additional controls hold the real engine gate and require both escalation and terminal recording to return within the existing 2000 ms lock budget. A free-gate terminal call must yield FaultLocked/UnknownState and must not claim OEM restoration. Target native imports are blocked and HC must not load. This does not exercise EC, devices or physical restoration.

The existing simulator's `tests/parent_exit_contract.ps1` combines this with `tools/fan_host_parent_watchdog_selftest.ps1`. That second process must preserve the exact host/cycle/parent identity, settle honestly and naturally exit zero; forced cleanup cannot pass. I13 in invariants now requires both receipts instead of historical source string names. All artifacts bind supplied Host hashes; no product binaries are built or deployed by this test.

The default regression also creates a private metadata mutation of the actual Host DLL: only MarkAbnormalFinalizeTerminal changes FaultLocked to Ready. It has no executable or payload manifest, uses the same blocked-native child and must fail the real terminal-state assertion. The positive receipt requires knownBadReadyTerminalRejected=true; a fake restored/Ready result cannot pass.
