// The optional file-transaction selection belongs to the original owned-return
// verifier. Listing these files never runs the selection or adds a default gate.
export const MODULE='tools/fixtures/msi_owned_return_store';
export const REAL_STORE_SOURCES=Object.freeze([
 'tools/verify-msi-owned-return-runtime.mjs','tools/msi_owned_return_runtime_selftest.cpp',
 ...['prepare_base.mjs','prepare.mjs','run.mjs','worker.mjs','sources.mjs','verify_case.mjs','store_bridge.h','store_bridge.cpp','toolchain.json'].map(name=>MODULE+'/'+name),
 'tools/virtual_controller_runtime_source.mjs','tools/ymcc_sim/global/controller-startup-return/extend-fixture.mjs',
 'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1'
]);
