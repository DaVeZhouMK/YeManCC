import assert from 'node:assert/strict';
export function forbiddenCloseReadAssertionString(){try{assert.equal(1,0);}catch(error){return String(error);}throw Error('EXPECTED_NODE_ASSERTION_NOT_THROWN');}
