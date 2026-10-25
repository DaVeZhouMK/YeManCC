import{makeContinuousOutput}from'./continuous-fixture';
import{makeHudFixture,normalizeMonitorOutput,HUD_METRICS}from'../../../../src/bridge/monitorHudModel';
import*as output from'../../../../src/bridge/monitorOutput';
import*as layout from'../../../../src/bridge/monitorLayout';
const w=window as any;
const __fixture={
 output,layout,
 hud(seq:number,patch:any={}){const now=Date.now(),prefs=normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{layout:'capsule',underlay:'none',hideUnavailable:false,fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,m.id==='clock']))}}),data={...makeHudFixture(),source:'ymcc-uniform',stale:false,values:{...makeHudFixture().values,clock:'12:34'},freshUntil:{hardware:now+1000,fps:now+1000}};
 return{type:'desktop-hud',protocol:1,hudId:w.__io.id,nonce:w.__io.nonce,seq,expiresAt:now+5000,show:true,preferences:prefs,data,...patch};},
 notice(seq:number,patch:any={}){const now=Date.now(),feedback={schemaVersion:1,id:'feedback-'+seq,actionId:'display.brightnessAdjust',origin:'controller-shortcut',state:'applied',title:'夹具亮度',message:'已确认亮度',occurredAtUnixMs:now,expiresAtUnixMs:now+4000,powerGeneration:1,value:{text:'40%',number:40,unit:'%'}};
 return{type:'monitor-notice',protocol:1,hudId:w.__io.id,nonce:w.__io.nonce,seq,expiresAt:feedback.expiresAtUnixMs,feedback,appearance:{opacity:.8,theme:'blue-black'},...patch};},
};
w.__fixture=__fixture;w.__fixture.continuous=()=>makeContinuousOutput(w,__fixture);
