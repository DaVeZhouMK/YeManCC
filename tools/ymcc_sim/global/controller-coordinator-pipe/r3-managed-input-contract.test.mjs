import test from 'node:test';
import assert from 'node:assert/strict';
import {LEGACY_MANAGED_CONTRACT,TERMINAL_MANAGED_CONTRACT,TERMINAL_SD_DECODER_MANAGED_CONTRACT,managedCompileContract,validateManagedCompileRows} from './r3-managed-input-contract.mjs';
import {requireCurrentCompileBatch} from './r3-current-compile-batch.mjs';
const rows=kind=>managedCompileContract(kind).names.map(relative=>({relative,sha256:'a'.repeat(64)}));
test('legacy21 remains the default and does not contain terminal22',()=>{
 const contract=managedCompileContract();assert.equal(contract.id,LEGACY_MANAGED_CONTRACT);assert.equal(contract.count,21);
 assert.equal(contract.names.includes('InputHost/HostTerminalCleanup.cs'),false);assert.equal(validateManagedCompileRows(rows(LEGACY_MANAGED_CONTRACT)).id,contract.id);
});
test('terminal22 explicitly requires the production terminal module',()=>{
 const contract=managedCompileContract(TERMINAL_MANAGED_CONTRACT);assert.equal(contract.count,22);
 assert.equal(contract.names.filter(n=>n==='InputHost/HostTerminalCleanup.cs').length,1);
 assert.equal(validateManagedCompileRows(rows(TERMINAL_MANAGED_CONTRACT),contract.id),contract);
});
test('a terminal22 member list cannot be relabeled legacy21',()=>assert.throws(()=>validateManagedCompileRows(rows(TERMINAL_MANAGED_CONTRACT)),/EXACT21/));
test('legacy21 cannot pass a terminal22 contract',()=>assert.throws(()=>validateManagedCompileRows(rows(LEGACY_MANAGED_CONTRACT),TERMINAL_MANAGED_CONTRACT),/EXACT22/));
test('missing duplicate foreign malformed and extra terminal members are refused',()=>{
 const good=rows(TERMINAL_MANAGED_CONTRACT);
 for(const bad of [good.slice(1),[...good,good[0]],good.map((r,i)=>i===3?good[0]:r),good.map((r,i)=>i===3?{...r,relative:'InputHost/FakeTerminal.cs'}:r),good.map((r,i)=>i===3?{...r,sha256:'broken'}:r)])
  assert.throws(()=>validateManagedCompileRows(bad,TERMINAL_MANAGED_CONTRACT));
});
test('unknown or guessed protocol versions cannot grant membership',()=>{
 for(const kind of ['terminal22',22,null,'current105-exact22-v1',{},''])assert.throws(()=>managedCompileContract(kind),/UNKNOWN/);
});
test('membership contracts are immutable and never compile/runtime capability tokens',()=>{
 const contract=managedCompileContract(TERMINAL_MANAGED_CONTRACT);assert(Object.isFrozen(contract));assert(Object.isFrozen(contract.names));
 for(const fake of [contract,{kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY'},JSON.parse(JSON.stringify(contract))])
  assert.throws(()=>requireCurrentCompileBatch(fake),/LIVE_BRANDED_BATCH_REQUIRED/);
});

test('terminal23 adds exactly one real SD decoder to the unchanged terminal22 membership',()=>{
 const legacy=managedCompileContract(LEGACY_MANAGED_CONTRACT),terminal=managedCompileContract(TERMINAL_MANAGED_CONTRACT);
 const contract=managedCompileContract(TERMINAL_SD_DECODER_MANAGED_CONTRACT),decoder='InputHost/SteamDeckPadReportObservation.cs';
 assert.equal(contract.id,'current105-terminal23-sd-decoder-v1');assert.equal(contract.count,23);
 assert.deepEqual(contract.names,[...terminal.names,decoder]);assert.equal(new Set(contract.names).size,23);
 assert.equal(contract.names.filter(n=>n===decoder).length,1);assert.equal(contract.names.filter(n=>n==='InputHost/HostTerminalCleanup.cs').length,1);
 assert.equal(contract.terminalCleanup,true);assert.equal(contract.steamDeckPadDecoder,true);
 for(const old of [legacy,terminal]){assert.equal(old.names.includes(decoder),false);assert.equal(old.steamDeckPadDecoder,false);}
 assert.equal(validateManagedCompileRows(rows(contract.id),contract.id),contract);
});
test('terminal23 requires the decoder and cannot accept a terminal22-only member list',()=>{
 const good=rows(TERMINAL_SD_DECODER_MANAGED_CONTRACT);
 assert.throws(()=>validateManagedCompileRows(good.filter(r=>r.relative!=='InputHost/SteamDeckPadReportObservation.cs'),TERMINAL_SD_DECODER_MANAGED_CONTRACT),/EXACT23/);
 assert.throws(()=>validateManagedCompileRows(rows(TERMINAL_MANAGED_CONTRACT),TERMINAL_SD_DECODER_MANAGED_CONTRACT),/EXACT23/);
});
test('terminal23 rejects duplicate foreign malformed and extra members',()=>{
 const kind=TERMINAL_SD_DECODER_MANAGED_CONTRACT,good=rows(kind),last=good.length-1;
 for(const bad of [good.map((r,i)=>i===last?good[0]:r),[...good,good[last]],good.map((r,i)=>i===last?{...r,relative:'InputHost/FakeDecoder.cs'}:r),good.map((r,i)=>i===last?{...r,sha256:'broken'}:r)])
  assert.throws(()=>validateManagedCompileRows(bad,kind));
});
test('terminal23 cannot be relabeled as legacy21 or terminal22',()=>{
 const good=rows(TERMINAL_SD_DECODER_MANAGED_CONTRACT);
 assert.throws(()=>validateManagedCompileRows(good,LEGACY_MANAGED_CONTRACT),/EXACT21/);
 assert.throws(()=>validateManagedCompileRows(good,TERMINAL_MANAGED_CONTRACT),/EXACT22/);
});
test('terminal23 decoder substitution is refused at the original legacy21 and terminal22 counts',()=>{
 for(const old of [LEGACY_MANAGED_CONTRACT,TERMINAL_MANAGED_CONTRACT]){
  const good=rows(old),bad=good.map((r,i)=>i===good.length-1?{...r,relative:'InputHost/SteamDeckPadReportObservation.cs'}:r);
  assert.throws(()=>validateManagedCompileRows(bad,old),/MEMBER_NOT_IN_/);
 }
});
test('terminal23 immutable membership is not a live compile or runtime capability',()=>{
 const contract=managedCompileContract(TERMINAL_SD_DECODER_MANAGED_CONTRACT);assert(Object.isFrozen(contract));assert(Object.isFrozen(contract.names));
 for(const fake of [contract,{kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY',managedInputContract:contract.id},JSON.parse(JSON.stringify(contract))])
  assert.throws(()=>requireCurrentCompileBatch(fake),/LIVE_BRANDED_BATCH_REQUIRED/);
});

import path from 'node:path';
import {requireTerminalManagedContract,terminalManagedStageStatus,terminalLifecycleDeclarationKind,terminalManagedContractFromDeclaration,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';
import {parseTerminalManagedPreparationArguments} from './r3-terminal22-prepare.mjs';
import {terminal22LifecyclePreparationPins} from './r3-terminal22-qualified-observe.mjs';
import {currentLifecycleModePlan} from './r3-current-run.mjs';

const decoder23Id=TERMINAL_SD_DECODER_MANAGED_CONTRACT,decoder23UnitBuildRoot=path.resolve('G:/unit-decoder-contract-output');
function decoder23Receipt(){
 const root=path.resolve('G:/unit-decoder-contract-output'),fixtureProject=path.join(root,'Fixture.csproj');
 const sources=rows(decoder23Id).map(row=>({...row,path:path.join(path.resolve('G:/unit-decoder-contract-source'),row.relative),snapshot:path.join(root,'input-sources',row.relative)}));
 const decoder=path.join(root,'SteamDeckPadReportObservation.cs'),sha256='a'.repeat(64);
 return {status:'BUILT',managedInputContract:decoder23Id,managedInputCount:23,terminalCleanupCompiled:true,steamDeckPadDecoderCompiled:true,sourceStable:true,toolchainStable:true,fixtureExecuted:false,realSdkLoaded:false,hardwareIo:false,fixtureProject,inputBindings:{sources,generatedInputs:[{path:decoder,sha256}]},evaluatedCompilerInputs:[{kind:'Compile',path:decoder,sha256}]};
}
test('decoder23 entry: explicit preparation selection preserves default22 and rejects ambiguous versions',()=>{
 assert.equal(parseTerminalManagedPreparationArguments([])['--managed-input-contract'],TERMINAL_MANAGED_CONTRACT);
 assert.equal(parseTerminalManagedPreparationArguments([])['--stage'],'managed');
 assert.equal(parseTerminalManagedPreparationArguments(['--stage','capture'])['--stage'],'capture');
 for(const args of [['--stage','runtime'],['--stage','capture','--stage','managed'],['--stage']])assert.throws(()=>parseTerminalManagedPreparationArguments(args));
 const options=parseTerminalManagedPreparationArguments(['--repo','G:/source','--workspace','G:/owner','--managed-input-contract',decoder23Id]);assert.equal(options['--managed-input-contract'],decoder23Id);
 for(const args of [['--managed-input-contract',LEGACY_MANAGED_CONTRACT],['--managed-input-contract','23'],['--managed-input-contract','current105-terminal24-v1'],['--managed-input-contract',decoder23Id,'--managed-input-contract',decoder23Id],['--managed-input-contract'],['--workspace','--repo']])assert.throws(()=>parseTerminalManagedPreparationArguments(args));
});
test('decoder23 entry: stage status follows only the exact selected contract',()=>{
 assert.equal(terminalManagedStageStatus('managed'),'MANAGED22_COMPILED_NOT_RUNTIME_ACCEPTED');assert.equal(terminalManagedStageStatus('native'),'NATIVE22_COMPILED_NOT_RUNTIME_ACCEPTED');
 assert.equal(terminalManagedStageStatus('managed',decoder23Id),'MANAGED23_COMPILED_NOT_RUNTIME_ACCEPTED');assert.equal(terminalManagedStageStatus('native',decoder23Id),'NATIVE23_COMPILED_NOT_RUNTIME_ACCEPTED');
 assert.throws(()=>terminalManagedStageStatus('runtime',decoder23Id),/STAGE_UNKNOWN/);assert.throws(()=>requireTerminalManagedContract(LEGACY_MANAGED_CONTRACT),/TERMINAL_CONTRACT_REQUIRED/);
});
test('decoder23 entry: lifecycle declaration keeps22 and23 identities separate without authority',()=>{
 const roles=[...new Set(currentLifecycleModePlan().map(row=>row.role))],preparations=roles.map((role,i)=>({role,path:path.resolve('G:/unit-declaration/'+i+'.json'),sha256:'a'.repeat(64)}));
 for(const id of [TERMINAL_MANAGED_CONTRACT,decoder23Id]){const kind=terminalLifecycleDeclarationKind(id);assert.equal(terminalManagedContractFromDeclaration(kind).id,id);assert.deepEqual(terminal22LifecyclePreparationPins({schemaVersion:1,kind,preparations}),preparations);}
 for(const kind of ['TERMINAL21_LIFECYCLE_COMPILED_INPUTS','TERMINAL24_LIFECYCLE_COMPILED_INPUTS','TERMINAL23_QUALIFIED',decoder23Id])assert.throws(()=>terminalManagedContractFromDeclaration(kind));
 assert.notEqual(terminalLifecycleDeclarationKind(decoder23Id),terminalLifecycleDeclarationKind());
});
test('decoder23 entry: decoder source generated and evaluated Compile relation grants no runtime authority',()=>{
 const value=decoder23Receipt(),proof=verifyManagedDecoderCompileEvidence(value,decoder23Id,decoder23UnitBuildRoot);assert.equal(proof.source.relative,'InputHost/SteamDeckPadReportObservation.cs');assert.equal(proof.generated.path,proof.compiled.path);assert.equal(proof.generated.sha256,proof.source.sha256);assert.equal(proof.runtimeAuthorityGranted,false);
 assert.throws(()=>verifyManagedDecoderCompileEvidence(decoder23Receipt(),decoder23Id),/AUTHORITY_ROOT_REQUIRED/);
 assert.equal(verifyManagedDecoderCompileEvidence({},TERMINAL_MANAGED_CONTRACT),null);assert.equal(verifyManagedDecoderCompileEvidence({},LEGACY_MANAGED_CONTRACT),null);
});
test('decoder23 entry: flags cannot replace actual decoder membership generated or Compile identity',()=>{
 for(const mutate of [v=>v.status='FAILED',v=>v.managedInputCount=22,v=>v.managedInputContract=TERMINAL_MANAGED_CONTRACT,v=>v.sourceStable=false,v=>v.toolchainStable=false,v=>v.fixtureExecuted=true,v=>v.steamDeckPadDecoderCompiled=false,v=>v.inputBindings.sources.pop(),v=>v.inputBindings.generatedInputs=[],v=>v.evaluatedCompilerInputs=[],v=>v.evaluatedCompilerInputs[0].kind='Reference',v=>v.evaluatedCompilerInputs[0].sha256='b'.repeat(64),v=>v.inputBindings.generatedInputs[0].sha256='b'.repeat(64),v=>v.fixtureProject='relative.csproj',v=>v.fixtureProject=path.resolve('G:/foreign-owner/Fixture.csproj'),v=>v.inputBindings.sources.find(row=>row.relative==='InputHost/SteamDeckPadReportObservation.cs').snapshot=path.resolve('G:/foreign-owner/decoder.cs')]){const value=decoder23Receipt();mutate(value);assert.throws(()=>verifyManagedDecoderCompileEvidence(value,decoder23Id,decoder23UnitBuildRoot));}
});
test('decoder23 entry: duplicate and unrelated decoder paths cannot satisfy the real project member',()=>{
 for(const mutate of [v=>v.evaluatedCompilerInputs.push({...v.evaluatedCompilerInputs[0]}),v=>v.inputBindings.generatedInputs.push({...v.inputBindings.generatedInputs[0]}),v=>{v.evaluatedCompilerInputs[0].path=path.resolve('G:/different-owner/SteamDeckPadReportObservation.cs');v.inputBindings.generatedInputs[0].path=v.evaluatedCompilerInputs[0].path;},v=>v.inputBindings.sources.find(row=>row.relative==='InputHost/SteamDeckPadReportObservation.cs').snapshot='relative.cs']){const value=decoder23Receipt();mutate(value);assert.throws(()=>verifyManagedDecoderCompileEvidence(value,decoder23Id,decoder23UnitBuildRoot));}
});


import {TERMINAL_SIGNAL_EVIDENCE_MANAGED_CONTRACT,validateManagedSourceRequirements} from './r3-managed-input-contract.mjs';
import contractFs from 'node:fs';
const signal26Id=TERMINAL_SIGNAL_EVIDENCE_MANAGED_CONTRACT;
const signal26Helpers=['InputHost/HostSdkFirstWitness.cs','InputHost/TargetCreationLifetimeEvidence.cs','InputHost/CompactFrameReplyBinding.cs'];
function signal26Receipt(){
 const value=decoder23Receipt(),root=path.dirname(value.fixtureProject),sha256='a'.repeat(64);
 value.managedInputContract=signal26Id;value.managedInputCount=26;value.signalEvidenceCompiled=true;
 value.inputBindings.sources=rows(signal26Id).map(row=>({...row,path:path.join(path.resolve('G:/unit-decoder-contract-source'),row.relative),snapshot:path.join(root,'input-sources',row.relative)}));
 for(const relative of signal26Helpers){const file=path.join(root,path.basename(relative));value.inputBindings.generatedInputs.push({path:file,sha256});value.evaluatedCompilerInputs.push({kind:'Compile',path:file,sha256});}
 return value;
}
test('signal26 entry: exact actual helpers extend unchanged23 without granting live authority',()=>{
 const contract=managedCompileContract(signal26Id);
 assert.equal(contract.count,26);assert.equal(contract.signalEvidence,true);
 assert.deepEqual(contract.names,[...managedCompileContract(decoder23Id).names,...signal26Helpers]);
 assert.equal(new Set(contract.names).size,26);assert(Object.isFrozen(contract.names));
 assert.equal(validateManagedCompileRows(rows(signal26Id),signal26Id),contract);
 assert.throws(()=>requireCurrentCompileBatch(contract),/LIVE_BRANDED_BATCH_REQUIRED/);
});
test('signal26 entry: missing duplicate foreign extra and mislabeled memberships remain refused',()=>{
 const good=rows(signal26Id);
 for(const bad of [good.slice(1),[...good,good[0]],good.map((r,i)=>i===25?good[0]:r),good.map((r,i)=>i===25?{...r,relative:'InputHost/Foreign.cs'}:r),good.map((r,i)=>i===25?{...r,sha256:'bad'}:r)])
  assert.throws(()=>validateManagedCompileRows(bad,signal26Id));
 for(const old of [LEGACY_MANAGED_CONTRACT,TERMINAL_MANAGED_CONTRACT,decoder23Id]){
  assert.throws(()=>validateManagedCompileRows(good,old));
  assert.throws(()=>validateManagedCompileRows(rows(old),signal26Id));
 }
});
test('signal26 entry: original preparation and declaration route only the explicit new version',()=>{
 assert.equal(parseTerminalManagedPreparationArguments(['--managed-input-contract',signal26Id])['--managed-input-contract'],signal26Id);
 assert.equal(parseTerminalManagedPreparationArguments([])['--managed-input-contract'],TERMINAL_MANAGED_CONTRACT);
 assert.equal(terminalManagedStageStatus('managed',signal26Id),'MANAGED26_COMPILED_NOT_RUNTIME_ACCEPTED');
 assert.equal(terminalManagedStageStatus('native',signal26Id),'NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED');
 assert.equal(terminalManagedContractFromDeclaration(terminalLifecycleDeclarationKind(signal26Id)).id,signal26Id);
 for(const id of ['current105-terminal25-v1','current105-terminal27-v1','26'])assert.throws(()=>requireTerminalManagedContract(id));
});
test('signal26 compile identity: actual source generated and Compile membership required for all three helpers',()=>{
 const proof=verifyManagedDecoderCompileEvidence(signal26Receipt(),signal26Id,decoder23UnitBuildRoot);
 assert.equal(proof.signalEvidence.length,3);assert.equal(proof.runtimeAuthorityGranted,false);
 for(const relative of signal26Helpers){
  const file=path.join(decoder23UnitBuildRoot,path.basename(relative));
  for(const mutate of [
   v=>v.signalEvidenceCompiled=false,
   v=>v.inputBindings.generatedInputs=v.inputBindings.generatedInputs.filter(r=>r.path!==file),
   v=>v.evaluatedCompilerInputs=v.evaluatedCompilerInputs.filter(r=>r.path!==file),
   v=>v.evaluatedCompilerInputs.push({...v.evaluatedCompilerInputs.find(r=>r.path===file)}),
   v=>v.evaluatedCompilerInputs.find(r=>r.path===file).sha256='b'.repeat(64),
   v=>v.inputBindings.generatedInputs.find(r=>r.path===file).sha256='b'.repeat(64),
   v=>v.inputBindings.sources.find(r=>r.relative===relative).snapshot=path.resolve('G:/foreign-owner/'+path.basename(relative)),
  ]){const value=signal26Receipt();mutate(value);assert.throws(()=>verifyManagedDecoderCompileEvidence(value,signal26Id,decoder23UnitBuildRoot));}
 }
});
test('signal26 preflight: current consumed type requirements reject legacy before expensive capture',()=>{
 const source=contractFs.readFileSync(new URL('../../../../InputHost/Program.cs',import.meta.url),'utf8');
 const proof=validateManagedSourceRequirements(source,signal26Id);
 assert.deepEqual(new Set(proof.required),new Set(signal26Helpers));
 assert.equal(proof.compileReadyProven,false);assert.equal(proof.runtimeAuthorityGranted,false);
 for(const id of [LEGACY_MANAGED_CONTRACT,TERMINAL_MANAGED_CONTRACT,decoder23Id])
  assert.throws(()=>validateManagedSourceRequirements(source,id),/SOURCE_DEPENDENCIES_MISSING/);
 assert.equal(validateManagedSourceRequirements('class OldBaseline {}',decoder23Id).compileReadyProven,false);
});

test('signal26 preparation: known missing dependencies reject before any workspace or capture creation',async()=>{
 const {fileURLToPath}=await import('node:url');
 const {randomUUID}=await import('node:crypto');
 const {prepareTerminal22}=await import('./r3-terminal22-prepare.mjs');
 const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
 const workspace=path.resolve(repo,'../../../Testing/YMCC/Workspace','Runtime105-preflight-'+randomUUID());
 assert.equal(contractFs.existsSync(workspace),false);
 await assert.rejects(prepareTerminal22(['--repo',repo,'--workspace',workspace,'--managed-input-contract',decoder23Id]),/SOURCE_DEPENDENCIES_MISSING/);
 assert.equal(contractFs.existsSync(workspace),false);
});
