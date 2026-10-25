import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=process.argv[2],variant=process.argv[3]??'CONTROL',run=promisify(execFile);
const requests=[],sockets=new Set(),cases=[];
const receipt={ok:true,lease:{leaseId:'fixture-lease-0001',generation:1,expiresAtMonoMs:15000}};
const server=createServer((req,res)=>{
 const token=req.headers['x-yeman-fan-session']??'';const kind=String(token).replace('fixture-token-','');
 requests.push({kind,method:req.method,url:req.url});
 if(req.method!=='POST'||req.url!=='/api/heartbeat'||!String(token).startsWith('fixture-token-')){res.writeHead(403);res.end('{}');return;}
 if(kind==='denied'){const body=JSON.stringify({ok:false,error:{code:'AUTH_UNAUTHORIZED'}});res.writeHead(401,{'Content-Length':Buffer.byteLength(body)});res.end(body);return;}
 if(kind==='undeclared-length'){res.writeHead(200);res.write(JSON.stringify(receipt));res.end();return;}
 if(kind==='wrong-lease'){res.end(JSON.stringify({ok:true,lease:{...receipt.lease,leaseId:'fixture-other-lease'}}));return;}
 if(kind==='malformed'){res.end(JSON.stringify({ok:'true',lease:receipt.lease}));return;}
 if(kind==='large'){res.end(JSON.stringify({...receipt,padding:'x'.repeat(65536)}));return;}
 if(kind==='partial'){const b=JSON.stringify(receipt);res.writeHead(200,{'Content-Length':Buffer.byteLength(b)+40});res.write(b);setTimeout(()=>res.destroy(),40).unref();return;}
 if(kind==='slow-body'||kind==='cancel-body'||kind==='exit-body'){
  res.writeHead(200);res.write('{');const timer=setInterval(()=>{if(res.destroyed){clearInterval(timer);return;}res.write(' ');},40);res.on('close',()=>clearInterval(timer));return;
 }
 if(kind==='cancel-headers'){setTimeout(()=>{if(!res.destroyed)res.end(JSON.stringify(receipt));},500).unref();return;}
 res.end(JSON.stringify(receipt));
});
server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
let failure='';
async function test(kind,check){
 const before=requests.length;let stdout;
 try{stdout=(await run(path.join(root,variant,'fixture.exe'),[String(port),kind],{windowsHide:true,timeout:3400,maxBuffer:1<<20})).stdout;}
 catch(e){cases.push({kind,failed:true,killed:e.killed,code:e.code,stdout:e.stdout,reason:'fixture did not settle within bounded test deadline'});throw e;}
 const r=JSON.parse(stdout);cases.push({kind,...r});assert(requests.slice(before).every(x=>x.method==='POST'&&x.url==='/api/heartbeat'));
 check(r,requests.length-before);
}
try{
 await test('normal',r=>{assert(r.ok);assert.equal(r.lease,receipt.lease.leaseId);assert.equal(r.status,200);});
 await test('wrong-lease',r=>{assert(!r.ok);assert.equal(r.lease,'');});
 await test('malformed',r=>{assert(!r.ok);assert.equal(r.lease,'');});
 await test('denied',r=>{assert(!r.ok);assert.equal(r.status,401);assert.equal(r.code,'AUTH_UNAUTHORIZED');assert.equal(r.authRejected,1);assert.equal(r.lease,'');});
 await test('large',r=>{assert(!r.ok);assert.equal(r.lease,'');assert.equal(r.displayObserved,0);});
 await test('partial',r=>{assert(!r.ok);assert.equal(r.lease,'');assert.equal(r.displayObserved,0);});
 await test('undeclared-length',r=>{assert(!r.ok);assert.equal(r.lease,'');assert.equal(r.displayObserved,0);});
 await test('slow-body',r=>{assert(!r.ok);assert(r.elapsed>=1700&&r.elapsed<2700,'one absolute budget, never refreshed by packets');assert(r.retryOk,'same fixture process can perform the next current request after timeout');});
 for(const kind of ['cancel-headers','cancel-body','exit-body'])await test(kind,r=>{assert(!r.ok);assert(r.elapsed<500,'shutdown interrupts pending network work before the 1s exit join bound');assert.equal(r.lease,'');assert.equal(r.displayObserved,0);});
 for(const kind of ['missing-token','wrong-identity'])await test(kind,(r,n)=>{assert(!r.ok);assert.equal(n,0);assert.equal(r.lease,'');});
}catch(e){failure=e.stack??String(e);}
finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}
const result={ok:!failure,variant,cases:cases.length,results:cases,requests,failure,scope:'exact production Fan keepalive + shared AsyncRequest/fan_response headers, real loopback WinHTTP; process/credential/display/mode fixtures; no FanHost/HC/device/CPU benchmark'};
fs.writeFileSync(path.join(root,variant+'.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));process.exitCode=failure?1:0;
