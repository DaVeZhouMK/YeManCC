import assert from 'node:assert/strict';
import {proc} from '../../../../src/bridge/api';
import {NoIoHost} from '../../../fixtures/fan_no_io_host';
const saved=proc.findExact,paths:string[]=[];
let nativeExportCalls=0,starts=0,stops=0;
proc.findExact=async(file:string)=>{paths.push(file);return{found:false,pid:0};};
const {FanHostLifecycle}=await import('../../../../src/bridge/fanHost');
proc.findExact=async()=>{nativeExportCalls++;throw Error('leaf control must use the captured memory probe');};
const host=new NoIoHost();host.completeResume=true;
const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:0,
 readNativeActivity:async()=>null,
 readNativePowerState:async()=>({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),
 launcher:{async start(){starts++;return{pid:118401,executable:'inert-resume-host.exe',processCreated:'inert-resume-birth',adopted:true};},async stop(){stops++;}}});
const originalState=host.getState;
try{
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 assert((await life.start({manualRecovery:true})).allowed);
 host.getState=async()=>{throw Error('inert status deliberately absent for leaf-only probe');};
 assert.equal(await (life as any).assertReachableBeforeShortCircuit(true),null);
 assert.equal(paths.length,2);assert.equal(nativeExportCalls,0);
 host.getState=originalState;await life.close();
 assert.equal(nativeExportCalls,0);assert.equal(starts,1);
 console.log(JSON.stringify({ok:true,kind:'fan-unconfirmed-resume-leaf-boundary',capturedFakeFindExactCalls:paths.length,nativeExportCalls,memoryLauncherStarts:starts,memoryLauncherStops:stops,hardwareCalls:0,realPidControlled:false,scope:'Separate actual FanHost captured leaf IO control; not an additional original30 business case'}));
}finally{host.getState=originalState;proc.findExact=saved;}
