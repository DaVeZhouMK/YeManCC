import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {reverseReviewedDelta} from '../sleep-native-recovery-consumer/script-review.mjs';
import {verifyOriginalEntrySourceSuccessor} from './current-source-successor.mjs';
import {verifySharedLaunchLeaves} from './shared-launch-review.mjs';

const digest=b=>createHash('sha256').update(b).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const need=(ok,code)=>{if(!ok)throw Error(code);};
export function verifyPublicWakeSource(repo,spec){
 const binding=spec.publicWakeSourceReview;
 if(!binding)return {source:verifyOriginalEntrySourceSuccessor(repo,spec),launch:()=>verifySharedLaunchLeaves(repo,spec),receipt:null};
 const expected='ed93828542acf6e2bd215ecfe27b67f139148898cbbf23981042798cfabdaca9', relative='tools/ymcc_sim/global/sleep-non-user-wake-consumer/public-wake-source-review.json';
 need(binding.manifest===relative&&binding.sha256===expected,'NONUSER_PUBLIC_WAKE_BINDING');
 const bytes=fs.readFileSync(path.join(repo,relative));need(digest(bytes)===expected,'NONUSER_PUBLIC_WAKE_MANIFEST');
 const {successor,...book}=JSON.parse(bytes), previous=book.priorSpec;
 const priorHash='525aa62354d547221cb3f45cac44beee555c6603511d27b8cab5429226b05c31';
 need(successor?.schemaVersion===1&&successor.previousManifestSha256===priorHash&&successor.sourceApproval===false&&successor.defaultParentApprovalRenewed===false&&digest(JSON.stringify(book,null,2)+'\n')===priorHash,'NONUSER_PUBLIC_WAKE_CURRENT_PREDECESSOR');
 const {continuation,...originalBook}=book;
 const originalHash='a8138859b07a1db0b94125d5394b7f8240b18c8a4869c9cf017202dc635d3f72';
 need(continuation?.schemaVersion===1&&continuation.previousManifestSha256===originalHash&&digest(JSON.stringify(originalBook,null,2)+'\n')===originalHash,'NONUSER_PUBLIC_WAKE_PREDECESSOR');
 need(book.schemaVersion===1&&book.kind==='public-wake-exact-prior-readers'&&book.sourceApproval===false&&book.defaultParentApprovalRenewed===false&&!previous.publicWakeSourceReview,'NONUSER_PUBLIC_WAKE_SCHEMA');
 const declared=new Set([...previous.productSources,...previous.sharedLeaves]), a=new Map(), b=new Map();
 need(JSON.stringify(spec.sharedLeaves)===JSON.stringify([...previous.sharedLeaves,...book.addedLeaves]),'NONUSER_PUBLIC_WAKE_SOURCE_SET');
 const baseProducts=[...previous.productSources,"native/sleep_wake_admission.h"];
 const i=continuation.insertion;
 const priorProducts=[...baseProducts.slice(0,i),...continuation.added.map(row=>row.path),...baseProducts.slice(i)];
 const joinedHeaders=["native/input_host_shared_power.h","native/sleep_power_revocation.h","native/sleep_abnormal_resleep.h"];
 need(Number.isInteger(i)&&i>0&&baseProducts[i-1]==='native/sleep_timeout_native.inc'&&JSON.stringify(successor.added.map(row=>row.path))===JSON.stringify(joinedHeaders)&&JSON.stringify(spec.productSources)===JSON.stringify([...priorProducts,...joinedHeaders]),'NONUSER_CLASSIFICATION_INPUT_SET');
 const currentPredecessor=new Map();
 const nestedPath="tools/ymcc_sim/global/sleep-non-user-wake-consumer/current-source-successor.json";
 const nestedBytes=fs.readFileSync(path.join(repo,nestedPath));
 need(digest(nestedBytes)===previous.currentSourceSuccessorReview.sha256,"NONUSER_CURRENT_SUCCESSOR_MANIFEST_CHANGED");
 const nested=JSON.parse(nestedBytes);
 const hashes=new Map([...nested.currentContracts.map(row=>[row.path,row.sha256]),...[...book.pairs,book.launchPair,...continuation.deltas].map(row=>[row.path,row.afterSha256])]);
 // Exact current inputs are reversed before the historical readers inspect them.
 const inputPredecessor=new Map();
 let inputSuccessorReview=null;
 if(binding.inputSuccessor){
  const manifest='tools/ymcc_sim/global/sleep-non-user-wake-consumer/public-wake-input-successor.json',sha256='a369cd94329fcad118a82460c8fba480d15bde451f1f54a37b552bb86958f27a';
  need(binding.inputSuccessor.manifest===manifest&&binding.inputSuccessor.sha256===sha256,'NONUSER_PUBLIC_WAKE_INPUT_BINDING');
  const inputBytes=fs.readFileSync(path.join(repo,manifest));
  need(digest(inputBytes)===sha256,'NONUSER_PUBLIC_WAKE_INPUT_MANIFEST');
  const input=JSON.parse(inputBytes);
  need(input.schemaVersion===1&&input.kind==='exact-public-wake-input-successor'&&input.previousManifestSha256===expected&&input.sourceApproval===false&&input.defaultParentApprovalRenewed===false,'NONUSER_PUBLIC_WAKE_INPUT_SCHEMA');
  const next=binding.inputContinuation;
  if(next){
   need(digest(JSON.stringify(next))==='ba1e10c0d2b27ce32d3a0fe82654e1bbe87ebc9c58c8fc4123ab3bc0050cecd0','NONUSER_PUBLIC_WAKE_INPUT_CONTINUATION_CHANGED');
   need(next.schemaVersion===1&&next.previousInputSha256===sha256&&next.sourceApproval===false&&next.defaultParentApprovalRenewed===false,'NONUSER_PUBLIC_WAKE_INPUT_CONTINUATION_SCHEMA');
  }
  const allowed=new Map([...hashes,...[...continuation.added,...successor.added,...successor.deltas].map(row=>[row.path,row.afterSha256??row.sha256])]);
  const s4=binding.s4CompletionTime;
  if(s4){
   need(digest(JSON.stringify(s4))==='8261ee0cacc72fb60af1415ab3268900999fa605a64b2460c418e2da61396f5b','NONUSER_PUBLIC_WAKE_S4_COMPLETION_CHANGED');
   need(s4.previousInputSha256===sha256&&s4.sourceApproval===false&&s4.defaultParentApprovalRenewed===false,'NONUSER_PUBLIC_WAKE_S4_COMPLETION_SCHEMA');
  }
  for(const row of [...input.deltas,...(next?.deltas??[])]){
   const file=path.join(repo,row.path),k=key(file);
   need(allowed.get(row.path)===row.beforeSha256&&!inputPredecessor.has(k),'NONUSER_PUBLIC_WAKE_INPUT_SCOPE:'+row.path);
   let current=fs.readFileSync(file);
   if(s4&&row.path===s4.delta.path){
    need(s4.delta.beforeSha256===row.afterSha256,'NONUSER_PUBLIC_WAKE_S4_COMPLETION_PREDECESSOR');
    current=reverseReviewedDelta(current,s4.delta);
   }
   inputPredecessor.set(k,reverseReviewedDelta(current,row));
  }
  inputSuccessorReview={manifest,sha256,deltas:input.deltas.length,...(next?{continuedDeltas:next.deltas.length}:{}),sourceApproval:false,defaultParentApprovalRenewed:false};
 }
 const readInput=(file,...args)=>{
  const restored=inputPredecessor.get(key(file));
  if(!restored)return fs.readFileSync(file,...args);
  need(args.length===0,'NONUSER_PUBLIC_WAKE_INPUT_READ_MODE');return Buffer.from(restored);
 };
 for(const row of successor.added)need(digest(readInput(path.join(repo,row.path)))===row.sha256,'NONUSER_JOINED_DEPENDENCY_CHANGED:'+row.path);
 for(const row of successor.deltas){
  const file=path.join(repo,row.path),k=key(file);
  need(hashes.get(row.path)===row.beforeSha256&&!currentPredecessor.has(k),'NONUSER_PUBLIC_WAKE_CURRENT_SCOPE:'+row.path);
  currentPredecessor.set(k,reverseReviewedDelta(readInput(file),row));
 }
 const readPrior=file=>currentPredecessor.get(key(file))??readInput(file);
 const restored=new Map();
 for(const row of continuation.added)need(digest(readInput(path.join(repo,row.path)))===row.sha256,'NONUSER_ADDED_SOURCE_CHANGED:'+row.path);
 for(const row of continuation.deltas){
  const file=path.join(repo,row.path),k=key(file);
  need(declared.has(row.path)&&!restored.has(k),'NONUSER_PUBLIC_WAKE_CONTINUATION_SCOPE');
  restored.set(k,reverseReviewedDelta(readPrior(file),row));
 }
 const base={...spec,productSources:previous.productSources,sharedLeaves:previous.sharedLeaves};
 delete base.publicWakeSourceReview;need(JSON.stringify(base)===JSON.stringify(previous),'NONUSER_PUBLIC_WAKE_PRIOR_SPEC');
 for(const row of book.pairs){const file=path.join(repo,row.path),k=key(file);need(declared.has(row.path)&&!a.has(k),'NONUSER_PUBLIC_WAKE_PAIR_SCOPE');a.set(k,reverseReviewedDelta(restored.get(k)??readPrior(file),row));}
 const row=book.launchPair;need(row.path==='tools/ymcc_sim/global/host-process.ps1'&&declared.has(row.path),'NONUSER_PUBLIC_WAKE_LAUNCH_SCOPE');
 const file=path.join(repo,row.path);b.set(key(file),reverseReviewedDelta(readPrior(file),row));
 const invoke=(fn,seal,overrides)=>{
  const exact=fn.toString();need(digest(exact)===seal,'NONUSER_PUBLIC_WAKE_ORIGINAL_READER_CHANGED');
  const readFileSync=(file,...args)=>{const k=key(file),body=overrides.get(k)??restored.get(k)??currentPredecessor.get(k);if(!body)return readInput(file,...args);need(args.length===0,'NONUSER_PUBLIC_WAKE_READ_MODE');return Buffer.from(body);};
  const original=vm.compileFunction('return ('+exact+');',['fs','path','vm','createHash','reverseReviewedDelta','digest','need','key'])({readFileSync},path,vm,createHash,reverseReviewedDelta,digest,need,key);
  return original(repo,previous);
 };
 return {source:invoke(verifyOriginalEntrySourceSuccessor,book.guards.source,a),launch:()=>invoke(verifySharedLaunchLeaves,book.guards.launch,b),receipt:{manifest:relative,sha256:expected,pairs:a.size,continuedInputs:restored.size,addedDependencies:[...continuation.added,...successor.added].map(row=>row.path),oldManifestSha256:originalHash,priorManifestSha256:priorHash,successorInputs:currentPredecessor.size,...(inputSuccessorReview?{inputSuccessorReview}:{}),sharedLaunchPair:true,sourceApproval:false,defaultParentApprovalRenewed:false}};
}
