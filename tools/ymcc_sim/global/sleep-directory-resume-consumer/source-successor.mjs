import vm from 'node:vm';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {verifyCurrentSleepSourceReview} from '../sleep-non-user-wake-consumer/compiler-leaf.mjs';
import {reverseReviewedDelta} from '../sleep-native-recovery-consumer/script-review.mjs';

const expected='b7122f2e45ea12cba277a885561767961ae7b1cff4337773e997bdce831d9b44';
const previousInputBook='3224596b129897c0e54fa39f08de921d472798fe30c29eda22a0b0234f41b326';
const predecessor='643020dcbe26fe310a6b13a8c643ef1ab3fe69b732fda38ffdcb4ddf085c83e4';
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const requireIdentity=(ok, detail)=>{if(!ok)throw Error('DIRECTORY_RESUME_SOURCE_SUCCESSOR:'+detail);};

export function readDirectorySourceManifest(bytes){
 requireIdentity(digest(bytes)===expected,'manifest changed');
 const review=JSON.parse(bytes);
 requireIdentity(review.schemaVersion===1&&review.kind==='directory-exact-source-successor'&&review.sourceApproval===false,'schema');
 const {inputContinuation,...previousReview}=review;
 requireIdentity(inputContinuation?.schemaVersion===1&&inputContinuation.previousManifestSha256===previousInputBook&&inputContinuation.sourceApproval===false&&digest(JSON.stringify(previousReview,null,2)+'\n')===previousInputBook,'prior input book changed');
 const {successor,...historical}=previousReview;
 requireIdentity(successor?.schemaVersion===1&&successor.previousManifestSha256===predecessor&&successor.sourceApproval===false&&digest(JSON.stringify(historical,null,2)+'\n')===predecessor,'prior directory book changed');
 return review;
}

export function verifyDirectorySourceSuccessor(repo, specPath, manifestPath, captured){
 const source=name=>{
  const bytes=captured.get(name);
  requireIdentity(Buffer.isBuffer(bytes),name+':not captured');
  return bytes;
 };
 const review=readDirectorySourceManifest(source(manifestPath)), spec=JSON.parse(source(specPath));
 requireIdentity(digest(source(specPath))===review.inputContinuation.specSha256,'registered spec changed');
 const pin=(name,sha)=>{
  const bytes=source(name);
  requireIdentity(digest(bytes)===sha,name+':bytes changed');
  return bytes;
 };
 const original=review.originalReview, originalManifest=JSON.parse(pin(original.path,original.sha256));
 const successor=review.successor, priorBook=review.continuedBooks.find(row=>row.path===successor.book.path);
 const {publicInputs,deltas}=review.inputContinuation;
 const inputs=JSON.parse(pin(publicInputs.path,publicInputs.sha256));
 requireIdentity(inputs.schemaVersion===1&&inputs.kind==='exact-public-wake-input-successor'&&inputs.sourceApproval===false&&inputs.defaultParentApprovalRenewed===false&&inputs.previousManifestSha256===successor.book.afterSha256,'public input successor changed');
 const priorInputs=new Map(review.targets.map(row=>[row.path,row.current]));
 for(const row of successor.targets)priorInputs.set(row.path,row.afterSha256);
 const inputPredecessors=new Map();
 for(const row of [...inputs.deltas,...deltas]){
  requireIdentity(priorInputs.get(row.path)===row.beforeSha256&&!inputPredecessors.has(row.path),row.path+':input predecessor');
  inputPredecessors.set(row.path,reverseReviewedDelta(pin(row.path,row.afterSha256),row));
 }
 const priorInput=(name,sha)=>inputPredecessors.get(name)??pin(name,sha);
 requireIdentity(priorBook?.sha256===successor.book.beforeSha256,'public wake predecessor binding');
 const {successor:publicWake,...historicalBook}=JSON.parse(pin(successor.book.path,successor.book.afterSha256));
 const historicalBytes=Buffer.from(JSON.stringify(historicalBook,null,2)+'\n');
 requireIdentity(publicWake?.previousManifestSha256===priorBook.sha256&&digest(historicalBytes)===priorBook.sha256,'public wake predecessor changed');
 const books=new Map(Object.entries(review.books).map(([name,sha])=>{
  const pair=review.continuedBooks.find(entry=>entry.path===name);
  if(!pair)return [name,JSON.parse(pin(name,sha))];
  const {continuation,...previous}=JSON.parse(name===priorBook.path?historicalBytes:pin(name,pair.sha256));
  requireIdentity(sha===pair.predecessorSha256&&continuation?.previousManifestSha256===sha&&digest(JSON.stringify(previous,null,2)+'\n')===sha,name+':historical book changed');
  return [name,previous];
 }));
 for(const [name,sha] of Object.entries(review.implementations))pin(name,sha);
 const continued=new Map();
 for (const row of successor.targets){
  const prior=review.targets.find(x=>x.path===row.path);
  const delta=Number.isInteger(row.publicWakeDelta)?publicWake.deltas[row.publicWakeDelta]:row.delta;
  requireIdentity(prior?.current===row.beforeSha256&&delta?.path===row.path&&delta.beforeSha256===row.beforeSha256&&delta.afterSha256===row.afterSha256&&!continued.has(row.path),row.path+':current continuation');
  continued.set(row.path,reverseReviewedDelta(priorInput(row.path,row.afterSha256),delta));
 }
 const shared=new Map(), previous=new Map();
 for(const row of review.targets){
  let body=continued.get(row.path)??priorInput(row.path,row.current);
  for(const ref of row.reverse){
   const delta=ref.book?ref.pointer.slice(1).split('/').reduce((x,k)=>Object.hasOwn(x,k)?x[k]:null,books.get(ref.book)):review.deltas[ref.delta];
   requireIdentity(delta?.path===row.path,row.path+':delta owner');
   body=reverseReviewedDelta(body,delta);
  }
  const out=row.kind==='directory-shared'?shared:previous;
  requireIdentity(!out.has(row.path)&&digest(body)===row.expected,row.path+':historical identity');
  out.set(row.path,body);
 }
 for(const row of spec.reviewedSharedLeafIdentities)requireIdentity(shared.has(row.path)&&digest(shared.get(row.path))===row.sha256,row.path+':directory registration');
 for(const row of [...originalManifest.changes,...originalManifest.contracts])requireIdentity(previous.has(row.path),row.path+':original registration');
 const exact=verifyCurrentSleepSourceReview.toString();
 requireIdentity(digest(exact)===original.functionSha256,'original verifier changed');
 // Give the unchanged verifier only the captured manifest and exact predecessors.
 const reader={readFileSync(file){
  const name=path.relative(repo,file).split(path.sep).join('/');
  if(name===original.path)return source(name);
  requireIdentity(previous.has(name),name+':not in original review');
  return previous.get(name);
 }};
 const verify=vm.compileFunction('return ('+exact+');',['fs','path','createHash'])(reader,path,createHash);
 return {...verify(repo),directorySuccessor:{manifest:manifestPath,sha256:expected,sharedInputs:shared.size,originalInputs:previous.size,continuedInputs:continued.size,restoredInputPredecessors:inputPredecessors.size,oldIdentitiesRetained:true,sourceApproval:false}};
}
