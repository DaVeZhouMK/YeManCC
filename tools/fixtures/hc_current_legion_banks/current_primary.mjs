// Fixed current HC source oracle; no constructor, device, mode or HC DLL use.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
export function currentLegionPrimary(workspace){
 const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/CurrentLegionBanks-20261018'),reference=JSON.parse(fs.readFileSync(path.join(root,'REFERENCE.json'),'utf8'));
 const tree=JSON.parse(fs.readFileSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/hc-tree.json'),'utf8'));
 if(reference.commit!==tree.sha||tree.sha!=='a250b19a4b7e29dac3b383f2d1ac517d1af43e4e')throw Error('Fixed current reference mismatch');
 const files=reference.files.map(p=>{const b=fs.readFileSync(p.destination),blob=createHash('sha1').update(Buffer.concat([Buffer.from('blob '+b.length+'\0'),b])).digest('hex'),sha256=createHash('sha256').update(b).digest('hex');if(blob!==p.gitBlob||sha256!==p.sha256||tree.tree.find(x=>x.path===p.path)?.sha!==blob)throw Error('Primary content/blob mismatch '+p.path);return{...p,text:b.toString('utf8')};});
 const tablet=files.find(x=>x.path.endsWith('/LegionController.cs')).text,gos=files.find(x=>x.path.endsWith('/LegionControllerS.cs')).text,base=files.find(x=>x.path.endsWith('/LegionControllerBase.cs')).text;
 const constants={};for(const [name,expected]of Object.entries({LCONTROLLER_ACCE_IDX:35,LCONTROLLER_GYRO_IDX:41,RCONTROLLER_ACCE_IDX:48,RCONTROLLER_GYRO_IDX:54})){const m=tablet.match(new RegExp('private byte '+name+' = (\\d+);'));if(!m||Number(m[1])!==expected)throw Error('Current constant '+name);constants[name]=expected;}
 const start=tablet.indexOf('switch (idx)'),open=tablet.indexOf('{',start),tabletSwitch=tablet.slice(start,closeBrace(tablet,open)+1);if(start<0||!tabletSwitch.includes('case 0:')||!tabletSwitch.includes('case 1:'))throw Error('Current actual two-bank switch absent');
 const single=[...gos.matchAll(/^\s*([ag][XYZ]\s*=\s*[^;]+;)/gm)].map(m=>m[1]);if(single.length!==6)throw Error('Six current GoS source assignments required');
 const loop=/for\s*\(byte idx = 0; idx <= 1; \+\+idx\)/.test(tablet);if(!loop||!tablet.includes('Inputs.GyroState.SetGyroscope(gX, gY, gZ);')||!tablet.includes('gamepadMotion.ProcessMotion(gX, gY, gZ, aX, aY, aZ, delta);'))throw Error('Current actual dual processing/readout call contract changed');
 const attachStart=tablet.indexOf('public override void AttachDetails(PnPDetails details)'),attachOpen=tablet.indexOf('{',attachStart);if(attachStart<0||attachOpen<0)throw Error('Actual current AttachDetails absent');const attach=tablet.slice(attachStart,closeBrace(tablet,attachOpen)+1);
 if(!attach.includes('if (IsHidReportMisaligned())')||/CONTROLLER_(ACCE|GYRO)_IDX\s*[-+]?=/.test(attach))throw Error('Current AttachDetails IMU boundary changed');
 return{constants,tabletSwitch,single,attachment:attach,files,reference,minOffsets:{tabletLeft:47,tabletRight:60,goS:26},dualSourceLoop:true,perBankProcessingInHc:true,secondGenerationGzTodo:tablet.includes('todo: invert gZ on LGO2')};
}
