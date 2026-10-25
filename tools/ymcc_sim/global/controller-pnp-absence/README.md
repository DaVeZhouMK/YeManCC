# PnP absence opaque-ID regression

Registered as `controller.pnp-absence-opaque-ids` in the existing global simulator, profiles `desktop` and `full`, kind `node`, scope `behavioral-mock`. Evidence is source-bound through the production header/record, JSON dependency, fixture, existing selftest, selected Host matching source and this registration module.

The wrapper executes the unchanged `tools/virtual_target_return_pnp_absence_selftest.mjs` as an actual Node child. It requires fresh absolute output and resolves the captured toolchain from `YMCC_SIM_PNP_ABSENCE_TOOLCHAIN` (absolute directory) or the existing default `G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch/native-verified`. Missing capture files, tools or frozen dependencies block registration or fail the wrapper; no compiler discovery/fallback occurs.

`toolchain-inputs.json` inventories the 280 frozen compiler headers/linker libraries observed in the existing final selftest receipt. It reuses only that dependency inventory. Each new run binds capture/manifest, Node, compiler/linker/dumpbin and every listed frozen dependency as simulator artifact inputs, checks pinned dependency identities and refuses an actual compile closure outside that registration. A future dependency change needs explicit inventory review.

The required receipt is `pnp-absence/RESULT.json`. Acceptance checks `pass`, all six variants, source/selected-Host-rule fences and actual dependency closure. The selftest retains the historical `BOUNDED_PNP_ABSENCE_FAKE_READER_VERIFIED_RUNTIME_NOT_INTEGRATED` status label. The production header is now connected through cold_runtime/main.cpp; that label is not interpreted as a current absence of production integration. This run executes only the actual helper with fake Reader. The actual ProductionReader ABI compiles, but its export is never called.

Run the exact front-door case from the repository:

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile desktop -Tests controller.pnp-absence-opaque-ids -EvidenceDir D:/Codex/2026-10-22/new-chat/g3e-freeze-080106/simulator-regression
```

Focused wrapper/registration checks:

```powershell
node --test tools/ymcc_sim/global/controller-pnp-absence/runner.test.mjs
```

`field-record.json` records the supplied ZIP observation: 29 cold-return unknown/device-id-invalid results from 07:56:43.529 to 08:00:48.653 on 2026-10-22. The old log has no exact rejected G3E ID. Every fixture ID is a contract regression input, never a claim about that G3E node. No physical hardware acceptance, product/Host execution, device/driver/service operations or new authority follows from this test.
