# MSI source-window / R3 authority

Run the existing simulator front door:

```powershell
tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests controller.msi-source-window
```

The fixture compiles the actual route decider, its actual lifecycle/wake wrapper,
ordinary base-event registration and the unchanged VirtualStartProtection class.
It reproduces the captured 19:45 route ordering: request start, late SDK mount,
legacy 10-second revert and DInput observation after that revert. It preserves
original admission/inspection/fallback limits, power-wake ownership, OFF/exit
boundaries and the non-MSI scope.

The preserved original wrapper is actual pre-fix code. Its body SHA also matches
the shipped SDK2.0 R3 source; the receipt records both source fingerprints. It is
compiled only as a failing policy control. Additional compiled faults must catch
a prior wake lending its longer budget to an expired player request and an
incorrect family scope. No test executes normal product Main or a device API.

This case proves the source-window policy and admission relation. Full pump,
physical binding/input, restart and sleep acceptance remain separate evidence.
