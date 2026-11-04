import {terminal22SteamCompileSpec,terminal23CompileSourceProfile} from './input_coordinator_terminal22_source_adapters.mjs';
import fs from'node:fs';import assert from'node:assert/strict';import{createHash}from'node:crypto';import{extract}from'./fixtures/legion_motion_consumer/extract_main.mjs';
import {fileURLToPath} from 'node:url';
const sha=b=>createHash('sha256').update(b).digest('hex'),repo=fileURLToPath(new URL('../',import.meta.url)),profile=JSON.parse(fs.readFileSync(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',import.meta.url),'utf8')),spec={...profile,steamRecovery:{...profile.steamRecovery,runtimeHeader:{...profile.steamRecovery.runtimeHeader,path:repo+profile.steamRecovery.runtimeHeader.relative}}};

export function currentCompileSourceDataProfile({sourceRoot,managedInputContract}={}){
 assert(managedInputContract===undefined||managedInputContract==='current105-exact21-v1'||managedInputContract==='current105-terminal22-v1'||managedInputContract==='current105-terminal23-sd-decoder-v1','UNKNOWN_COMPILE_SOURCE_CONTRACT');
 return managedInputContract==='current105-terminal23-sd-decoder-v1'?terminal23CompileSourceProfile(sourceRoot):spec;
}
export function guardCurrentSteam(original,{sourceRoot,managedInputContract}={}){
 assert(managedInputContract===undefined||managedInputContract==='current105-exact21-v1'||managedInputContract==='current105-terminal22-v1'||managedInputContract==='current105-terminal23-sd-decoder-v1','UNKNOWN_COMPILE_SOURCE_CONTRACT');
 const current23=managedInputContract==='current105-terminal23-sd-decoder-v1';
 const guardSpec=current23?terminal23CompileSourceProfile(sourceRoot):managedInputContract==='current105-terminal22-v1'?terminal22SteamCompileSpec(sourceRoot):spec;
 let inert=original;const header='#include "steam_original_recovery_runtime.h"';assert.equal(inert.split(header).length,2);
 assert.equal(sha(fs.readFileSync(guardSpec.steamRecovery.runtimeHeader.path)),guardSpec.steamRecovery.runtimeHeader.sha256);
 const actualHeader=fs.readFileSync(guardSpec.steamRecovery.runtimeHeader.path,'utf8');assert.equal(sha(extract(actualHeader,'steamOriginalRecoveryStart').code),guardSpec.steamRecovery.startSha256);
 const startWrapper='static void steamOriginalRecoveryStart(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-start");}';
 const exportWrapper='static std::wstring steamOriginalRecoveryExportSnapshot(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-export");}';
 if(current23)assert.equal(sha(extract(actualHeader,'steamOriginalRecoveryExportSnapshot').code),guardSpec.steamRecovery.exportSnapshotSha256);
 const replacement=current23?['#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart','#define steamOriginalRecoveryExportSnapshot fixtureNeverCallOriginalSteamRecoveryExportSnapshot',header,'#undef steamOriginalRecoveryExportSnapshot','#undef steamOriginalRecoveryStart',startWrapper,exportWrapper].join('\n')
  :['#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart',header,'#undef steamOriginalRecoveryStart',startWrapper].join('\n');
 inert=inert.replace(header,replacement);const bridge=extract(inert,'steamOriginalRecoveryEngineHandle').code;
 assert.equal(sha(bridge.replaceAll('\r\n','\n')),guardSpec.steamRecovery.bridgeSha256);
 const changed=bridge.slice(0,bridge.indexOf('{')+1)+'\n++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-engine");\n}';
 assert.equal(inert.split(bridge).length,2);inert=inert.replace(bridge,changed);
 const inverse=inert.replace(changed,()=>bridge).replace(replacement,()=>header);assert.equal(inverse,original);
 const substitutions=[{symbol:'steamOriginalRecoveryStart',beforeSha256:guardSpec.steamRecovery.startSha256,afterSha256:sha(replacement),rejectOnly:true,scope:'Rename only real included Start definition; public entry throws for offline test'},{symbol:'steamOriginalRecoveryEngineHandle',beforeSha256:sha(bridge),afterSha256:sha(changed),rejectOnly:true,scope:'Every recovery engine bridge call throws in offline test'}];
 if(current23)substitutions.push({symbol:'steamOriginalRecoveryExportSnapshot',beforeSha256:guardSpec.steamRecovery.exportSnapshotSha256,afterSha256:sha(exportWrapper),rejectOnly:true,scope:'Rename the real included snapshot exporter; public entry throws before independent saveJson'});
 return {inert,nativeGuardProvenance:guardSpec.nativeGuardProvenance??null,substitutions};
}
export function observeCurrent105Fixture(original,options={}){const observationSpec=currentCompileSourceDataProfile(options);assert.equal(sha(original),observationSpec.fixture.sha256,'Exact captured current fixture');const review={changes:observationSpec.fixtureObservationChanges};let text=original;const applied=[];for(const row of review.changes){const lfBefore=row.before.replaceAll('\r\n','\n'),lfAfter=row.after.replaceAll('\r\n','\n'),beforeCandidates=[...new Set([lfBefore,lfBefore.replaceAll('\n','\r\n')])],hits=beforeCandidates.flatMap(before=>Array.from({length:text.split(before).length-1},()=>before));assert.equal(hits.length,row.count,'Actual unchanged observation anchor '+row.label);for(const before of hits){const after=before.includes('\r\n')?lfAfter.replaceAll('\n','\r\n'):lfAfter;text=text.replace(before,()=>after);applied.push({before,after});}}let inverse=text;for(const row of [...applied].reverse())inverse=inverse.replace(row.after,()=>row.before);assert.equal(inverse,original);return {text,originalSha256:sha(original),observedSha256:sha(text),originalBusinessChecksChanged:false};}
