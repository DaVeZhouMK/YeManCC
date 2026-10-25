import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {extract,sha} from '../owned_return_settlement_r120/source-binding.mjs';
export {sha};
export const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};};
export function bindCurrent(s){
 const t=s.text,ranges=[],owner=extract(t,'struct','MsiDinputState',s.file,ranges),ownerStart=t.indexOf(owner);
 function record(symbol,a,b){assert(a>=ownerStart&&b<=ownerStart+owner.length&&b>a,symbol+' outside owner');const code=t.slice(a,b);ranges.push({file:s.file,kind:'exact-state-declarations',symbol,startLine:t.slice(0,a).split('\n').length,endLine:t.slice(0,b).split('\n').length,startUtf8Byte:Buffer.byteLength(t.slice(0,a)),endUtf8ByteExclusive:Buffer.byteLength(t.slice(0,b)),sha256:sha(code)});return code;}
 const bindMatches=[...owner.matchAll(/^    unsigned bindCount = 0;[^\r\n]*/gm)];assert.equal(bindMatches.length,1);
 const ba=ownerStart+bindMatches[0].index,bindField=record('bindCount',ba,ba+bindMatches[0][0].length);
 const first=/^    std::atomic<[^\r\n]+\bcanonical[A-Za-z]+[^\r\n]*$/m.exec(owner);assert(first);
 const a=ownerStart+first.index,end='    std::atomic<bool> canonicalValid{false};',b=t.indexOf(end,a)+end.length;
 const fields=record('canonical field block including producer proof and original ifdef',a,b);
 const names=v=>[...new Set([...v.matchAll(/\bcanonical[A-Za-z0-9_]+\b/g)].map(m=>m[0]))].sort();assert.deepEqual(names(owner),names(fields));
 const definitions=['MsiCanonicalReadProof','InputCaptureGamepadSnapshot'].map(name=>extract(t,'struct',name,s.file,ranges));
 const functions=['currentPowerGeneration','msiDinputPublishCanonicalFrame','msiDinputCanonicalSnapshot','msiDinputApplyCanonicalReadProof'].map(name=>extract(t,'function',name,s.file,ranges));
 return {ranges,owner,fields:bindField+'\n'+fields,commonTypes:definitions.join('\n\n'),functions:functions.join('\n\n')};
}
export function freeze(repo,root){
 const workspace=path.resolve(repo,'../../..');
 const baselineRoot=path.join(workspace,'Build/Validation/VirtualInput-MsiCanonical-R123-20261017/canonical-sidecar');
 const receipt=load(path.join(baselineRoot,'source-binding.json')),baseline=JSON.parse(receipt.text);
 for(const f of baseline.frozenInputs)assert.equal(sha(fs.readFileSync(path.join(baselineRoot,'baseline',f.name))),f.sha256,'R123 input changed: '+f.name);
 const current=load(path.join(repo,'native/main.cpp')),bound=bindCurrent(current);
 fs.mkdirSync(root,{recursive:true});const frozen=path.join(root,'frozen');fs.mkdirSync(frozen);
 for(const [to,from]of [['baseline-fields.inc','actual-fields.inc'],['baseline-functions.inc','actual-functions.inc'],['baseline-MsiDinputState.txt','MsiDinputState.actual.txt']])fs.copyFileSync(path.join(baselineRoot,'baseline',from),path.join(frozen,to));
 fs.writeFileSync(path.join(frozen,'baseline-source-binding.json'),receipt.bytes);
 for(const [name,text]of Object.entries({'current-fields.inc':bound.fields,'current-functions.inc':bound.functions,'actual-common-types.inc':bound.commonTypes,'current-MsiDinputState.txt':bound.owner}))fs.writeFileSync(path.join(frozen,name),text);
 const jsonHeader=load(path.join(repo,'deps/json/json.hpp'));fs.writeFileSync(path.join(frozen,'json.hpp'),jsonHeader.bytes);
 const binding={frozenAt:new Date().toISOString(),baselineSourceSHA:baseline.baselineSourceSHA,currentSourceSHA:current.sha256,currentSourceFile:current.file,baselineReceipt:{file:receipt.file,sha256:receipt.sha256},baselineRanges:baseline.ranges,currentRanges:bound.ranges,
  dependency:{file:jsonHeader.file,sha256:jsonHeader.sha256},sourceSubstitutions:[],diagnostic:false,fullMainOrOwnerLayoutCompiled:false,
  frozenFiles:fs.readdirSync(frozen).map(name=>({name,sha256:sha(fs.readFileSync(path.join(frozen,name)))})),
  scope:'Byte-exact functions and required declarations inside isolated namespaces. Common recipient layout is current InputCaptureGamepadSnapshot in both variants, solely harness plumbing; no old proof application is invented. Current matching generation atomics are stand-ins.',
  comparability:'Same input table, business output, digest, compiler options and opaque single operation wrapper call. Three paired workloads compare publish; old snapshot vs requested producer-proof snapshot; old publish+snapshot vs current publish+snapshot+actual proof application. Apply current-only has no invented baseline implementation. No LTO across wrapper and measurement TUs.',
  exclusions:['full producer pump/permit/locks/failure paths','contention and cross-thread atomic behavior','full state layout/cache footprint','production build callsite inlining/LTO','sleep/Host/device/Steam/root cause','whole-machine CPU or benefit','CtrlAlt']};
 fs.writeFileSync(path.join(root,'source-binding.json'),JSON.stringify(binding,null,2)+'\n');return {binding,current,bound,frozen};
}