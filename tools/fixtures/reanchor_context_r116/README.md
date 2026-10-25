# R116 physical-source reanchor context fixture

Run from the repository:

    node tools/reanchor_context_r116_selftest.mjs

The tool extracts actual physical identity, mask/persona helpers and the complete
production `ymccOpenXInputReanchorPhysicalSource` function without rewriting them.
It builds Release and Checked with the real TransactionMutex header. Mapping,
logging and the PnP-route-proof helper are inert: this validates commit admission,
source/accounting continuity and existing query counts, not Windows ancestry or
hardware correctness. It launches only its own fixture, without YMCC/Host/device.

The repaired source passes 77 scenarios / 135 assertions in each build. The frozen
pre-fix source has 89 failed assertions under the same final fixture. Intermediate
63/121 and 75/133 results are distinct snapshots, not additive field defects.

Coverage includes healthy Elite/Xbox saved routes and selected-source fallback;
healthy exact recovered route; current source/Host/run/transaction/power/topology,
publication/Unknown occupancy invalidation during queries; final recovered query
return; fallback replacement or newly acquired journal; initial and commit lock
contention; unresolved source, ambiguity and unproven route. Healthy saved/recovered
routes retain exactly 1 / 2 mapping queries and 0 / 1 PnP-helper calls respectively.

A race callback mutates only process-local fixture state. Some structural callbacks
are stronger than production lock discipline permits and are conservative contract
probes, not proof of real concurrent Windows writes or a field root cause. The
post-query atomic admission edge probes represent the asynchronous boundary.

`--source MAIN_CPP` selects a frozen actual source; `--out DIR` selects a new evidence
location. Default evidence is Build/Validation/VirtualInput-Reanchor-R116-20261016/reanchor.
Exit 1 means contract failures; exit 2/infrastructure errors are not converted to pass.
No real driver, controller, Steam, services, power changes or deployment is performed.
