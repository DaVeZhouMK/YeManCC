import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {readVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
const expanded=readVirtualControllerSource(process.argv[2]);
let source=expanded.source.replaceAll('\r\n','\n');
// This pre-existing wake module owns one fingerprinted helper. It remains a
// normal include in the three compiling consumers; read it only for this hash.
if(/^#include "virtual_wake_execution_runtime.h"/m.test(source)){
 const file=path.join(path.dirname(path.resolve(process.argv[2])),'virtual_wake_execution_runtime.h'),bytes=fs.readFileSync(file);
 source+='\n'+bytes.toString('utf8').replaceAll('\r\n','\n');
 expanded.manifest.fingerprintSupplementalInputs=[{file,sha256:createHash('sha256').update(bytes).digest('hex')}];
}
expanded.manifest.fingerprintSourceSha256=createHash('sha256').update(source).digest('hex');
// Preserve the fingerprint JSON contract; optional provenance binds every source.
if(process.argv[3]==='--binding'&&process.argv[4])fs.writeFileSync(process.argv[4],JSON.stringify(expanded.manifest,null,2)+'\n',{flag:'wx'});
else if(process.argv.length!==3)throw Error('Use MAIN_CPP [--binding NEW_JSON]');
const mask=source.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, match=>' '.repeat(match.length));
const functions=['msiClawKnownBoardAuthority','msiControllerNeutralCleanupAllowed','msiUiBoundTargetAdmitted','focusIsolationPlanTick','msiUiOwnsInput','msiUiOutputNote','msiVirtualWakeFacts','msiVirtualWakeWaitSource','msiVirtualWakeTryRearmLocked','msiDinputRouteWindowDecideForWake','inputHostWakeMaxAgeMs','inputHostWakeArmTick','inputHostWakeWorkReason','inputHostPerformWakeHotReplace','inputHostRequestWakeRescan','inputHostConsumeWakeRescan'];
const hashes={};
for(const name of functions){
 const pattern=new RegExp('static\\s+[^;{}]*\\b'+name+'\\([^;{}]*\\)\\s*\\{','g');const hits=[...mask.matchAll(pattern)];if(hits.length!==1)throw Error('Expected one function '+name+': '+hits.length);const hit=hits[0];
 const start=hit.index, open=mask.indexOf('{',start);let end=open+1, depth=1;
 while(depth){if(mask[end]==='{')depth++;if(mask[end]==='}')depth--;end++;if(end>mask.length)throw Error('Bad block '+name);}
 hashes[name]=createHash('sha256').update(source.slice(start,end)).digest('hex');
}
for(const marker of ['struct FocusPlan {','static std::atomic<bool> g_msiVirtualReleaseRearmEligible']){
 const start=source.indexOf(marker);if(start<0)throw Error('Missing declaration '+marker);
 let end=source.indexOf(';',start)+1;
 if(marker.startsWith('struct')){const open=mask.indexOf('{',start);end=open+1;let depth=1;while(depth){if(mask[end]==='{')depth++;if(mask[end]==='}')depth--;end++;}}
 hashes[marker]=createHash('sha256').update(source.slice(start,end)).digest('hex');
}
for(const marker of ['if(msiClawKnownBoardAuthority())g_msiVirtualReleaseRearmEligible.store(true,std::memory_order_release);','const bool msiUiNeutral = msiUiOwnsInput(focusPlan);','msiUiOutputNote(msiUiNeutral, submitNeutral, hostFrame, live, inputSource);']){
 if(!source.includes(marker))throw Error('Required integration disappeared: '+marker);hashes[marker]=true;
}
console.log(JSON.stringify(hashes,null,2));
