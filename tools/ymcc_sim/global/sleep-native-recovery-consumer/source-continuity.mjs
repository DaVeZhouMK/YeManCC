import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {hash} from 'node:crypto';

const bookPath='tools/ymcc_sim/global/sleep-native-recovery-consumer/source-continuity.json';
const previousBookHash='d7d5887b495751584ba541bc295fbb4a5489d3c74babdf5a12ec5549ebc8bc16';
const previousSuccessorHash='b89fe83bfb16e3086401582aacb58b6c4234971b627822465b752b2a6401ae56';
const bookHash='a2f6ca59d4082b895184c9b977461ead429ca9ec4684d87cc1cab95cbe3566d3';
const sha=b=>hash('sha256',b);

function parseBook(manifest){
 assert.equal(sha(manifest),bookHash,'SOURCE_CONTINUITY_BOOK_CHANGED');
 const {exactCurrentInputs,...previous}=JSON.parse(manifest);
 assert(exactCurrentInputs?.schemaVersion===1&&exactCurrentInputs.sourceApproval===false&&
  exactCurrentInputs.previousBookSha256===previousSuccessorHash,'SOURCE_CONTINUITY_EXACT_INPUTS_SCHEMA');
 assert.equal(sha(JSON.stringify(previous)+'\n'),previousSuccessorHash,'SOURCE_CONTINUITY_PREDECESSOR_SUCCESSOR_CHANGED');
 const latest=new Map(exactCurrentInputs.deltas.map(delta=>[delta.path,delta]));
 assert.equal(latest.size,exactCurrentInputs.deltas.length,'SOURCE_CONTINUITY_DUPLICATE_EXACT_INPUT');
 const {currentSourceSuccessor,...book}=previous;
 assert(currentSourceSuccessor?.schemaVersion===1&&currentSourceSuccessor.sourceApproval===false&&
  currentSourceSuccessor.previousBookSha256===previousBookHash,'SOURCE_CONTINUITY_CURRENT_SUCCESSOR_SCHEMA');
 assert.equal(sha(JSON.stringify(book)+'\n'),previousBookHash,'SOURCE_CONTINUITY_PREDECESSOR_BOOK_CHANGED');
 const successor=new Map(currentSourceSuccessor.deltas.map(delta=>[delta.path,delta]));
 assert.equal(successor.size,currentSourceSuccessor.deltas.length,'SOURCE_CONTINUITY_DUPLICATE_CURRENT_SUCCESSOR');
 return {book,successor,latest};
}
function reverseSuccessors(name,bytes,latest,successor,reverseReviewedDelta){
 for(const stage of [latest,successor]){
  const delta=stage.get(name);
  if(delta)bytes=reverseReviewedDelta(bytes,delta);
 }
 return bytes;
}
const readCurrentS4Ingress=()=>parseBook(fs.readFileSync(new URL('./source-continuity.json',import.meta.url))).book;
export function reverseCurrentS4Ingress(name,bytes,reverseReviewedDelta){
 const {book,successor,latest}=parseBook(fs.readFileSync(new URL('./source-continuity.json',import.meta.url)));
 bytes=reverseSuccessors(name,bytes,latest,successor,reverseReviewedDelta);
 for(const stage of [book.s4FanManualIntentDeltas,book.s4ReadyTicketDeltas,book.s4ReadyTicketPriorDeltas,book.s4FanIngressDeltas,book.s4GameWakeDeltas,book.s4IngressDeltas]){
  const delta=stage.find(row=>row.path===name);
  if(delta)bytes=reverseReviewedDelta(bytes,delta);
 }
 return bytes;
}
export function reverseS4IngressDefinition(bytes,reverseReviewedDelta){
 const book=readCurrentS4Ingress();
 if(book.s4ReadyTicketDefinition)bytes=reverseReviewedDelta(bytes,book.s4ReadyTicketDefinition);
 return reverseReviewedDelta(bytes,book.s4IngressDefinition);
}

