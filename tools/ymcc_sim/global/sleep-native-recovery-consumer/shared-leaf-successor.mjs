import assert from 'node:assert/strict';
import {hash} from 'node:crypto';
import {reverseReviewedDelta} from './script-review.mjs';

const modulePath='tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.mjs';
const bookPath='tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.json';
const bookSha256='49b322ea17a053a66ac636813c3266d8f8e0080e19695c1eda44185bc12ae94a';
const sha=bytes=>hash('sha256',bytes);

export function openSharedLeafReview(spec,readSource){
 const names=new Set(spec.sharedLeaves);
 assert(names.has(bookPath)&&names.has(modulePath),'SHARED_LEAF_REVIEW_UNDECLARED');
 const bookBytes=readSource(bookPath);
 assert.equal(sha(bookBytes),bookSha256,'SHARED_LEAF_REVIEW_BOOK_CHANGED');
 const book=JSON.parse(bookBytes);
 assert(book.schemaVersion===1&&book.sourceApproval===false&&book.reviewScope==='exact-shared-read-predecessors','SHARED_LEAF_REVIEW_SCHEMA');
 const pins=new Map([[bookPath,bookSha256],[modulePath,sha(readSource(modulePath))]]);
 const exactSuccessors=[];
 return {
  verify(row,bytes,refusal){
   assert(names.has(row.path),refusal);
   const current=sha(bytes);
   if(current!==row.sha256){
    const matches=book.deltas.filter(delta=>delta.path===row.path&&delta.beforeSha256===row.sha256&&delta.afterSha256===current);
    assert.equal(matches.length,1,refusal);
    assert.equal(sha(reverseReviewedDelta(bytes,matches[0])),row.sha256,refusal);
    exactSuccessors.push({path:row.path,predecessorSha256:row.sha256,currentSha256:current});
   }
   pins.set(row.path,current);
  },
  pins,
  receipt:()=>({status:'PASS',manifest:bookPath,sha256:bookSha256,exactSuccessors,sourceApproval:false,compiledHistoricalBytes:false}),
 };
}

export function verifySharedLeafPins(spec,readSource,prefix){
 const reviewed=openSharedLeafReview(spec,readSource);
 for(const row of spec.reviewedSharedLeafIdentities)reviewed.verify(row,readSource(row.path),prefix+'_SHARED_LEAF_CHANGED:'+row.path);
 return reviewed;
}
