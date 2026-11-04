import path from 'node:path';
// Explicit build-only managed membership versions. Neither a JSON label nor
// this contract grants runtime authority. Legacy21 remains immutable; only the
// named terminal contracts compile the unique production terminal policy.
export const LEGACY_MANAGED_CONTRACT='current105-exact21-v1';
export const TERMINAL_MANAGED_CONTRACT='current105-terminal22-v1';
export const TERMINAL_SD_DECODER_MANAGED_CONTRACT='current105-terminal23-sd-decoder-v1';
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
const terminalSdDecoderNames=Object.freeze([...terminalNames,'InputHost/SteamDeckPadReportObservation.cs']);
// Build-only membership. The new decoder version grants no capture, compile-batch or runtime authority.
const contracts=new Map([
 [LEGACY_MANAGED_CONTRACT,Object.freeze({id:LEGACY_MANAGED_CONTRACT,count:21,names:legacyNames,terminalCleanup:false,steamDeckPadDecoder:false})],
 [TERMINAL_MANAGED_CONTRACT,Object.freeze({id:TERMINAL_MANAGED_CONTRACT,count:22,names:terminalNames,terminalCleanup:true,steamDeckPadDecoder:false})],
 [TERMINAL_SD_DECODER_MANAGED_CONTRACT,Object.freeze({id:TERMINAL_SD_DECODER_MANAGED_CONTRACT,count:23,names:terminalSdDecoderNames,terminalCleanup:true,steamDeckPadDecoder:true})],
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

// Exact stage versions only. Membership and receipt relations grant no live authority.
export function requireTerminalManagedContract(id=TERMINAL_MANAGED_CONTRACT){
 const contract=managedCompileContract(id);
 if(id!==TERMINAL_MANAGED_CONTRACT&&id!==TERMINAL_SD_DECODER_MANAGED_CONTRACT)throw Error('CURRENT_MANAGED_TERMINAL_CONTRACT_REQUIRED');
 return contract;
}
export function terminalManagedStageStatus(stage,id=TERMINAL_MANAGED_CONTRACT){
 const contract=requireTerminalManagedContract(id);
 if(stage!=='managed'&&stage!=='native')throw Error('CURRENT_MANAGED_STAGE_UNKNOWN');
 return (stage==='managed'?'MANAGED':'NATIVE')+contract.count+'_COMPILED_NOT_RUNTIME_ACCEPTED';
}
export function terminalLifecycleDeclarationKind(id=TERMINAL_MANAGED_CONTRACT){
 return 'TERMINAL'+requireTerminalManagedContract(id).count+'_LIFECYCLE_COMPILED_INPUTS';
}
export function terminalManagedContractFromDeclaration(kind){
 for(const id of [TERMINAL_MANAGED_CONTRACT,TERMINAL_SD_DECODER_MANAGED_CONTRACT])if(kind===terminalLifecycleDeclarationKind(id))return requireTerminalManagedContract(id);
 throw Error('CURRENT_MANAGED_LIFECYCLE_DECLARATION_UNKNOWN');
}
// Pure cross-record checks only. Callers still verify file SHA, owned paths,
// source capture, producer identity and the original compiled-artifact receipts.
export function verifyManagedDecoderCompileEvidence(value,id,authorizedBuildRoot){
 const contract=managedCompileContract(id);if(!contract.steamDeckPadDecoder)return null;
 const need=(ok,code)=>{if(!ok)throw Error('CURRENT_MANAGED_DECODER_'+code);};
 need(typeof authorizedBuildRoot==='string'&&path.isAbsolute(authorizedBuildRoot),'AUTHORITY_ROOT_REQUIRED');
 const owned=file=>{const rel=path.relative(authorizedBuildRoot,file);return rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
 need(value?.status==='BUILT'&&value.managedInputContract===contract.id&&value.managedInputCount===contract.count&&value.terminalCleanupCompiled===true&&value.steamDeckPadDecoderCompiled===true&&value.sourceStable===true&&value.toolchainStable===true&&value.fixtureExecuted===false&&value.realSdkLoaded===false&&value.hardwareIo===false,'EXPLICIT_BUILD_CONTRACT_REQUIRED');
 validateManagedCompileRows(value.inputBindings?.sources,contract.id);
 const source=value.inputBindings.sources.find(row=>row.relative==='InputHost/SteamDeckPadReportObservation.cs');
 need(path.isAbsolute(source.path??'')&&path.isAbsolute(source.snapshot??''),'ABSOLUTE_DECLARED_SOURCE_REQUIRED');
 need(typeof value.fixtureProject==='string'&&path.isAbsolute(value.fixtureProject),'ABSOLUTE_PROJECT_REQUIRED');
 need(owned(value.fixtureProject)&&owned(source.snapshot),'OWNED_PROJECT_AND_SNAPSHOT_REQUIRED');
 const expected=path.join(path.dirname(value.fixtureProject),'SteamDeckPadReportObservation.cs'),key=file=>typeof file==='string'&&path.isAbsolute(file)?path.resolve(file).toLowerCase():null;
 need(Array.isArray(value.inputBindings.generatedInputs)&&Array.isArray(value.evaluatedCompilerInputs),'ACTUAL_INPUT_ROWS_REQUIRED');
 const generated=value.inputBindings.generatedInputs.filter(row=>key(row?.path)===key(expected));
 const compiled=value.evaluatedCompilerInputs.filter(row=>row?.kind==='Compile'&&key(row.path)===key(expected));
 need(generated.length===1&&compiled.length===1,'ONE_EXACT_GENERATED_AND_COMPILE_MEMBER_REQUIRED');
 need(generated[0].sha256===source.sha256&&compiled[0].sha256===source.sha256,'SOURCE_SNAPSHOT_GENERATED_COMPILE_IDENTITY_REQUIRED');
 return {contractId:contract.id,source,generated:generated[0],compiled:compiled[0],runtimeAuthorityGranted:false};
}
