import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [beforeDir,afterDir,output]=process.argv.slice(2);
if(!beforeDir||!afterDir||!output)throw Error('Usage: before-native after-native report.json');
const read=(dir,name)=>fs.readFileSync(path.join(dir,name),'utf8').replace(/\r\n/g,'\n');
const before=Object.fromEntries(['main.cpp','screen_touchpads.h','screen_touchpad_cursor.h'].map(n=>[n,read(beforeDir,n)]));
const after=Object.fromEntries(Object.keys(before).map(n=>[n,read(afterDir,n)]));
const checks=[];const check=(name,ok)=>checks.push({name,ok:!!ok});
const single=(s,needle)=>s.split(needle).length===2;
const replace=(s,a,b)=>{if(!single(s,a))throw Error('Unexpected source anchor: '+a.slice(0,70));return s.replace(a,b);};
const added=
'// Order the existing Deck display request after the report attempt and\n'+
'    // outside the transport mutex. A Host receipt is not Steam mouse consumption.\n'+
'    ymcc::screenpads::cursorRequestAfterReport();\n    ';
let restored=replace(after['main.cpp'],added,'');
check('main-only-adds-one-display-request-after-original-lock',restored===before['main.cpp']);
check('one-production-snapshot-and-after-call',single(after['main.cpp'],'ymcc::screenpads::snapshot(&touchTransport)')&&single(after['main.cpp'],'ymcc::screenpads::cursorRequestAfterReport();'));
const main=after['main.cpp'];const snapshot=main.indexOf('ymcc::screenpads::snapshot(&touchTransport)');
const submit=main.indexOf('accepted = frameTextReady',snapshot),tail=main.indexOf('        hostActive = g_inputHostActive;\n        lifecycle = g_inputHostLifecycle;\n    }',submit);
const afterCall=main.indexOf('ymcc::screenpads::cursorRequestAfterReport();',tail);
check('after-call-is-after-request-result-and-unconditional-lock-exit',snapshot<submit&&submit<tail&&tail<afterCall&&main.slice(tail,afterCall).includes(added.split('ymcc::')[0]));
let pad=after['screen_touchpads.h'];pad=replace(pad,'    cursorRequestBeforeReport(); // Deck uses the same BUS request after its report attempt.','    cursorRequestSync(); // UI-only visuals, using the existing BUS cadence.');
pad=replace(pad,'j["cursorPolicyVersion"]=12;j["cursorSubmitOrderVersion"]=1;','j["cursorPolicyVersion"]=11;');
pad=replace(pad,'j["cursorDeckOnlyTracking"]=cursorDeckOnlyTracking.load(std::memory_order_acquire);','j["cursorDeckOnlyTracking"]=cursorDeckOnlyTracking;');
check('touchpad-Core-mapping-and-R7-byte-preserved',pad===before['screen_touchpads.h']);
const wrapper='// The BUS assembles the report before it submits it. Deck mouse output is\n// asynchronous, so do not spend its display slot before that report attempt.\n// Mouse/mixed contacts keep the original pre-report request. No extra poll.\ninline void cursorRequestBeforeReport() {\n    if(!cursorDeckOnlyTracking.load(std::memory_order_acquire))cursorRequestSync();\n}\ninline void cursorRequestAfterReport() {\n    if(cursorDeckOnlyTracking.load(std::memory_order_acquire))cursorRequestSync();\n}\n';
let cursor=after['screen_touchpad_cursor.h'];cursor=replace(cursor,wrapper,'');
cursor=replace(cursor,'inline std::atomic<bool> cursorDeckOnlyTracking{false}; // UI writes; BUS reads only to order the existing display request.','inline bool cursorDeckOnlyTracking=false; // UI-owned: no active direct-Mouse contact.');
cursor=replace(cursor,'cursorvisibility::stop();cursorDeckOnlyTracking.store(false,std::memory_order_release);cursorDeckNextPositionTick=0;','cursorvisibility::stop();cursorDeckOnlyTracking=false;cursorDeckNextPositionTick=0;');
cursor=replace(cursor,'    if(cursorDeckOnlyTracking.load(std::memory_order_relaxed)!=deckOnly) {\n        cursorResetHandoff();cursorDeckOnlyTracking.store(deckOnly,std::memory_order_release);\n    }','    if(cursorDeckOnlyTracking!=deckOnly)cursorResetHandoff();\n    cursorDeckOnlyTracking=deckOnly;');
check('cursor-drawing-queries-deadlines-and-edge-policy-byte-preserved',cursor===before['screen_touchpad_cursor.h']);
check('mode-published-only-on-change-with-no-seqcst-MOVE-store',after['screen_touchpad_cursor.h'].includes('if(cursorDeckOnlyTracking.load(std::memory_order_relaxed)!=deckOnly) {\n        cursorResetHandoff();cursorDeckOnlyTracking.store(deckOnly,std::memory_order_release);'));
check('same-24ms-budget-and-original-coalescing',after['screen_touchpad_cursor.h'].includes('kCursorActiveFallbackMs=24')&&after['screen_touchpad_cursor.h'].includes('cursorSyncPending.exchange(true,std::memory_order_acq_rel)'));
check('before-after-wrappers-do-not-query-or-change-deadlines',!wrapper.includes('GetCursorInfo')&&!wrapper.includes('.store(')&&!wrapper.includes('PostMessage'));
const result={ok:checks.every(c=>c.ok),checks,scope:'Exact production source delta; this is not compiling/executing the complete BUS, Host or Steam',sources:Object.entries(after).map(([name,s])=>({name,normalizedSha256:crypto.createHash('sha256').update(s).digest('hex')}))};
fs.mkdirSync(path.dirname(path.resolve(output)),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2));
console.log(JSON.stringify({ok:result.ok,passed:checks.filter(c=>c.ok).length,checks:checks.length,output}));process.exitCode=result.ok?0:1;
