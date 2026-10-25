// Explicit build-only managed membership versions. Neither a JSON label nor
// this contract grants runtime authority. Legacy21 remains immutable; only the
// named terminal22 contract can compile the unique production terminal policy.
export const LEGACY_MANAGED_CONTRACT='current105-exact21-v1';
export const TERMINAL_MANAGED_CONTRACT='current105-terminal22-v1';
const legacyNames=Object.freeze([
 "InputHost/Program.cs",
 "InputHost/PsEndpointOwnership.cs",
 "InputHost/ParentProcessLifetime.cs",
 "InputHost/HostControllerOutputStop.cs",
 "InputHost/HostControllerIdleCtsRelease.cs",
 "InputHost/HostGameOutputObservation.cs",
 "InputHost/HostVirtualSignalEvidence.cs",
 "InputHost/MsiSteamDeckReleaseFence1726.cs",
 "InputHost/MsiVirtualReportChain1410.cs",
 "InputHost/ScreenTouchpadReport.cs",
 "InputHost/ScreenButtonReport.cs",
 "InputHost/CanonicalFrameFingerprintMemo.cs",
 "InputHost/HostResponseFields.cs",
 "tools/msi_steamdeck_release_fence_1726_selftest.mjs",
 "tools/msi_steamdeck_release_fence_1726_selftest.cs",
 "tools/inputhost_lifecycle_selftest.cs",
 "tools/input_lifecycle_managed_fixture.mjs",
 "tools/fixtures/input_lifecycle_integration/ManagedEntry.cs",
 "tools/ymcc_sim/global/inputhost-protocol/adapter.mjs",
 "tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json",
 "capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs"
]);
const terminalNames=Object.freeze([...legacyNames.slice(0,3),'InputHost/HostTerminalCleanup.cs',...legacyNames.slice(3)]);
const contracts=new Map([
 [LEGACY_MANAGED_CONTRACT,Object.freeze({id:LEGACY_MANAGED_CONTRACT,count:21,names:legacyNames,terminalCleanup:false})],
 [TERMINAL_MANAGED_CONTRACT,Object.freeze({id:TERMINAL_MANAGED_CONTRACT,count:22,names:terminalNames,terminalCleanup:true})],
]);
export function managedCompileContract(id=LEGACY_MANAGED_CONTRACT){
 const contract=contracts.get(id);
 if(!contract)throw Error('CURRENT_MANAGED_CONTRACT_UNKNOWN');
 return contract;
}
export function validateManagedCompileRows(rows,id=LEGACY_MANAGED_CONTRACT){
 const contract=managedCompileContract(id);
 if(!Array.isArray(rows)||rows.length!==contract.count||new Set(rows.map(row=>row?.relative)).size!==contract.count)
  throw Error('CURRENT_MANAGED_EXACT'+contract.count+'_MEMBERS_REQUIRED');
 const known=new Set(contract.names);
 for(const row of rows)if(!row||!known.has(row.relative)||!/^[a-f0-9]{64}$/.test(row.sha256??''))
  throw Error('CURRENT_MANAGED_MEMBER_NOT_IN_'+contract.id);
 return contract;
}
