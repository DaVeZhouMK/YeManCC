// Production trailing-edge scheduler; all time and transport are isolated.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';
import {root,task} from './fixtures/decky_task_paths.mjs';
import {load} from './fixtures/decky_game_memory_store.mjs';
const {FanChoiceDebounce,FAN_CHOICES,FAN_CHOICE_DELAY_MS}=load(fs.readFileSync(path.join(root,'decky-plugin/src/fanChoiceDebounce.ts'),'utf8'));
const cases=[];function check(name,run){run();cases.push(name);}
function fixture(){
 let now=0,id=0;const timers=new Map(),sent=[];
 const state={connected:true,busy:false,snapshot:{ready:true,runId:'f'.repeat(32),generation:1,revision:4,actions:{fan:true},game:{identity:'one'},fan:{enabled:false,preset:'balanced',pending:false,canToggle:true,choices:FAN_CHOICES.slice(1)}}};
 const choice=new FanChoiceDebounce({state:()=>state,send:async c=>{sent.push(structuredClone(c));},schedule:(action,delay)=>{timers.set(++id,{action,due:now+delay});return id;},cancel:id=>timers.delete(id)});
 return {state,choice,sent,timers,choose:v=>choice.choose(v,state.snapshot),advance:ms=>{now+=ms;for(const [id,t]of [...timers])if(t.due<=now){timers.delete(id);t.action();}}};
}
check('four fixed fan choices start with OFF and end with aggressive',()=>{assert.deepEqual(Array.from(FAN_CHOICES,c=>c.data),['off','soft','balanced','aggressive']);assert.equal(FAN_CHOICE_DELAY_MS,3000);});
check('2999ms sends nothing and 3000ms sends one final choice',()=>{const f=fixture();f.choose('soft');f.advance(2999);assert.equal(f.sent.length,0);f.advance(1);assert.equal(f.sent.length,1);assert.equal(f.sent[0].value,'soft');assert.equal(f.timers.size,0);});
check('intermediate changes restart the full three-second delay',()=>{const f=fixture();f.choose('soft');f.advance(2900);f.choose('balanced');f.advance(2900);f.choose('aggressive');f.advance(2999);assert.equal(f.sent.length,0);assert.equal(f.timers.size,1);f.advance(1);assert.deepEqual(f.sent.map(c=>c.value),['aggressive']);});
check('return to confirmed OFF cancels without a write',()=>{const f=fixture();f.choose('soft');f.choose('off');f.advance(5000);assert.equal(f.sent.length,0);assert.equal(f.choice.snapshot(),null);assert.equal(f.timers.size,0);});
check('confirmed ON to OFF uses the same delay',()=>{const f=fixture();f.state.snapshot.fan.enabled=true;f.choose('off');f.advance(3000);assert.deepEqual(f.sent.map(c=>c.value),['off']);});
check('same confirmed preset is a no-op',()=>{const f=fixture();f.state.snapshot.fan.enabled=true;f.choose('balanced');assert.equal(f.timers.size,0);});
for(const [name,change] of [
 ['disconnect',f=>f.state.connected=false],['busy',f=>f.state.busy=true],
 ['fan action revoked',f=>f.state.snapshot.actions.fan=false],
 ['toggle revoked',f=>f.state.snapshot.fan.canToggle=false],['new source',f=>f.state.snapshot.runId='b'.repeat(32)],
 ['new generation',f=>f.state.snapshot.generation++],['revision rollback',f=>f.state.snapshot.revision--],
 ['external ON change',f=>f.state.snapshot.fan.enabled=true],['external preset change',f=>f.state.snapshot.fan.preset='soft'],
 ['choice disabled',f=>f.state.snapshot.fan.choices=[{data:'soft',disabled:true}]],
 ])check(name+' cancels the pending intent',()=>{const f=fixture();f.choose('soft');change(f);f.choice.observe(f.state);f.advance(5000);assert.equal(f.sent.length,0);assert.equal(f.choice.snapshot(),null);assert.equal(f.timers.size,0);});
check('source change immediately before timer execution is still rejected',()=>{const f=fixture();f.choose('soft');f.state.snapshot.runId='b'.repeat(32);f.advance(3000);assert.equal(f.sent.length,0);});
check('new harmless revision and game switch retain this global fan intent',()=>{const f=fixture();f.choose('soft');f.state.snapshot.revision=8;f.state.snapshot.game={identity:'two'};f.state.snapshot.fan.pending=true;f.choice.observe(f.state);f.advance(3000);assert.equal(f.sent.length,1);assert.equal(f.sent[0].snapshot.revision,8);assert.equal(f.sent[0].snapshot.game.identity,'two');});
check('unsupported choices are never scheduled',()=>{const f=fixture();for(const v of ['invalid','',null])f.choose(v);assert.equal(f.timers.size,0);});
check('independent Fan permission retains pending choice when shared ready becomes false',()=>{const f=fixture();f.choose('soft');f.state.snapshot.ready=false;f.choice.observe(f.state);f.advance(3000);assert.equal(f.sent.length,1);assert.equal(f.timers.size,0);});
check('unmount disposes its only timer and subscription',()=>{const f=fixture();let count=0;f.choice.subscribe(()=>count++);f.choose('soft');f.choice.dispose();const after=count;f.advance(5000);assert.equal(f.sent.length,0);assert.equal(count,after);assert.equal(f.timers.size,0);});
fs.writeFileSync(path.join(task,'validation/FAN-CHOICE-DEBOUNCE.json'),JSON.stringify({status:'passed',cases:cases.length,results:cases,productionScheduler:true,realHardwareWrites:0},null,2));console.log('Fan choice debounce PASS '+cases.length);
