import {createServer} from 'node:http';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
const root=process.argv[2],run=promisify(execFile),sockets=new Set(),rows=[];
const receipt={ok:true,state:{state:'Ready',protocolVersion:'2',hardwareWritesEnabled:true,openCalled:true,openEventsCalled:true,lease:{leaseId:'fixture-lease-0001',generation:1},leaseGeneration:1}};
const server=createServer((req,res)=>{const kind=new URL(req.url,'http://localhost').searchParams.get('kind');assert.equal(req.headers['x-yeman-fan-session'],'fixture-session');const b=JSON.stringify(receipt);
 if(kind==='partial'||kind==='partial-close'||kind==='partial-denied'){res.writeHead(kind==='partial-denied'?401:200,{'Content-Length':Buffer.byteLength(b)+40});res.write(b);setTimeout(()=>res.destroy(),35).unref();return;}
 if(kind==='partial-chunked'){res.writeHead(200);res.write(b);setTimeout(()=>res.destroy(),35).unref();return;}
 if(kind==='chunked'){res.writeHead(200);res.write(b.slice(0,10));res.end(b.slice(10));return;}
 if(kind==='slow-body'||kind==='cancel-body'){res.writeHead(200);res.write('{');const timer=setInterval(()=>{if(!res.destroyed)res.write(' ');},35);res.on('close',()=>clearInterval(timer));return;}
 if(kind==='denied'){res.writeHead(401);res.end(JSON.stringify({ok:false,error:{code:'AUTH_UNAUTHORIZED'}}));return;}
 if(kind==='unicode'){res.end(JSON.stringify({...receipt,note:'微星风扇确认'}));return;}
 if(kind==='head'){res.writeHead(200,{'Content-Length':100});res.end();return;}
 res.end(b);
});server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});await new Promise(r=>server.listen(0,'127.0.0.1',r));
let failure='';async function test(kind,mode,check){const {stdout}=await run(path.join(root,'fixture.exe'),[String(server.address().port),kind,mode],{windowsHide:true,timeout:2200,maxBuffer:1<<20});const j=JSON.parse(stdout);rows.push({kind,mode,...j});check(j);}
try{
 await test('normal','post',r=>{assert.equal(r.status,200);assert.equal(r.armed,1);assert.equal(r.activityAccepted,1);});
 await test('partial','post',r=>{assert(r.threw||r.bodyComplete===false,'parseable truncated reply was accepted');assert.equal(r.armed,0);assert.equal(r.activityAccepted,0);assert.equal(r.displayObserved,0);});
 await test('chunked','post',r=>{assert.equal(r.status,200);assert.notEqual(r.bodyComplete,false);assert.equal(r.armed,1);});
 await test('partial-chunked','post',r=>{assert(r.threw||r.bodyComplete===false,'unterminated chunks were accepted');assert.equal(r.armed,0);assert.equal(r.activityAccepted,0);assert.equal(r.displayObserved,0);});
 await test('partial-close','close',r=>{assert(r.threw||r.bodyComplete===false);assert.equal(r.disarmed,1,'explicit lifecycle close still disarms backup renewal');assert.equal(r.activityAccepted,0);});
 await test('denied','post',r=>{assert.equal(r.status,401);assert.equal(r.authRejected,1);assert.equal(r.armed,0);});
 await test('head','head',r=>{assert.equal(r.status,200);assert.notEqual(r.bodyComplete,false);assert.equal(r.body,'');});
 await test('partial','get',r=>{assert.equal(r.bodyComplete,false);assert.equal(r.activityAccepted,0);assert.equal(r.displayObserved,0);});
 await test('partial-denied','post',r=>{assert.equal(r.status,401);assert.equal(r.bodyComplete,false);assert.equal(r.authRejected,1);assert.equal(r.activityAccepted,0);});
 await test('slow-body','post',r=>{assert.equal(r.bodyComplete,false);assert(r.elapsed>=450&&r.elapsed<1100);assert.equal(r.armed,0);assert.equal(r.displayObserved,0);});
 await test('cancel-body','post',r=>{assert.equal(r.bodyComplete,false);assert(r.elapsed<500);assert.equal(r.armed,0);});
 await test('unicode','post',r=>{assert.equal(r.bodyComplete,true);assert.match(r.body,/微星/);assert.equal(r.headerConversions,1);});
 await test('normal','nonfan',r=>{assert.equal(r.status,200);assert.equal(r.bodyComplete,undefined);assert.equal(r.displayObserved,0);assert.equal(r.headerConversions,1);});
 await test('partial','nonfan',r=>{assert.equal(r.status,200);assert.equal(r.bodyComplete,undefined);assert.equal(r.displayObserved,0);});

}catch(e){failure=e.stack??String(e);}finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}
const result={ok:!failure,checks:rows.length,rows,failure,scope:'extracted actual native reg_http/crackHttpUrl/observer; real WinHTTP; fake downstream activity/display/identity/auth sink; no HC/device/CPU benchmark'};fs.writeFileSync(path.join(root,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failure},null,2));process.exitCode=failure?1:0;
