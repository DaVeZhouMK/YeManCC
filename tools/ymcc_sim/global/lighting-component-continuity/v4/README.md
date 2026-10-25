# Lighting build delivery in the existing simulator

Proposed registered ID: `lifecycle.lighting-build-delivery`.

This leaf complements `lifecycle.lighting-component-continuity`, whose fixed 7bf v3 contract remains unchanged. The new case consumes the reviewed b6 helper, e6 native, independent core, runtime/profile and matching lighting catalog. The archive contains build/test evidence only; no ZIP or DLL is added to the product runtime.

Every execution verifies and extracts the fixed archive into its owned case directory, checks every member's real SHA256, restores exact temporary installation preimages, runs the unchanged `New-LightingBuildDelivery.ps1`, executes the original 37-case `Test-LightingBuildDelivery.ps1` with PS7 and PS5.1, and finally calls the original connector against the new temporary installation. The original locked-second-file rollback and old v2/v3 acceptance/refusal cases remain enabled. Reading an earlier PASS is not execution of this case.

`Archive-IO.ps1` only resolves declared original paths to the matching archived bytes. It returns actual file hashes, content and copies, does not rewrite old receipts, and blocks undeclared external reads and copies outside the owned case. The selected legacy v2 contract uses its approved 7bf C-path image; the earlier image remains archived. New-helper and legacy maps are separate because the same original source path can describe different source versions.

The ZIP reader checks pinned compressed size/hash, member count, expanded size, per-member size, absolute/traversal paths, case-insensitive duplicates, and symlink/reparse attributes before extraction. Extracted bytes are checked against the separately pinned index. No native application, HC factory, real device, UI or actual installation is started or changed.

After registration by the shared-entry owner, run from the original `tools/ymcc_sim/ymcc_sim.ps1` global entry with this exact test ID and a fresh evidence directory outside the repository. The wrapper requires the global-owned execution environment. Its report separates new plan generation, both engine runs, child connector processes and the final two-file transaction. Fixed tuple validation is software evidence; full release/UI matching and physical acceptance remain separate.