export function verifySourceContinuity(repo,kind,spec,review,reverseReviewedDelta,read=name=>fs.readFileSync(path.join(repo,name))){
 const current=new Map(), historical=new Map();
 function load(name){
  if(!current.has(name))current.set(name,sha(read(name)));
  const bytes=read(name);
  assert.equal(sha(bytes),current.get(name),'SOURCE_MOVED_DURING_REVIEW:'+name);
  return bytes;
 }
 const {book,successor,latest}=parseBook(load(bookPath)), row=book.reviews[kind];
 assert(book.schemaVersion===1&&book.sourceApproval===false&&row,'SOURCE_CONTINUITY_SCHEMA');
 assert.equal(sha(load(book.verifier.path)),book.verifier.sha256,'SOURCE_CONTINUITY_VERIFIER_CHANGED');
 const entries=new Map(book.entryDeltas.map(delta=>[delta.path,delta]));
 const deltas=new Map(row.deltas.map(delta=>[delta.path,delta]));
 const ingress=new Map(book.s4IngressDeltas.map(delta=>[delta.path,delta]));
 const gameWake=new Map(book.s4GameWakeDeltas.map(delta=>[delta.path,delta]));
 const fanIngress=new Map(book.s4FanIngressDeltas.map(delta=>[delta.path,delta]));
 const readyTicket=new Map(book.s4ReadyTicketDeltas.map(delta=>[delta.path,delta]));
 const readyPrior=new Map(book.s4ReadyTicketPriorDeltas.map(delta=>[delta.path,delta]));
 const fanManualIntent=new Map(book.s4FanManualIntentDeltas.map(delta=>[delta.path,delta]));
 assert.equal(ingress.size,book.s4IngressDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_INGRESS');
 assert.equal(gameWake.size,book.s4GameWakeDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_S4_GAME_WAKE');
 assert.equal(fanIngress.size,book.s4FanIngressDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_S4_FAN_INGRESS');
 assert.equal(readyTicket.size,book.s4ReadyTicketDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_S4_READY_TICKET');
 assert.equal(readyPrior.size,book.s4ReadyTicketPriorDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_S4_READY_PRIOR');
 assert.equal(fanManualIntent.size,book.s4FanManualIntentDeltas.length,'SOURCE_CONTINUITY_DUPLICATE_S4_FAN_MANUAL_INTENT');
 const predecessor=name=>{
  let bytes=reverseSuccessors(name,load(name),latest,successor,reverseReviewedDelta);
  for(const stage of [fanManualIntent,readyTicket,readyPrior,fanIngress,gameWake,ingress]){const delta=stage.get(name);if(delta)bytes=reverseReviewedDelta(bytes,delta);}
  return bytes;
 };
 const declared=new Set(kind==='native'?spec.sources:[...spec.productSources,...spec.sharedLeaves]);
 // Bind current compile inputs even when an original review did not read them.
 const reviewedPaths=new Set([...declared,...row.contracts.map(pin=>pin.path),...entries.keys()]);
 let currentReverseDeltas=0;
 for(const name of new Set([...latest.keys(),...successor.keys()]))if(reviewedPaths.has(name)){
  reverseSuccessors(name,load(name),latest,successor,reverseReviewedDelta);
  currentReverseDeltas++;
 }
 assert.equal(entries.size,book.entryDeltas.length);
 assert.equal(deltas.size,row.deltas.length);
 for(const entry of entries.values())reverseReviewedDelta(predecessor(entry.path),entry);
 const rebases=new Map(book.currentRebases.map(delta=>[delta.path,delta]));
 assert.equal(rebases.size,book.currentRebases.length,'SOURCE_CONTINUITY_DUPLICATE_REBASE');
 for(const pin of row.contracts){
  const now=predecessor(pin.path), entry=entries.get(pin.path), delta=deltas.get(pin.path);
  assert(!historical.has(pin.path),'SOURCE_CONTINUITY_DUPLICATE:'+pin.path);
  assert(!delta||(declared.has(pin.path)&&!entry),'SOURCE_CONTINUITY_UNDECLARED_DELTA:'+pin.path);
  const rebase=rebases.get(pin.path);
  assert(!rebase||(declared.has(pin.path)&&!entry),'SOURCE_CONTINUITY_UNDECLARED_REBASE:'+pin.path);
  const basis=entry?reverseReviewedDelta(now,entry):rebase?reverseReviewedDelta(now,rebase):now;
  assert.equal(sha(basis),pin.sha256,'SOURCE_CONTINUITY_INPUT_CHANGED:'+pin.path);
  historical.set(pin.path,delta?reverseReviewedDelta(basis,delta):basis);
 }
 function projected(name){
  assert(historical.has(name),'SOURCE_CONTINUITY_UNREVIEWED_INPUT:'+name);
  return Buffer.from(historical.get(name));
 }
 const priorSpec=JSON.parse(projected(row.specPath));
 assert.deepEqual(priorSpec,spec,'SOURCE_CONTINUITY_CURRENT_SPEC_CHANGED');
 const receipt=review(priorSpec,projected);
 assert.deepEqual(receipt,row.historicalReceipt,'SOURCE_CONTINUITY_PREDECESSOR_RECEIPT_CHANGED');
 return {
  receipt:{...receipt,continuity:{manifest:bookPath,sha256:bookHash,kind,exactReverseDeltas:row.deltas.length,currentReverseDeltas,reviewedReads:row.contracts.length,compiledHistoricalBytes:false,sourceApproval:false}},
  historicalHash:name=>sha(projected(name)),
  verifyCaptured(name,captured){if(current.has(name))assert.equal(sha(captured),current.get(name),'SOURCE_CONTINUITY_CAPTURE_CHANGED:'+name);},
 };
}
