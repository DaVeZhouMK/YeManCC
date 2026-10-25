# Fan component source archive tooling

This is build/evidence tooling, excluded from runtime payloads. It never opens a device, starts FanHost/HC, installs a component or promotes a candidate.

Build FanHostSourceArchive.csproj with --artifacts-path pointing outside the source repository. The inspect command takes repo-root, exact-artifact-root, build-evidence-root and output-json; it reads the actual Host/Backend PE and portable PDB, checks the paired IDs and each source checksum, and requires Release without YEMAN_ASYNC_BOUNDARY_TEST. It retains raw custom debug records and decoded compilation options.

The verify command takes bundle-root, externally pinned manifest-SHA256 and output-json. It uses archived source/generated documents, verifies both exact module identities, all files and their source-row/PDB associations, and rejects changed, omitted or disconnected evidence. Original absolute source paths are provenance and document keys, never fallback read locations in portable verification. Byte-identical rebuild reproducibility is not claimed.

From the repository root, tools/prepare-fan-host-candidate.ps1 requires explicit ArtifactRoot, BuildEvidenceRoot, MetadataToolDll and a fresh external OutputRoot. It freezes the current33-input compile closure and matching config, preserves actual artifacts, prepares a schema1 archive and schema2 nine-file payload, copies the unchanged runtime/shared dependencies and runs the existing payload verifier with candidate identity constants in a staged copy. It rechecks input bytes and source membership. It does not update the canonical source bundle, native binary, live verifier, historical admission contract or installed payload.

Run tools/fan_host_source_archive_selftest.ps1 with PreparedRoot, MetadataToolDll and a fresh EvidenceRoot. It executes the actual verifier against owned negative copies, including resealed bad identities to exercise PDB/source association checks. These results do not grant formal publication authority. Mainline promotion must preserve original gates and exact current evidence, compare-and-swap all consumers, retain rollback artifacts and rebuild the matching native consumer.
