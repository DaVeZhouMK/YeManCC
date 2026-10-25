import assert from 'node:assert/strict';
export function cancelledLaunchAssertionString(){try{assert.equal(1,0,'cancelled launch continuation sent a handshake');}catch(error){return String(error);}throw Error('EXPECTED_NODE_ASSERTION_NOT_THROWN');}
