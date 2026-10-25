import fs from 'node:fs';
import path from 'node:path';
import {verifyScratch,cleanupScratch} from '../fan-monitor-browser-consumer/scratch.mjs';
import {need,within} from './contract.mjs';
// Keep the authenticated root marker until every potentially busy browser child is gone.
export async function cleanupOwnedScratch(record,evidenceRoot){
 const root=verifyScratch(record,evidenceRoot),childrenRemoved=[],retryErrors=[];
 for(const entry of fs.readdirSync(root,{withFileTypes:true})){
  if(entry.name==='.ymcc-browser-owner.json')continue;
  const target=path.join(root,entry.name);verifyScratch(record,evidenceRoot);
  need(within(root,target)&&target!==root&&!entry.isSymbolicLink()&&fs.realpathSync(target)===target,'SLEEP_UI_CLEANUP_CHILD_OWNERSHIP');
  let removed=false;
  for(let attempt=0;attempt<6;attempt++){
   verifyScratch(record,evidenceRoot);
   try{fs.rmSync(target,{recursive:true,force:true,maxRetries:5,retryDelay:200});removed=true;break;}
   catch(error){retryErrors.push({child:entry.name,attempt:attempt+1,code:error.code});if(!['EBUSY','EPERM','ENOTEMPTY'].includes(error.code)||attempt===5)throw error;await new Promise(resolve=>setTimeout(resolve,200));}
  }
  need(removed&&!fs.existsSync(target),'SLEEP_UI_CLEANUP_CHILD_REMAINS');childrenRemoved.push(entry.name);
 }
 const result=cleanupScratch(record,evidenceRoot);
 return {...result,childrenRemoved,retryErrors,markerPreservedUntilFinalCleanup:true};
}
