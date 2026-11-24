import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {runtimeBundle} from './harness.mjs';

export async function runOptionalOwner(out,args=[]){
 const choice=name=>args.find(v=>v.startsWith('--optional-action-'+name+'='))?.slice(('--optional-action-'+name+'=').length);
 const kinds=['revision','hash','mode','result','observed','applied','active','ack','unrelated','current'];
 const stages=['charge','display','appearance','fan','sleep','monitor','brightness'];
 const selected={mode:choice('mode'),stage:choice('stage'),kind:choice('kind')};
 assert(!selected.mode||['global','xbox','steam','xbox-steam'].includes(selected.mode));
 assert(!selected.stage||stages.includes(selected.stage));assert(!selected.kind||kinds.includes(selected.kind));
 const source=choice('source'),a=await runtimeBundle(out,source?{bootModesSource:source}:{}),q=a.ports,rows=[];
 const raw=path.join(out,'optional-owner-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 for(const mode of a.model.BOOT_MODE_IDS)for(const stage of stages)for(const kind of kinds){
  if(Object.entries(selected).some(([key,value])=>value&&({mode,stage,kind})[key]!==value))continue;
  a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();q.reset();
  const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  for(const scene of Object.values(doc.scenes))for(const key of a.model.BOOT_MODE_MODULES)delete scene.overrides[key];
  doc.global.audioOptimization={enabled:false};doc.global.frameGeneration={enabled:false};doc.global.softwareLaunch={paths:[],timing:'manual',delay:0};
  const old={id:'1280x800@60/0',width:1280,height:800,refresh:60,orientation:0},next={id:'1920x1080@120/0',width:1920,height:1080,refresh:120,orientation:0};
  q.state.displayModes={current:old.id,modes:[old,next]};
  if(stage==='charge')doc.global.charge={enabled:true,limit:60};
  if(stage==='display'||stage==='brightness')doc.global.display={brightness:stage==='brightness'?75:50,resolution:'1920x1080',hz:'120'};
  if(stage==='appearance')doc.global.steamAppearance={states:{Round:true}};
  if(stage==='fan')doc.global.fan={enabled:true,preset:'soft'};
  if(stage==='sleep')doc.global.sleep={hibernate:true,hibernateSize:60,timeouts:{acSleep:1}};
  if(stage==='monitor')doc.global.monitor={preferences:{...doc.global.monitor.preferences,output:'ymcc',enabled:true,ymccEnabled:true}};
  q.settings.extensions.bootModes=structuredClone(doc);
  let changed=false,at=0;
  const mutate=()=>{
   if(changed||q.settings.extensions.bootModes.revision!==doc.revision+1)return;
   changed=true;at=q.calls.length;
   if(kind==='revision')q.settings.input.revision++;
   if(kind==='hash')q.settings.input.gyroMotion.motionSensitivityX+=0.25;
   if(kind==='mode')q.settings.extensions.bootModes.revision++;
   if(kind==='result')q.settings.extensions.bootModesStartupResult={schema:1,revision:99};
   if(kind==='observed')q.settings.input.startupObservedBoot='other-boot';
   if(kind==='applied')q.settings.extensions.bootModesAppliedBoot='other-applied';
   if(kind==='active')q.settings.extensions.bootModesActiveBoot='other-active';
   if(kind==='ack')q.settings.input.hostAcknowledgedRevision=7;
   if(kind==='unrelated')q.settings.extensions.peer={keep:true};
  };
  const hooks={charge:'chargeRead',display:'displayRead',appearance:'appearanceRead',fan:'fanStart',sleep:'hibernateRead',monitor:'monitorSave',brightness:'brightnessResult'};
  q.hooks[hooks[stage]]=()=>{mutate();if(stage==='brightness')return {saved:true,applied:true};};
  let error='';try{await a.boot.applyBootMode(mode,doc,{startup:true});}catch(e){error=String(e);}
  const observedOps={charge:['charge.set','charge.release'],display:['display.set'],appearance:['appearance.apply'],fan:['fan.preset'],sleep:['hibernate.set','hibernate.size','sleep.timeout'],monitor:['monitor.apply'],brightness:['display.set']};
  const late=q.calls.slice(at).filter(row=>observedOps[stage].includes(row.op));
  if(stage==='sleep'&&!a.model.BOOT_MODE_MODULES.includes('sleep')){
   const row={mode,stage,kind,retired:true,changed,error,lateOps:late.map(v=>v.op)};
   fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(!changed&&!error&&late.length===0,'Retired module reached a sleep owner');rows.push(row);continue;
  }
  const allowed=['ack','unrelated','current'].includes(kind),row={mode,stage,kind,changed,allowed,error,lateOps:late.map(v=>v.op)};
  fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(changed,'Selected optional module boundary not reached '+JSON.stringify(row));
  if(allowed){assert(!error,JSON.stringify(row));assert(late.length>0,'Current optional module did not reach original action '+JSON.stringify(row));}
  else assert.equal(late.length,0,'expired optional module reached later action: '+JSON.stringify(row));
  rows.push(row);
 }
 a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
 const result={status:'PASS',actualRows:rows.length,selected,sourceOverride:a.sourceOverride,rows,
  scope:'Actual boot consumer, memory native/module owners; observations only cover caller await boundaries, not in-flight hardware cancellation',realHardwareOperations:0};
 fs.writeFileSync(path.join(out,'optional-owner-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
