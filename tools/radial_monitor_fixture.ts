import {writeFileSync} from 'node:fs';
import {defaultRadialSettings} from '../src/radial/model';
import {normalizeMonitorOutput,makeHudFixture} from '../src/bridge/monitorHudModel';
const preferences=normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{layout:'capsule',position:1,dragLocked:false,scale:1,underlay:'none'}});
for(const key of Object.keys(preferences.ymcc.fields))preferences.ymcc.fields[key]=false;
Object.assign(preferences.ymcc.fields,{fps:true,cpuTemp:true,cpuPower:true});
// Explicit protocol fixture; no live telemetry or device source is used.
const data={...makeHudFixture(),source:'ymcc-uniform' as const};
writeFileSync(process.argv[2],JSON.stringify({monitor:{type:'desktop-hud',protocol:1,show:true,preferences,data},wheel:{settings:defaultRadialSettings(),tdp:{available:true,min:5,max:25,value:15,label:'Owned fixture'},feedback:false}}));
