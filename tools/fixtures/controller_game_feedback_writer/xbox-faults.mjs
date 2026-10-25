import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {STEAM_NATIVE_FAULTS} from './steam-faults.mjs';
export const XBOX_MANAGED_FAULTS=Object.freeze([
 {
  "id": "led-only-promoted-to-motor",
  "before": "if ((flags & 0x02) == 0) return Result.NoMotorUpdate;",
  "after": "// controlled source fault: LED-only promoted",
  "expected": "xbox-led-only-does-not-become-motor-or-zero"
 },
 {
  "id": "led-mask-used-as-vibration",
  "before": "if ((flags & 0x02) == 0) return Result.NoMotorUpdate;",
  "after": "if ((flags & 0x01) == 0) return Result.NoMotorUpdate;",
  "expected": "xbox-flagged5-direct-independent-amplitudes"
 },
 {
  "id": "raw-word-low-byte-used-as-amplitude",
  "before": "motors = new(data[1], data[3], 4);",
  "after": "motors = new(data[0], data[2], 4);",
  "expected": "xbox-raw4-little-endian-word-pair"
 },
 {
  "id": "steam-compatibility-borrowed-for-xbox",
  "before": "motors = new(data[2], data[3], 4);",
  "after": "motors = new(data[2], data[3], 3);",
  "expected": "xbox-flagged5-direct-independent-amplitudes"
 },
 {
  "id": "wrong-xinput-output-source",
  "before": "if (source != 2 || reportId != 0) return Result.NotApplicable;",
  "after": "if (source != 0 || reportId != 0) return Result.NotApplicable;",
  "expected": "xbox-wrong-source-report-and-size-refused"
 },
 {
  "id": "unknown-flag-bits-admitted",
  "before": "if ((flags & ~0x03) != 0) return Result.Malformed;",
  "after": "// controlled source fault: unknown flags admitted",
  "expected": "xbox-all-other-flag-bits-rejected"
 },
 {
  "id": "raw-drain-erases-pending-xbox-stop",
  "before": "Volatile.Write(ref _pending, _pending & ~delivered);",
  "after": "Volatile.Write(ref _pending, _pending & ~(delivered | MotorPending));",
  "expected": "xbox-real-mailbox-led-and-malformed-cannot-erase-pending-stop"
 },
 {
  "id": "old-callback-admitted",
  "before": "if (!ReferenceEquals(_current, binding) || !ReferenceEquals(binding.Controller, sender)) return;",
  "after": "if (!ReferenceEquals(binding.Controller, sender)) return;",
  "expected": "xbox-real-mailbox-old-callback-and-wrong-sender-refused"
 }
]);
export const XBOX_NATIVE_FAULTS=Object.freeze([
 {
  "id": "restore-only-prior-target-binding",
  "kind": "source",
  "before": "profile&&profile->token>=1&&profile->token<=3&&protocol==profile->token",
  "after": "profile&&profile->token>=1&&profile->token<=2&&protocol==profile->token",
  "expected": "XBOX_PROFILE3_BOUND"
 },
 {
  "id": "host-protocol-catalog-mismatch-admitted",
  "kind": "source",
  "before": "profile&&profile->token>=1&&profile->token<=3&&protocol==profile->token",
  "after": "profile&&profile->token>=1&&profile->token<=3",
  "expected": "XBOX_HOST_PROTOCOL_MUST_MATCH_EXACT_TARGET"
 },
 {
  "id": "steam-compatibility-admitted-under-xbox",
  "kind": "receipt",
  "before": "((protocol_==1&&sample.compatibility!=1&&sample.compatibility!=2)||(protocol_==2&&sample.compatibility!=3)||(protocol_==3&&sample.compatibility!=4))",
  "after": "((protocol_==1&&sample.compatibility!=1&&sample.compatibility!=2)||(protocol_==2&&sample.compatibility!=3))",
  "expected": "STEAM_COMPAT_CANNOT_ENTER_XBOX_INBOX"
 },
 {
  "id": "xbox-compatibility-rejected-before-consumer",
  "kind": "receipt",
  "before": "(words[4]!=1&&words[4]!=2&&words[4]!=3&&words[4]!=4)",
  "after": "(words[4]!=1&&words[4]!=2&&words[4]!=3)",
  "expected": "XBOX_ACTUAL_XINPUT_WRITER_ACCEPTED"
 }
]);

export function steamNativeFaultsForCurrent(main,receipt){return STEAM_NATIVE_FAULTS.map(f=>{
 const text=f.kind==='source'?main:receipt;if(text.split(f.before).length===2)return f;
 let before=f.before,after=f.after;
 if(f.kind==='source'){before=before.replace('token<=2','token<=3');after=after.replace('token<=2','token<=3');}
 else if(f.id==='cross-profile-compatibility-admitted')before=before.replace('||(protocol_==2&&sample.compatibility!=3))','||(protocol_==2&&sample.compatibility!=3)||(protocol_==3&&sample.compatibility!=4))');
 else if(f.id==='steam-compatibility-rejected-before-consumer'){before=before.replace('words[4]!=3)','words[4]!=3&&words[4]!=4)');after=after.replace('words[4]!=2)','words[4]!=2&&words[4]!=4)');}
 if(text.split(before).length!==2)throw Error('CURRENT_STEAM_SOURCE_FAULT_ANCHOR_DRIFT:'+f.id);
 return {...f,before,after,currentThreeTargetSourceAnchor:true};
 });}
export function inspectXboxIntegration(repo){
 const specs=[
 ['native/main.cpp','profile&&profile->token>=1&&profile->token<=3&&protocol==profile->token'],
 ['InputHost/HostGameOutputObservation.cs','motors = new(data[1], data[3], 4);'],
 ['InputHost/HostGameOutputObservation.cs','if ((flags & 0x02) == 0) return Result.NoMotorUpdate;'],
 ['InputHost/HostGameOutputObservation.cs','if ((flags & ~0x03) != 0) return Result.Malformed;'],
 ['native/game_feedback_receipt.h','(protocol_==3&&sample.compatibility!=4)'],
 ['capabilities/HcControllerFeedbackCapabilityCatalog.generated.h','xbox-elite-v2-xinput-classic-v1'],
 ['capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','"xbox-elite-v2" => (ushort)3']
 ];
 const checks=specs.map(([relative,anchor])=>{const p=path.join(repo,relative),exists=fs.existsSync(p),bytes=exists?fs.readFileSync(p):null;return{relative,exists,sha256:bytes?crypto.createHash('sha256').update(bytes).digest('hex'):null,anchor,matched:bytes?bytes.toString('utf8').includes(anchor):false};});
 const relative='capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json',p=path.join(repo,relative),exists=fs.existsSync(p);let v=null;if(exists)try{v=JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));}catch{}
 checks.push({relative,exists,sha256:exists?crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'):null,matched:v?.protocolToken===3&&v?.compatibilityToken===4&&v?.physicalAcceptance===false});
 const ready=checks.every(c=>c.matched);return{status:ready?'CURRENT_G_D12_SOURCE_PRESENT':'WAITING_FOR_CURRENT_G_D12_SOURCE_INTEGRATION',ready,checks,productCandidateInjected:false,wholeFileIdentityExemptions:false};
}
