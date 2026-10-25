import {createServer} from 'node:http';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
const root=process.argv[2],run=promisify(execFile),rows=[],sockets=new Set();let requestCounts={};
const ready={ok:true,resumeAccepted:true,generation:1,state:{state:'Ready',powerState:'On',resumePhase:'Ready',resumePhaseGeneration:1,powerOperationGeneration:1,powerOperationStatus:'completed',powerOperationOutcome:'restored',openCalled:true,openEventsCalled:true,unknownState:false,hcCloseCleanupPending:false,controlAccepting:true},lease:{leaseId:'fixture-lease-0001'}};
const safe={ok:true,state:{state:'Stopped',hardwareWritesEnabled:false,hardwareWritesObserved:true,oemRestoreConfirmed:true,unknownState:false,hcCloseCleanupPending:false,openCalled:false,openEventsCalled:false,hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true}};
const server=createServer((req,res)=>{const kind=String(req.headers['x-yeman-fan-session']??'').replace(/^fixture-/,'');requestCounts[req.url]=(requestCounts[req.url]??0)+1;const denied=kind==='denied'||kind==='partial-denied';const status=denied?401:(kind==='legacy404'||kind==='partial404')&&req.url==='/api/suspend'?404:kind==='partial500'||kind==='error500'?500:200;const payload=denied?{ok:false,error:{code:'API_SESSION_REQUIRED'}}:req.url==='/api/close'||req.url==='/api/suspend'?{...safe,state:{...safe.state,state:req.url==='/api/suspend'?'Suspended':'Stopped'}}:ready;const b=JSON.stringify(payload);
 if(req.method==='GET'&&kind!=='get-partial'&&kind!=='partial-denied'&&kind!=='denied'&&kind!=='get-slow'&&kind!=='partial'&&kind!=='partial-chunked'){res.end(b);return;}
 if(kind==='partial'||kind==='get-partial'||kind==='post-partial'&&req.method==='POST'||kind==='partial500'||kind==='partial404'||kind==='partial-denied'){res.writeHead(status,{'Content-Length':Buffer.byteLength(b)+40});res.write(b);setTimeout(()=>res.destroy(),30).unref();return;}
 if(kind==='partial-chunked'){res.writeHead(status);res.write(b);setTimeout(()=>res.destroy(),30).unref();return;}
 if(kind==='chunked'){res.writeHead(status);res.write(b.slice(0,9));res.end(b.slice(9));return;}
 if(kind==='slow'||kind==='get-slow'){res.writeHead(status,{'Content-Length':200000});res.write(' ');const timer=setInterval(()=>{if(!res.destroyed)res.write(' ');},50);res.on('close',()=>clearInterval(timer));return;}
 if(kind==='malformed'){res.end('{');return;}
 if(kind==='empty'){res.writeHead(status,{'Content-Length':0});res.end();return;}
 if(kind==='legacy-eof'){res.useChunkedEncodingByDefault=false;res.shouldKeepAlive=false;res.writeHead(status,{'Connection':'close'});res.end(b);return;}
 if(kind==='oversized'){res.end(b+' '.repeat(256*1024));return;}
 res.writeHead(status);res.end(b);
});server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});await new Promise(r=>server.listen(0,'127.0.0.1',r));
async function test(kind,mode,check){requestCounts={};let j;try{const {stdout}=await run(path.join(root,'fixture.exe'),[String(server.address().port),mode,kind],{windowsHide:true,timeout:3300,maxBuffer:2<<20});j=JSON.parse(stdout);j.requests={...requestCounts};rows.push({kind,mode,...j});check(j);}catch(e){if(!j)rows.push({kind,mode,error:String(e),requests:{...requestCounts}});throw e;}}
let failures=[];async function attempt(name,body){try{await body();}catch(e){failures.push(name+': '+e.message);}}
try{
 await attempt('post-normal',()=>test('normal','post-any',r=>{assert.equal(r.ok,true);assert.equal(r.status,200);assert(r.bodyParsed);}));
 await attempt('get-normal',()=>test('normal','get',r=>{assert.equal(r.ok,true);assert(r.bodyParsed);}));
 await attempt('post-safe-normal',()=>test('normal','post-safe',r=>assert.equal(r.ok,true)));
 await attempt('post-truncated',()=>test('partial','post-any',r=>{assert.equal(r.ok,false,'truncated JSON accepted as successful POST');assert.equal(r.bodyBytes,0);assert.equal(r.requests['/api/resume'],1,'unknown POST was repeated');}));
 await attempt('safe-truncated',()=>test('partial','post-safe',r=>{assert.equal(r.ok,false,'truncated safe-release was accepted');assert.equal(r.bodyBytes,0);assert.equal(r.requests['/api/close'],1);}));
 await attempt('get-truncated',()=>test('get-partial','get',r=>{assert.equal(r.ok,false,'truncated GET was accepted');assert.equal(r.bodyBytes,0);}));
 await attempt('post-chunked',()=>test('chunked','post-any',r=>{assert.equal(r.ok,true);assert(r.bodyParsed);}));
 await attempt('get-chunked',()=>test('chunked','get',r=>assert.equal(r.ok,true)));
 await attempt('post-unterminated-chunked',()=>test('partial-chunked','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert.equal(r.requests['/api/resume'],1);}));
 await attempt('get-unterminated-chunked',()=>test('partial-chunked','get',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);}));
 await attempt('post-legacy-eof',()=>test('legacy-eof','post-any',r=>assert.equal(r.ok,true)));
 await attempt('get-legacy-eof',()=>test('legacy-eof','get',r=>assert.equal(r.ok,true)));
 await attempt('denied',()=>test('denied','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.status,401);assert(r.authRejected>=1);}));
 await attempt('denied-truncated',()=>test('partial-denied','get',r=>{assert.equal(r.ok,false);assert.equal(r.status,401);assert.equal(r.bodyBytes,0);assert.equal(r.authRejected,1);}));
 await attempt('wrong-process',()=>test('wrong-process','post-any',r=>{assert.equal(r.ok,false);assert.equal(Object.keys(r.requests).length,0);}));
 await attempt('missing-token',()=>test('missing-token','get',r=>{assert.equal(r.ok,false);assert.equal(Object.keys(r.requests).length,0);}));
 await attempt('malformed-safe',()=>test('malformed','post-safe',r=>assert.equal(r.ok,false)));
 await attempt('oversized',()=>test('oversized','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);}));
 await attempt('suspend-truncated-no-fallback',()=>test('partial','suspend',r=>{assert.equal(r.ok,false);assert.equal(r.requests['/api/close']??0,0);assert.equal(r.requests['/api/suspend'],1);}));
 await attempt('legacy404-fallback',()=>test('legacy404','suspend',r=>{assert.equal(r.ok,true);assert.equal(r.requests['/api/close'],1);}));
 await attempt('resume-truncated-then-valid-observation',()=>test('partial','resume',r=>{assert.equal(r.leaseArms,0);const terminal=r.lifecycle.find(x=>x.event==='fan-host-resume-native-trigger');assert.equal(terminal?.details.terminal,'exhausted');}));
 await attempt('resume-post-only-truncated-then-ready',()=>test('post-partial','resume',r=>{const terminal=r.lifecycle.find(x=>x.event==='fan-host-resume-native-trigger');assert.equal(terminal?.details.terminal,'recovered');}));
 await attempt('failed-observation-next-request-still-reachable',()=>test('partial','resume-retry',r=>{assert.equal(r.ok,true);const terminals=r.lifecycle.filter(x=>x.event==='fan-host-resume-native-trigger');assert.deepEqual(terminals.map(x=>x.details.terminal),['exhausted','recovered']);assert.equal(r.requests['/api/resume'],2);assert.equal(r.leaseArms,1);}));
 await attempt('unknown500-no-post-repeat',()=>test('partial500','resume',r=>{assert.equal(r.requests['/api/resume'],1);assert.equal(r.leaseArms,0);assert((r.requests['/api/state']??0)>0);assert.equal(r.lifecycle.find(x=>x.event==='fan-host-resume-native-trigger')?.details.terminal,'recovered');}));
 await attempt('partial404-no-legacy-close',()=>test('partial404','suspend',r=>{assert.equal(r.ok,false);assert.equal(r.requests['/api/close']??0,0);assert.equal(r.requests['/api/suspend'],1);}));
 await attempt('complete500-keeps-bounded-retry',()=>test('error500','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.requests['/api/resume'],2);}));
 await attempt('post-drip-deadline',()=>test('slow','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert.equal(r.requests['/api/resume'],1);assert(r.elapsedMs<3000);}));
 await attempt('get-drip-deadline',()=>test('get-slow','get',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert(r.elapsedMs<3000);}));
}finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}
const result={ok:failures.length===0,checks:rows.length,rows,failures,scope:'extracted actual native boundary POST/GET+safe classifier+Suspend+resume owner; actual WinHTTP; fixture identity/credential/log hooks; no FanHost/HC/device/CPU benchmark'};fs.writeFileSync(path.join(root,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failures},null,2));process.exitCode=result.ok?0:1;
