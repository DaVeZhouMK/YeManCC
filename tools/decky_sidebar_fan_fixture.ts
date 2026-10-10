// Export only a no-I/O integration fixture; never an installed runtime helper.
import { FanHostLifecycle, type FanHostLauncher } from '../src/bridge/fanHost';
import { createFanMirrorActions } from '../src/bridge/deckyFanActions';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { FanPreset } from '../src/bridge/fanFeature';
import type { PowerLifecycleState } from '../src/bridge/api';
export function createOriginalFanFixture(options:{startWait?:Promise<void>}={}) {
  const host=new NoIoHost();
  const power:PowerLifecycleState={generation:1,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:false,hibernateAvailable:true};
  const launcher:FanHostLauncher={start:async()=>{await options.startWait;return {pid:943,executable:'no-io-fixture.exe'};},stop:async()=>{}};
  const life=new FanHostLifecycle({enabled:true,launcher,adapter:host,heartbeatIntervalMs:5000,readNativePowerState:async()=>power,resumeWaitDeadlineMs:800});
  let active=false,preset:FanPreset='balanced',saveCount=0,handshakeSupported=false;
  const curves={soft:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:15},{tempC:70,dutyPercent:30},{tempC:100,dutyPercent:70}],
    balanced:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:20},{tempC:70,dutyPercent:45},{tempC:100,dutyPercent:90}],
    aggressive:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:30},{tempC:70,dutyPercent:75},{tempC:100,dutyPercent:100}]};
  const actions=createFanMirrorActions({owner:life,enabled:()=>active,currentPreset:()=>preset,curve:name=>structuredClone(curves[name]),
    save:async(_nodes,name)=>{preset=name;saveCount++;},updateActive:value=>{active=value;},updateDuty:()=>{},power:async()=>power,
    featureAllowed:()=>true,uiBusy:()=>false,display:()=>{}});
  return {
    lifecycle:life,
    featureBindings:{FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return active;}},
      getFanFeatureSettings:()=>({preset}),getFanPresetCurve:(name:FanPreset)=>structuredClone(curves[name]),
      saveFanCurve:async(_nodes:unknown,name:FanPreset)=>{preset=name;saveCount++;},setFanControlActive:(value:boolean)=>{active=value;},
      setFanNavigationDuty:()=>{},recordFanHandshake:async(allowed:boolean)=>{handshakeSupported=allowed;}},
    snapshot(label:string) { return {generation:1,ready:true,game:{label,identity:'a.exe:101:A',fields:{}},
      fan:{supported:life.controlReady,canToggle:true,enabled:active,preset,choices:[{data:'soft',label:'轻柔'},{data:'balanced',label:'均衡'},{data:'aggressive',label:'暴力'}]},
      actions:{fan:true,game:false},notice:'Production lifecycle / inert device fixture'}; },
    execute:actions.execute,
    stats:()=>({active,preset,saveCount,handshakeSupported,calls:[...host.calls],lease:life.currentLease,physicalHardwareWrites:0}),
    close:()=>life.close(),
  };
}
