// Library upgrade guards; mutate isolated files, never the repository sources.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';import {fileURLToPath,pathToFileURL} from 'node:url';
const args=process.argv.slice(2),opt=(key,fallback)=>{const i=args.indexOf(key);return i<0?fallback:args[i+1];};
const repo=path.resolve(opt('--repo',path.join(path.dirname(fileURLToPath(import.meta.url)),'..'))),out=path.resolve(opt('--out','./game-motor-catalog-tests'));
if(fs.existsSync(out))throw Error('Fresh evidence required');fs.mkdirSync(out,{recursive:true});
const policy=path.resolve(opt('--policy',path.join(repo,'capabilities/hc-controller-feedback-policy.json'))),receipt=path.resolve(opt('--contract',path.join(repo,'capabilities/CONTROLLER-GAME-MOTOR-RECEIPT.json'))),generator=path.resolve(opt('--generator',path.join(repo,'tools/lib/hc_controller_feedback_catalog.mjs')));
const {buildControllerFeedbackArtifacts}=await import(pathToFileURL(generator));
const sandbox=path.join(out,'sandbox'),inputs=[],sha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function copy(file,relative){const dest=path.join(sandbox,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(file,dest);inputs.push({file,sha256:sha(file)});}
copy(policy,'capabilities/hc-controller-feedback-policy.json');copy(receipt,'capabilities/CONTROLLER-GAME-MOTOR-RECEIPT.json');copy(path.join(repo,'capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json'),'capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json');copy(path.join(repo,'capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json'),'capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json');
copy(path.join(repo,'capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json'),'capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json');
const controller=path.join(repo,'capabilities/CONTROLLER-CONSUMPTION.json'),inventory=JSON.parse(fs.readFileSync(controller,'utf8')).sourceInventory;copy(controller,'capabilities/CONTROLLER-CONSUMPTION.json');copy(path.join(repo,inventory.sourceIndex),inventory.sourceIndex);
for(const relative of new Set([...inventory.files.map(r=>r.file),'Targets/VIIPERTarget.cs','Targets/DualSenseTarget.cs','Targets/SteamDeckTarget.cs','Targets/Xbox360Target.cs']))copy(path.join(repo,'deps/handheldcompanion-runtime/source',relative),path.join('deps/handheldcompanion-runtime/source',relative));
copy(path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll'),'deps/hidmaestro/HIDMaestro.Core.dll');inputs.push({file:generator,sha256:sha(generator)},{file:fileURLToPath(import.meta.url),sha256:sha(fileURLToPath(import.meta.url))});
const positive=buildControllerFeedbackArtifacts({repo:sandbox});
if(positive.metadata.gameMotorReceiptProfiles.length!==3||positive.metadata.registeredProviders!==2)throw Error('Positive library scope');
const cases=[];
function fault(id,relative,mutate,expected){const file=path.join(sandbox,relative),before=fs.readFileSync(file);let error=null;try{fs.writeFileSync(file,mutate(before));try{buildControllerFeedbackArtifacts({repo:sandbox});}catch(e){error=e.message;}}finally{fs.writeFileSync(file,before);}cases.push({id,expected,error,pass:error===expected});}
function jsonFault(id,relative,mutate,expected){fault(id,relative,b=>{const doc=JSON.parse(b);mutate(doc);return JSON.stringify(doc,null,2)+'\n';},expected);}
const policyRel='capabilities/hc-controller-feedback-policy.json',receiptRel='capabilities/CONTROLLER-GAME-MOTOR-RECEIPT.json';
jsonFault('unreviewed-profile',policyRel,p=>p.gameMotorReceiptProfiles[0].profileIdentity='xbox-elite-v2','UNREVIEWED_GAME_FEEDBACK_PROFILE:profileIdentity');
jsonFault('unreviewed-token',policyRel,p=>p.gameMotorReceiptProfiles[0].token=2,'UNREVIEWED_GAME_FEEDBACK_PROFILE:token');
jsonFault('extra-profile',policyRel,p=>p.gameMotorReceiptProfiles.push({...p.gameMotorReceiptProfiles[0],token:2}),'GAME_FEEDBACK_PROFILE_SCOPE');
jsonFault('false-forwarding-proof',policyRel,p=>p.gameMotorReceiptProfiles[0].gameFeedbackForwardingProven=true,'UNREVIEWED_GAME_FEEDBACK_PROFILE:gameFeedbackForwardingProven');
jsonFault('false-physical-proof',policyRel,p=>p.gameMotorReceiptProfiles[0].physicalAcceptance=true,'UNREVIEWED_GAME_FEEDBACK_PROFILE:physicalAcceptance');
jsonFault('wire-offset-drift',receiptRel,p=>p.wire.leftBodyOffset=2,'GAME_FEEDBACK_WIRE_PROFILE_REVIEW');
jsonFault('unreviewed-profile-resource',receiptRel,p=>p.lockedProfile.sha256='0'.repeat(64),'GAME_FEEDBACK_WIRE_PROFILE_REVIEW');
jsonFault('false-contract-forwarding-proof',receiptRel,p=>p.gameFeedbackForwardingProven=true,'GAME_FEEDBACK_LOCKED_SDK_OR_SCOPE');
fault('locked-sdk-byte-drift','deps/hidmaestro/HIDMaestro.Core.dll',b=>Buffer.concat([b,Buffer.from([0])]),'GAME_FEEDBACK_LOCKED_SDK_OR_SCOPE');
fault('frozen-target-drift','deps/handheldcompanion-runtime/source/Targets/DualSenseTarget.cs',b=>Buffer.concat([b,Buffer.from('\n// private source fault\n')]),'GAME_FEEDBACK_TARGET_SOURCE:Targets/DualSenseTarget.cs');
fault('frozen-index-drift',inventory.sourceIndex,b=>Buffer.concat([b,Buffer.from('\n')]),'GAME_FEEDBACK_FROZEN_INDEX');
const steamRel='capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json';
jsonFault('steam-unreviewed-profile',policyRel,p=>p.gameMotorReceiptProfiles[1].profileIdentity='xbox-elite-v2','UNREVIEWED_GAME_FEEDBACK_PROFILE:profileIdentity');
jsonFault('steam-unreviewed-token',policyRel,p=>p.gameMotorReceiptProfiles[1].token=3,'UNREVIEWED_GAME_FEEDBACK_PROFILE:token');
jsonFault('steam-wrong-source-api',steamRel,p=>p.wire.reportSource=0,'STEAM_RUMBLE_WIRE_OR_SCOPE');
jsonFault('steam-wrong-motor-word-offset',steamRel,p=>p.wire.leftBodyOffset=5,'STEAM_RUMBLE_WIRE_OR_SCOPE');
jsonFault('steam-false-physical-proof',steamRel,p=>p.physicalAcceptance=true,'STEAM_RUMBLE_WIRE_OR_SCOPE');
jsonFault('steam-wrong-resource-hash',steamRel,p=>p.lockedProfile.sha256='0'.repeat(64),'STEAM_RUMBLE_LOCKED_SOURCE');
jsonFault('steam-uncertified-source-hidden',policyRel,p=>p.additionalGameMotorSoftwareConsumption[0].sourceConnected=false,'STEAM_RUMBLE_SOURCE_CONSUMPTION_SCOPE');
fault('steam-frozen-target-drift','deps/handheldcompanion-runtime/source/Targets/SteamDeckTarget.cs',b=>Buffer.concat([b,Buffer.from('\n// private source fault\n')]),'GAME_FEEDBACK_TARGET_SOURCE:Targets/SteamDeckTarget.cs');
const xboxRel='capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json';
jsonFault('xbox-unreviewed-profile',policyRel,p=>p.gameMotorReceiptProfiles[2].profileIdentity='xbox-elite-v2-bt','UNREVIEWED_GAME_FEEDBACK_PROFILE:profileIdentity');
jsonFault('xbox-unreviewed-token',policyRel,p=>p.gameMotorReceiptProfiles[2].token=4,'UNREVIEWED_GAME_FEEDBACK_PROFILE:token');
jsonFault('xbox-wrong-source-api',xboxRel,p=>p.wire.reportSource=0,'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-led-mask-mistaken-for-vibration',xboxRel,p=>p.wire.flagged5.vibrationMask=1,'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-virtual-index-cannot-select-physical-slot',xboxRel,p=>p.wire.flagged5.physicalSourceChosenByVirtualIndex=true,'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-false-physical-proof',xboxRel,p=>p.physicalAcceptance=true,'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-wrong-resource-hash',xboxRel,p=>p.lockedProfile.sha256='0'.repeat(64),'XBOX_XINPUT_RUMBLE_LOCKED_SOURCE');
jsonFault('xbox-uncertified-source-hidden',policyRel,p=>p.additionalGameMotorSoftwareConsumption[1].sourceConnected=false,'STEAM_RUMBLE_SOURCE_CONSUMPTION_SCOPE');
fault('xbox-frozen-target-drift','deps/handheldcompanion-runtime/source/Targets/Xbox360Target.cs',b=>Buffer.concat([b,Buffer.from('\n// private source fault\n')]),'GAME_FEEDBACK_TARGET_SOURCE:Targets/Xbox360Target.cs');
jsonFault('steam-contract-id-cannot-drift',steamRel,p=>p.capabilityId='controller.feedback.game.wrong','STEAM_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-contract-id-cannot-drift',xboxRel,p=>p.capabilityId='controller.feedback.game.wrong','XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-physical-provider-list-cannot-expand',xboxRel,p=>p.supportedPhysicalSources.push('controller.feedback.unknown'),'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-probe-not-actually-executed',xboxRel,p=>p.primaryProtocolEvidence.probesExecuted=true,'XBOX_XINPUT_RUMBLE_WIRE_OR_SCOPE');
jsonFault('xbox-frozen-target-hash-cannot-drift',xboxRel,p=>p.frozenTarget.sha256='0'.repeat(64),'XBOX_XINPUT_RUMBLE_LOCKED_SOURCE');
jsonFault('xbox-protocol-reference-commit-cannot-drift',xboxRel,p=>p.primaryProtocolEvidence.openXinputCommit='0'.repeat(40),'XBOX_XINPUT_RUMBLE_LOCKED_SOURCE');
// Current provenance rejects historical identities while retaining every protocol/scope negative above.
const observationRel='capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json';
for(const [name,relative,expected] of [['sony',receiptRel,'GAME_FEEDBACK_LOCKED_SDK_OR_SCOPE'],['steam',steamRel,'STEAM_RUMBLE_LOCKED_SOURCE'],['xbox',xboxRel,'XBOX_XINPUT_RUMBLE_LOCKED_SOURCE'],['observation',observationRel,'GAME_OUTPUT_OBSERVATION_LOCKED_SDK']]){
 jsonFault(name+'-historical-112-sdk-rejected',relative,p=>Object.assign(p.lockedSdk,{sha256:'f0b048e1b566804226efdf854f6201eb9d2895c1e83f3c1a8813562aab2d186c',mvid:'5f1d5fa1-a18d-44e5-aba0-a8dfb3fabdf8',commit:'b4d29d9f4b1b91ced6b4755f095184b66339b8be'}),expected);
 for(const [field,value] of [['file','deps/hidmaestro/other.dll'],['sha256','0'.repeat(64)],['mvid','00000000-0000-0000-0000-000000000000'],['commit','0'.repeat(40)]])jsonFault(name+'-sdk-'+field+'-cannot-drift',relative,p=>p.lockedSdk[field]=value,expected);
}
jsonFault('sony-historical-112-profile-rejected',receiptRel,p=>p.lockedProfile.sha256='aebe41f4bc11dfe64862d9ad564d768e1a20a07e7374ffdeb4992deb1a588c0a','GAME_FEEDBACK_WIRE_PROFILE_REVIEW');
jsonFault('steam-historical-112-profile-rejected',steamRel,p=>p.lockedProfile.sha256='d3cf9b032cb44883afe8d00f18abf5d43a16a774fafe5fbbf0b58a6c58f47b6b','STEAM_RUMBLE_LOCKED_SOURCE');
jsonFault('steam-sdk-protocol-provenance-cannot-drift',steamRel,p=>p.primaryProtocolEvidence.sdkSourceCommit='b4d29d9f4b1b91ced6b4755f095184b66339b8be','STEAM_RUMBLE_LOCKED_SOURCE');
jsonFault('xbox-sdk-protocol-provenance-cannot-drift',xboxRel,p=>p.primaryProtocolEvidence.sdkCommit='b4d29d9f4b1b91ced6b4755f095184b66339b8be','XBOX_XINPUT_RUMBLE_LOCKED_SOURCE');
const stable=inputs.every(i=>sha(i.file)===i.sha256),result={status:stable&&cases.every(c=>c.pass)?'PASS':'FAIL',stable,inputs,cases,registeredPhysicalProviders:2,registeredGameReceiptProfiles:3,hostStarted:false,physicalAcceptance:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,stable,cases:cases.length,failed:cases.filter(c=>!c.pass)}));if(result.status!=='PASS')process.exitCode=1;
