/** Independent fault directories may compile concurrently. Every started fault
 * settles before the caller captures artifacts, including after a failure. */
export const XINPUT_FAULT_CONCURRENCY = 2;
export async function runXinputFaults({faults:controls,execute,negatives,faultScheduling}) {
 const completed=new Array(controls.length);
 const schedule=faultScheduling;Object.assign(schedule,{concurrency:XINPUT_FAULT_CONCURRENCY,total:controls.length,events:[]});
 let next=0,active=0,failure=null;
 async function worker(){
  while(!failure&&next<controls.length){
   const index=next++;++active;
   schedule.events.push({phase:'start',index,active,atUtc:new Date().toISOString()});
   let status='PASS';
   try{completed[index]=await execute(controls[index],index);negatives.splice(0,negatives.length,...completed.filter(row=>row!==undefined));}
   catch(error){status='FAIL';failure??={index,error};}
   finally{--active;schedule.events.push({phase:'finish',index,active,status,atUtc:new Date().toISOString()});}
  }
 }
 await Promise.all(Array.from({length:Math.min(XINPUT_FAULT_CONCURRENCY,controls.length)},()=>worker()));
 if(failure)throw failure.error;
 return completed;
}
