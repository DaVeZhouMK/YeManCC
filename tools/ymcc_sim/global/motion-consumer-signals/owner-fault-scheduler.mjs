/** Independent fault directories may compile concurrently. Every started fault
 * settles before the caller captures artifacts, including after a failure. */
export const OWNER_FAULT_CONCURRENCY = 2;
export async function runOwnerFaults({controls,execute,result}) {
 const completed=new Array(controls.length);
 const schedule=result.faultScheduling={concurrency:OWNER_FAULT_CONCURRENCY,total:controls.length,events:[]};
 let next=0,active=0,failure=null;
 async function worker(){
  while(!failure&&next<controls.length){
   const index=next++;++active;
   schedule.events.push({phase:'start',index,active,atUtc:new Date().toISOString()});
   let status='PASS';
   try{completed[index]=await execute(controls[index],index);result.faults=completed.filter(row=>row!==undefined);}
   catch(error){status='FAIL';failure??={index,error};}
   finally{--active;schedule.events.push({phase:'finish',index,active,status,atUtc:new Date().toISOString()});}
  }
 }
 await Promise.all(Array.from({length:Math.min(OWNER_FAULT_CONCURRENCY,controls.length)},()=>worker()));
 if(failure)throw failure.error;
 return completed;
}
