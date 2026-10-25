import assert from 'node:assert/strict';
// Independent representation of the already predicted read counter actual1/expected0.
export function retiredReadAssertionString(){
 try{assert.equal(1,0,'already-retired failure still queried Host');}
 catch(error){return String(error);}
 throw Error('EXPECTED_NODE_ASSERTION_NOT_THROWN');
}
