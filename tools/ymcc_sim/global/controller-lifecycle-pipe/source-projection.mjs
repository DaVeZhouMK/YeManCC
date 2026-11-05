// The original wire producer and future identity review share this generator.
// Pure source generation only: no compiler, fixture execution or cache authority.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from '../../../fixtures/hc_remaining_model_capabilities/expectations.mjs';
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
export const WIRE_FAULTS=Object.freeze([
 Object.freeze({id:'sequence-sync-omitted',before:'if (advanceSequence && hostConsumed) g_inputHostSequence = sequenceValue;',after:'(void)hostConsumed;',expected:'business-refusal-sequence-synced-without-transport-fault'}),
 Object.freeze({id:'instance-fence-omitted',before:'if (!tupleMatches || !identityOk) {',after:'(void)identityOk; if (!tupleMatches) {',expected:'stale-Host-instance-cannot-accept-ACK'})
]);
export function mutateWireSource(main,id){
 assert.equal(typeof main,'string','Wire source text required');
 const fault=WIRE_FAULTS.find(item=>item.id===id);assert(fault,'Unknown original wire mutation: '+id);
 assert.equal(main.split(fault.before).length,2,'SOURCE_FAULT_ANCHOR_CHANGED:'+fault.id);
 return main.replace(fault.before,fault.after);
}
export function generateWireInput(main,template){
 assert.equal(typeof main,'string','Wire source text required');
 assert.equal(typeof template,'string','Wire template text required');
const bindings=[];
const fn=name=>{const b=extract(main,name);bindings.push(b);return b.code;};
const struct=name=>{const b=extract(main,name,'struct');bindings.push(b);return b.code;};
function section(marker,label){const start=main.indexOf(marker);assert(start>=0&&main.indexOf(marker,start+marker.length)<0,'source anchor '+label);const brace=main.indexOf('{',start),end=closeBrace(main,brace);const code=main.slice(start,end+1);bindings.push({name:label,sha256:sha(code),code});return code;}
const constants=['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity','kInputHostLockedCoreSha256','kInputHostLockedCoreMvid'].map(name=>{const matches=[...main.matchAll(new RegExp('^static constexpr const char\\* '+name+'\\s*=[^;]+;','gm'))];assert(matches.length===1,name);bindings.push({name,code:matches[0][0],sha256:sha(matches[0][0])});return matches[0][0];}).join('\n');
const unicodeDefinitions=[...main.matchAll(/^static std::string W2U\(const wchar_t\*/gm)];assert.equal(unicodeDefinitions.length,1,'W2U pointer overload missing/nonunique');
const unicodeStart=main.indexOf('static std::string W2U(const wchar_t*'),unicodeEnd=main.indexOf('\n}',unicodeStart)+2;assert(unicodeStart>0&&unicodeEnd>unicodeStart);const wideRows=[...main.matchAll(/^static std::string W2U\(const std::wstring& w\)[^\r\n]+/gm)];assert.equal(wideRows.length,1,'W2U wide overload missing/nonunique');const wide=wideRows[0];
const unicode=main.slice(unicodeStart,unicodeEnd)+'\n'+wide[0]+'\n'+fn('U2W');bindings.push({name:'W2U overloads',code:unicode,sha256:sha(unicode)});
const compactBindingDeclarations=[...main.matchAll(/^static std::string g_inputHostFrameReplyBinding;\r?$/gm)];
if(compactBindingDeclarations.length!==1)throw Error('Actual compact reply binding declaration missing/nonunique');
bindings.push({name:'g_inputHostFrameReplyBinding',code:compactBindingDeclarations[0][0],sha256:sha(compactBindingDeclarations[0][0])});
const compactReplySupport='#include "input_host_compact_frame_reply.h"\n'+compactBindingDeclarations[0][0]+'\n';
// Wire-only scope has no deferred visibility or OneX Start writer. Bind the
// actual null TLS declarations; their opaque owners and Start deadline are not
// fabricated as successful live-start consumers.
const startWriteDeclarations=[
 /^static thread_local OneXVirtualStartEpisode\* t_inputHostOneXStartWrite=nullptr;\r?$/gm,
 /^static thread_local const InputHostDeferredPhysicalHide\* t_inputHostDeferredPhysicalHideWrite=nullptr;\r?$/gm
].map(pattern=>{const rows=[...main.matchAll(pattern)];assert(rows.length===1,'Current unarmed Start TLS declaration missing/nonunique');const code=rows[0][0];bindings.push({name:'unarmed-Start-TLS',code,sha256:sha(code),scope:'actual declaration; opaque null owner'});return code;});
const startDeadlineRows=[...main.matchAll(/^static uint64_t inputHostOneXStartDeadline\(\);\r?$/gm)];
assert(startDeadlineRows.length===1,'Current OneX Start deadline signature missing/nonunique');
const startDeadlineDeclaration=startDeadlineRows[0][0];
bindings.push({name:'inputHostOneXStartDeadline-signature',code:startDeadlineDeclaration,sha256:sha(startDeadlineDeclaration),scope:'actual signature; unreachable wire-only trap body'});
const unarmedStartWrites='struct OneXVirtualStartEpisode;\nstruct InputHostDeferredPhysicalHide;\n'+startWriteDeclarations.join('\n')+'\n'+startDeadlineDeclaration.trimEnd().replace(/;$/,'{throw std::logic_error("unexpected-OneX-Start-write-in-wire-fixture");}')+'\n';
const replacements={REQUEST_LIFECYCLE:['inputHostRequestIsLifecycleCommand','inputHostRequestHasExecutionDeadline','inputHostProcessAliveLocked'].map(fn).join('\n'),GAME_MOTOR:[compactReplySupport,struct('InputHostGameMotorDomKeys'),fn('inputHostExpandCompactFrameReplyLocked'),fn('inputHostGameMotorFromDom'),fn('inputHostConsumeGameMotorReplyLocked'),fn('inputHostObserveGameMotorBindingLocked')].join('\n'),FRAME_SCOPE:unarmedStartWrites+struct('InputFrameTransportScope'),CONSTANTS:constants,UNICODE:unicode,NEW_ID:fn('inputHostNewId'),HASH:['inputHostCryptoProvider','inputHostSha256','inputHostCanonicalFrameHash'].map(fn).join('\n'),ENVELOPE:fn('inputHostEnvelopeLocked'),PIPE:['inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostCreateServerPipeLocked','inputHostPipeIo','inputHostWaitClientLocked'].map(fn).join('\n'),FAST_RECEIPT:struct('InputHostFastReceipt'),SCANNER:section('namespace c7aReceiptFast {','c7aReceiptFast'),AFTER_FRAME:fn('inputHostAfterSubmitFrameAccepted'),RELEASE_TIMING_LOG:fn('inputHostReleaseTimingReceiptLog'),COMMAND_RESULT_LOG:fn('inputHostCommandResultLog'),REQUEST:fn('inputHostRequestPreparedLocked')};
let cpp=template;
 for(const[name,code]of Object.entries(replacements)){
  assert.equal(cpp.split('@@'+name+'@@').length,2,name);
  cpp=cpp.replace('@@'+name+'@@',()=>code);
 }
 assert(!/@@\w+@@/.test(cpp),'Unresolved wire template marker');
 return {cpp,bindings,replacements};
}
export function wireSourceProjection(main,template){
 const descriptions=[{id:'positive',expectedRejection:null,source:main},...WIRE_FAULTS.map(fault=>({id:fault.id,expectedRejection:fault.expected,source:mutateWireSource(main,fault.id)}))];
 const variants=descriptions.map(({id,expectedRejection,source})=>{
  const {cpp}=generateWireInput(source,template);
  return {id,expectedRejection,sha256:sha(cpp),bytes:Buffer.byteLength(cpp)};
 });
 assert.equal(new Set(variants.map(item=>item.sha256)).size,3,'Original wire mutations must change generated input');
 return {schemaVersion:1,kind:'wire-generated-source-projection-v1',variants};
}
