export const STALE_REVISION_FAULT=Object.freeze({
 id:'host-motor-revision-stops-at-one',
 source:'InputHost/HostGameOutputObservation.cs',
 before:'                ++_motorRevision;',
 after:'                if (_motorRevision == 0) ++_motorRevision; // private semantic source fault: replay revision'
});
export const NATIVE_REVISION_FAULT=Object.freeze({
 id:'native-inbox-revision-guard-removed',source:'native/game_feedback_receipt.h',
 before:'sample.revision<=lastRevision_',after:'false /* private native source fault: accept stale revision */',
 expectedAssertion:'actual-Inbox-rejects-replayed-revision-over-pipe'
});
function once(text,before,after){if(text.split(before).length!==2)throw Error('PRIVATE_SOURCE_FAULT_ANCHOR_DRIFT:'+before);return text.replace(before,after);}
export function staleRevision(text){return once(text,STALE_REVISION_FAULT.before,STALE_REVISION_FAULT.after);}
export function noRevisionGuard(text){return once(text,NATIVE_REVISION_FAULT.before,NATIVE_REVISION_FAULT.after);}
