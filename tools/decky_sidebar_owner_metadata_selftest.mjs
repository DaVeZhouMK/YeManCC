import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {task} from './fixtures/decky_task_paths.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const wait=async()=>{for(let i=0;i<150;i++)await Promise.resolve();},runId='a'.repeat(32),cases=[];
for(const owner of [undefined,{owner:'YMCC-native',pid:1234},{owner:'YMCC-native',pid:0},{owner:'foreign',pid:1234}]){
 const f=await createMirrorHostFixture({invoke:async(command)=>command==='deckySidebar.mirrorAttach'?{connected:false,...owner}:true});f.ready();await wait();
 f.emit('deckySidebar.mirrorRequest',{connection:'1',runId,request:{command:'snapshot',runId}});await wait();const snapshot=f.replies.at(-1).message.snapshot;
 assert.equal(snapshot.provenance.kind,owner?.owner==='YMCC-native'&&owner.pid>0?'ymcc-native':'unconfirmed');
 f.emit('deckySidebar.mirrorRequest',{connection:'1',runId,request:{command:'fan.setPreset',args:{preset:'soft'},id:'owner-test:1',clientId:'owner-test',sequence:1,runId,generation:snapshot.generation,revision:snapshot.revision}});await wait();
 const reply=f.replies.filter(r=>r.message.type==='reply').at(-1);assert.ok(reply);assert.equal(reply.message.ok,false);assert.equal(f.counts.fanSaves,0);f.stop();cases.push({inertOwner:owner||null,shown:snapshot.provenance.kind,offFanPresetRejected:true});
}
const report={stage:'Mainline19',cases:cases.length,results:cases,inertNativeMetadata:true,actualNativeAppExecuted:false,configWrites:0,physicalHardwareWrites:0};fs.writeFileSync(path.join(task,'validation/MAINLINE19-OWNER-METADATA.json'),JSON.stringify(report,null,2));console.log('Owner provenance/off-state Fan contracts passed');
