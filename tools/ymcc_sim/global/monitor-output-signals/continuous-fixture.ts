import{createHudSampleSource}from'../../../../src/bridge/monitorHudSampleSource';
import{createHudClock}from'../../../../src/bridge/monitorHudClock';
import{createDesktopHudOwner}from'../../../../src/bridge/desktopHudOwner';
export function makeContinuousOutput(w:any,fixture:any){
 const io=w.__io,original=io.invoke.bind(io);let nativeId=0,sourceUsers=0;const ownerEvents=['desktopHud.ready','desktopHud.painted','desktopHud.closed','desktopHud.expired'];
 io.invoke=async(command:string,args:any)=>{
  if(command==='monitor.hudPulse.set'||command==='monitor.hudPulse.ack'){io.calls.push({command,args:structuredClone(args)});return true;}
  if(command==='desktopHud.status'){io.calls.push({command,args});return{available:true,id:nativeId};}
  if(command==='desktopHud.create'){io.calls.push({command,args:structuredClone(args)});nativeId=io.id;io.emit('desktopHud.ready',{id:io.id,nonce:args.nonce});return{id:io.id,nonce:args.nonce};}
  if(command==='desktopHud.frame'){io.calls.push({command,args:structuredClone(args)});io.emit(command,args.frame);return true;}
  if(command==='desktopHud.layout'){const accepted=await original(command,args);if(accepted)io.emit('desktopHud.painted',{id:io.id,nonce:io.nonce,seq:args.seq,visible:true});return accepted;}
  if(command==='desktopHud.close'){io.calls.push({command,args:structuredClone(args)});nativeId=0;window.dispatchEvent(new Event('pagehide'));return true;}
  return original(command,args);
 };
 let owner:any;
 const clock=createHudClock({now:Date.now,nonce:()=> 'continuous-output-clock-000001',invoke:(cmd,args)=>io.invoke(cmd,args),setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),
 pulse:()=>owner?.advance(),failed:()=>owner?.clockUnavailable('fixture native clock failed')});
 const snapshot=()=>({schema:1,source:'snapshot' as const,session:1,relayEpoch:0,generation:1,seq:1,collectedAt:Date.now(),heartbeatAt:Date.now(),hwinfoOkAt:Date.now(),hwDown:false,topActive:true,fpsActive:false,top:{ts:Date.now(),hwDown:false,tempC:65,tdpW:30},fps:null});
 const source=createHudSampleSource({now:Date.now,subscribeSnapshot:fn=>{fn(snapshot() as any);return()=>{}},subscribeTdp:fn=>{fn({mode:null,status:'unknown',watts:null,proof:null,revision:0,confirmedAt:null});return()=>{}},
  readOnce:async()=>snapshot() as any,acquire:()=>{sourceUsers++;const releaseClock=clock.acquire();return()=>{sourceUsers--;releaseClock();}}});
 owner=createDesktopHudOwner({scope:'global',externalClock:true,target:()=>null,nonce:()=>io.nonce,now:Date.now,sample:source.sample,
 acquire:source.acquire,acquireClock:clock.acquire,invoke:(cmd,args)=>io.invoke(cmd,args),
 observe:listener=>{const off=ownerEvents.map(name=>io.on(name,(event:any)=>listener(name,event)));return()=>off.forEach(fn=>fn());},
 setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number)});
 return{owner,clock,sourceUsers:()=>sourceUsers,dispose:async()=>{await owner.dispose();clock.dispose();}};
}
