// Explicit current-source declarations own their route even when admission
// later fails. A stale or missing current prerequisite never falls back to B54.
import {currentPreparedProfileActive} from './r3-current-prepared.mjs';
import {b54ActualProfileActive} from './r3-b54-actual-worker.mjs';
export function selectCoordinatorProfile(repo,{current=currentPreparedProfileActive,b54=b54ActualProfileActive}={}){
 const selected=current(repo);
 if(typeof selected!=='boolean')throw Error('COORDINATOR_CURRENT_SELECTION_NOT_BOOLEAN');
 if(selected)return 'current';
 const historical=b54(repo);
 if(typeof historical!=='boolean')throw Error('COORDINATOR_B54_SELECTION_NOT_BOOLEAN');
 return historical?'b54':'legacy';
}
