# Fan source-current runtime maintenance

Run through the existing guarded simulator:

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests fan.source-current-runtime-maintenance -EvidenceDir <new-outside-repo-evidence>
```

This case builds the current formal FanHost csproj into case-owned artifacts with the existing Run-Catalog bin/obj exclusion. It never uses frozen candidate98 in place of current source. Source membership/bytes, snapshot copies and flat output artifacts are hashed and checked again after validation. A missing required source is a failure.

It invokes two fixed existing consumers against the exact built Host DLL:

- `tools/fan_runtime_maintenance_isolation_selftest.ps1`: original8 maintenance/isolation checks including diagnostic flag/callback failure, sampling failure/reentry, control-state neutrality, timer rearming and no real HC.
- `tools/fan_maintenance_result_buffers_selftest.ps1 -Mode behavior`:20 actual-method behavior scenarios for idle/pending/success/failure/reentry/controller duties and shared Host selftests. The exact Host SHA, all20 positive rows and empty failure list are required.

The adapter records current-source build provenance, original structured outputs, no Host Main/real HC/device operation, no installation/publication and no CPU benchmark. Only software reflection consumers with inert terminals run. PASS is behavior preservation, not measured CPU/power savings or physical fan acceptance.

`maintenance/result.json` is the wrapper result. Original isolation and result-buffer summaries, result-buffer detail, input snapshots, build arguments/log and tool logs remain separate. New or missing tests cannot silently lower the required8/20 contract. No arbitrary tool path, provider switching, publish pin change or installed-payload operation is accepted.

An explicit `source-contract` at `buffer-shape-contract.json` guards exactly production `StopHcControllerCpuLoops` and `EvaluateLoopStop`: two nullable-null locals, nullable ref inputs, all8 known Add sites use lazy `??=`, no eager result-list constructor or direct result Add, and empty diagnostic arrays remain arrays. This14-check lexical shape guard rejects rollback of the allocation change but does not claim measured allocations or CPU savings; semantic pending/idle behavior stays with the original8+20 actual-method checks. Equivalent refactors must deliberately review/update this narrow source contract.
