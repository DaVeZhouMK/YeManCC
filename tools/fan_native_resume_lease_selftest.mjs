import {createServer} from 'node:http';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import path from 'node:path';import fs from 'node:fs';import assert from 'node:assert/strict';
const root=process.argv[2],run=promisify(execFile),sockets=new Set(),rows=[],failures=[];
const ready={ok:true,resumeAccepted:true,generation:1,state:{state:'Ready',powerState:'On',resumePhase:'Ready',resumePhaseGeneration:1,powerOperationGeneration:1,powerOperationStatus:'completed',powerOperationOutcome:'restored',openCalled:true,openEventsCalled:true,unknownState:false,hcCloseCleanupPending:false,controlAccepting:true},lease:{leaseId:'fixture-lease-0001',generation:1}};
const server=createServer((req,res)=>{const kind=String(req.headers['x-yeman-fan-session']).replace(/^fixture-/,'');let j=structuredClone(ready);
 if(kind==='explicit-false')j.ok=false;
 if(kind==='false-noop'){j.ok=false;j.resumeNoop=true;}
 if(kind==='bad-ok')j.ok='true';
 if(kind==='legacy-ok-absent')delete j.ok;
 if(kind==='legacy-string-lease')j.lease='fixture-lease-0001';
 if(kind==='lease-in-state'){j.state.lease=j.lease;delete j.lease;}
 if(kind==='no-lease')delete j.lease;
 if(kind==='stale-generation'){j.generation=2;j.state.resumePhaseGeneration=2;j.state.powerOperationGeneration=2;}
 if(kind==='unknown-state')j.state.unknownState=true;
 if(kind==='fault-state')j.state.state='FaultLocked';
 if((kind==='pending-post'||kind==='get-power-race')&&req.method==='POST'){j.state.state='Resuming';j.state.powerOperationStatus='executing';j.state.powerState='Resuming';}
 if(kind==='malformed-state'&&req.method==='POST')j.state.controlAccepting='true';
 if(kind==='rejected-code'){j.ok=false;j.error={code:'HC_RESUME_SUPERSEDED'};}
 const send=()=>{res.end(JSON.stringify(j));};if((kind==='manual-race'||kind==='power-race')&&req.method==='POST'||kind==='get-power-race'&&req.method==='GET')setTimeout(send,100);else send();
});server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});await new Promise(r=>server.listen(0,'127.0.0.1',r));
async function test(kind,check){try{const{stdout}=await run(path.join(root,'fixture.exe'),[String(server.address().port),'resume',kind],{windowsHide:true,timeout:5500,maxBuffer:2<<20});const j=JSON.parse(stdout);rows.push({kind,...j});check(j);}catch(e){failures.push(kind+': '+e.message);}}
function terminal(r){return r.lifecycle.find(x=>x.event==='fan-host-resume-native-trigger')?.details.terminal;}
try{
 for(const kind of ['normal','legacy-ok-absent','legacy-string-lease','lease-in-state'])await test(kind,r=>{assert.equal(r.ok,true);assert.equal(r.leaseArms,1);assert.equal(r.currentLease,'fixture-lease-0001');});
 await test('no-lease',r=>{assert.equal(r.ok,true);assert.equal(r.leaseArms,0);});
 await test('power-race',r=>{assert.equal(r.leaseArms,0,'old-generation response armed lease before generation rejection');assert.equal(r.currentLease,'');assert(r.lifecycle.some(x=>x.event==='fan-host-resume-superseded'));});
 await test('get-power-race',r=>{assert.equal(r.leaseArms,0);assert.notEqual(terminal(r),'recovered');assert(r.lifecycle.some(x=>x.event==='fan-host-resume-superseded'));});
 await test('manual-race',r=>{assert.equal(r.currentLease,'new-manual-lease-0002','old recovery replaced newer manual lease');assert.equal(r.leaseArms,0);});
 for(const kind of ['explicit-false','false-noop','bad-ok','stale-generation','unknown-state','fault-state','rejected-code'])await test(kind,r=>{assert.equal(r.leaseArms,0,'non-accepted recovery receipt armed lease');assert.notEqual(terminal(r),'recovered');assert.notEqual(terminal(r),'noop');});
 for(const kind of ['pending-post','malformed-state'])await test(kind,r=>{assert.equal(r.leaseArms,0,'pending/malformed POST cannot authorize native renewal');assert.equal(terminal(r),'recovered');});
}finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}
const result={ok:!failures.length,checks:rows.length,rows,failures,scope:'exact actual native lease-arm and resume worker+WinHTTP; shared generation/manual epoch changed by fixture thread, inert thread launcher, identity/power/log sinks; no Host/HC/device/CPU benchmark'};fs.writeFileSync(path.join(root,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failures},null,2));process.exitCode=result.ok?0:1;
