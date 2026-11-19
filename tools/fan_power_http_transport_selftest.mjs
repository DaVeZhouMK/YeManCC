import {createServer} from 'node:http';import {execFile} from 'node:child_process';import {promisify} from 'node:util';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
const root=process.argv[2],run=promisify(execFile),rows=[],sockets=new Set();let requestCounts={},receivedFormalHeaders=[];
const requestLifetimeOnly=process.argv[3]==='--request-lifetime-only',lifetimeCase=requestLifetimeOnly?(process.argv[4]??null):null;
if(requestLifetimeOnly&&process.argv.length===5&&!process.argv[4])throw Error('empty exact Fan business case');
const lifetimeCases=['business-terminal','business-awaiting-old-superseded','business-unknown','business-other-request','business-running-global-ready','business-duplicate-key'];
const lifetimeSelection=lifetimeCase?lifetimeCase.split(','):null;
assert(!lifetimeSelection||requestLifetimeOnly&&lifetimeSelection.length>0&&lifetimeSelection.every(x=>lifetimeCases.includes(x))&&new Set(lifetimeSelection).size===lifetimeSelection.length,'exact F5 request lifetime selector');
const expectedLifetime=lifetimeSelection?lifetimeCases.filter(x=>lifetimeSelection.includes(x)):lifetimeCases;
let actualChecks=0;const verify=(condition,message)=>{++actualChecks;assert(condition,message);};
const sharedPowerOnly=process.argv[3]==='--shared-power-only',sharedOwnerOnly=process.argv[3]==='--shared-owner-only',resumeExistingOnly=process.argv[3]==='--resume-existing-only';if(process.argv.length>5||(process.argv[3]&&!sharedPowerOnly&&!sharedOwnerOnly&&!resumeExistingOnly&&!requestLifetimeOnly)||process.argv.length===5&&!requestLifetimeOnly&&!resumeExistingOnly)throw Error('Invalid explicit native power HTTP selection');
const ready={ok:true,resumeAccepted:true,generation:1,state:{state:'Ready',powerState:'On',resumePhase:'Ready',resumePhaseGeneration:1,powerOperationGeneration:1,powerOperationStatus:'completed',powerOperationOutcome:'restored',openCalled:true,openEventsCalled:true,unknownState:false,hcCloseCleanupPending:false,controlAccepting:true},lease:{leaseId:'fixture-lease-0001'}};
const safe={ok:true,state:{state:'Stopped',hardwareWritesEnabled:false,hardwareWritesObserved:true,oemRestoreConfirmed:true,unknownState:false,hcCloseCleanupPending:false,openCalled:false,openEventsCalled:false,hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true}};
const server=createServer((req,res)=>{const kind=String(req.headers['x-yeman-fan-session']??'').replace(/^fixture-/,'');const countPath=!kind.startsWith('business-')&&req.url.startsWith('/api/state?')?'/api/state':req.url;requestCounts[countPath]=(requestCounts[countPath]??0)+1;const formalHeader=req.headers["x-ymcc-shared-power"];if(formalHeader)receivedFormalHeaders.push(String(formalHeader));if(kind.startsWith("business-")&&req.method==="GET"){
 const parsed=new URL(req.url,"http://127.0.0.1"),low=parsed.searchParams.get("resumeRequestLow"),high=parsed.searchParams.get("resumeRequestHigh");
 if(!/^[0-9a-f]{16}$/.test(low??"")||!/^[0-9a-f]{16}$/.test(high??"")){res.writeHead(400);res.end("{}");return;}
 const awaiting=kind==="business-awaiting-old-superseded"&&!formalHeader;
 let business={schemaVersion:1,encoding:"native-f5-deadline-v1",requestLow:low,requestHigh:high,phase:awaiting?"AwaitingGrant":kind==="business-running-global-ready"?"Running":"Terminal",reason:awaiting?"public-admission-revoked":"restored",fanGeneration:"1",nativeGeneration:"1",accepted:true,currentAttempt:{id:"7",status:awaiting?"superseded":kind==="business-running-global-ready"?"executing":"completed",outcome:awaiting?"public-power-superseded":"restored"},grantBudgetUsed:null,hostGrantsObserved:formalHeader?2:1,grantPending:false};
 if(kind==="business-other-request")business.requestLow=((BigInt("0x"+low)+1n)&((1n<<64n)-1n)).toString(16).padStart(16,"0");
 if(kind==="business-unknown")business=null;
 const body={...ready,resumeBusiness:business,...(awaiting?{error:{code:"HC_RESUME_SUPERSEDED"},state:{...ready.state,powerOperationStatus:"superseded",powerOperationOutcome:"public-power-superseded"}}:{})};
 let encoded=JSON.stringify(body);if(kind==="business-duplicate-key")encoded=encoded.replace('"accepted":true','"accepted":false,"accepted":true');res.end(encoded);return;
}const denied=kind==='denied'||kind==='partial-denied';const status=denied?401:(kind==='legacy404'||kind==='partial404')&&req.url==='/api/suspend'?404:kind==='partial500'||kind==='error500'||kind==='f5-three-shared-slots'?500:200;const payload=denied?{ok:false,error:{code:'API_SESSION_REQUIRED'}}:req.url==='/api/close'||req.url==='/api/suspend'?{...safe,state:{...safe.state,state:req.url==='/api/suspend'?'Suspended':'Stopped'}}:ready;const progress=(kind.startsWith('shared-owner-')||kind==='f5-original-deadline-get'||kind==='f5-three-shared-slots'||kind==='formal-peer-replaced'||kind.startsWith('business-'))&&req.method==='POST'?{...ready,state:{...ready.state,state:'Resuming',resumePhase:'Rebuilding',powerOperationStatus:'pending',powerOperationOutcome:'pending',openCalled:false,openEventsCalled:false,controlAccepting:false},lease:null}:payload;const response=kind==='scheduler-new-generation'?{...progress,generation:2,state:{...progress.state,resumePhaseGeneration:2,powerOperationGeneration:2}}:progress;let wireResponse=response;
 if(req.method==='GET'&&req.url.startsWith('/api/state?')){
  const selected=new URL(req.url,'http://localhost');
  wireResponse={...response,resumeBusiness:{schemaVersion:1,encoding:'native-f5-deadline-v1',requestLow:selected.searchParams.get('resumeRequestLow'),requestHigh:selected.searchParams.get('resumeRequestHigh'),phase:'Terminal',reason:'restored',fanGeneration:'1',nativeGeneration:'1',accepted:true,currentAttempt:{id:'1',status:'completed',outcome:'restored'},grantBudgetUsed:null,hostGrantsObserved:1,grantPending:false}};
 }
 const b=JSON.stringify(wireResponse);
 if(req.method==='GET'&&kind!=='f5-original-deadline-get'&&kind!=='get-partial'&&kind!=='partial-denied'&&kind!=='denied'&&kind!=='get-slow'&&kind!=='partial'&&kind!=='partial-chunked'){res.end(b);return;}
 if(kind==='partial'||kind==='f5-ready-retains-unknown'&&req.method==='POST'||kind==='get-partial'||kind==='post-partial'&&req.method==='POST'||kind==='partial500'||kind==='partial404'||kind==='partial-denied'){res.writeHead(status,{'Content-Length':Buffer.byteLength(b)+40});res.write(b);setTimeout(()=>res.destroy(),30).unref();return;}
 if(kind==='partial-chunked'){res.writeHead(status);res.write(b);setTimeout(()=>res.destroy(),30).unref();return;}
 if(kind==='chunked'){res.writeHead(status);res.write(b.slice(0,9));res.end(b.slice(9));return;}
 if(kind==='f5-original-deadline-post'||kind==='f5-original-deadline-get'&&req.method==='GET'||kind==='slow'||kind==='get-slow'||kind==='shared-cancel-resume'){res.writeHead(status,{'Content-Length':200000});res.write(' ');const timer=setInterval(()=>{if(!res.destroyed)res.write(' ');},50);res.on('close',()=>clearInterval(timer));return;}
 if(kind==='malformed'){res.end('{');return;}
 if(kind==='empty'){res.writeHead(status,{'Content-Length':0});res.end();return;}
 if(kind==='legacy-eof'){res.useChunkedEncodingByDefault=false;res.shouldKeepAlive=false;res.writeHead(status,{'Connection':'close'});res.end(b);return;}
 if(kind==='oversized'){res.end(b+' '.repeat(256*1024));return;}
 res.writeHead(status);res.end(b);
});server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});await new Promise(r=>server.listen(0,'127.0.0.1',r));
async function test(kind,mode,check){requestCounts={};receivedFormalHeaders=[];let j;const saveRaw=({stdout,stderr})=>{if(requestLifetimeOnly){fs.writeFileSync(path.join(root,'native-'+mode+'-'+kind+'.stdout.raw'),stdout??'',{flag:'wx'});fs.writeFileSync(path.join(root,'native-'+mode+'-'+kind+'.stderr.raw'),stderr??'',{flag:'wx'});}};try{const output=await run(path.join(root,'fixture.exe'),[String(server.address().port),mode,kind],{windowsHide:true,timeout:3300,maxBuffer:2<<20}).catch(e=>{saveRaw(e);throw e;});saveRaw(output);j=JSON.parse(output.stdout);j.requests={...requestCounts};j.receivedFormalHeaders=[...receivedFormalHeaders];rows.push({kind,mode,...j});check(j);}catch(e){if(!j)rows.push({kind,mode,error:String(e),childExitCode:e.code??null,signal:e.signal??null,killed:e.killed??false,requests:{...requestCounts}});throw e;}}
const existingResumeCase=resumeExistingOnly?(process.argv[4]??null):null;
const existingResumeNames=new Set(['resume-truncated-then-valid-observation','resume-post-only-truncated-then-ready','failed-observation-next-request-still-reachable','unknown500-no-post-repeat']);
assert(!existingResumeCase||existingResumeNames.has(existingResumeCase),'exact existing resume selector');
let failures=[],lifetimeStopped=false;async function attempt(name,body){if(requestLifetimeOnly&&(lifetimeStopped||!expectedLifetime.includes(name)))return;if(resumeExistingOnly&&(!existingResumeNames.has(name)||(existingResumeCase&&name!==existingResumeCase)))return;try{await body();}catch(e){failures.push(name+': '+e.message);if(requestLifetimeOnly)lifetimeStopped=true;}}
try{
 if(requestLifetimeOnly){
 const terminal=r=>r.lifecycle.filter(x=>x.event==='fan-host-resume-native-trigger').at(-1)?.details.terminal;
 const sameRequest=r=>{verify(r.requestViews.every(x=>typeof x.keyLow==='string'&&typeof x.keyHigh==='string'&&x.keyLow===((1n<<63n)|BigInt(x.request)).toString()&&x.keyHigh===String(x.deadline)),'full uint64 Request key must survive exact string capture');return r.requestViews.every(x=>x.request===r.requestViews[0].request&&x.deadline===r.requestViews[0].deadline&&x.started===r.requestViews[0].started&&x.keyLow===r.requestViews[0].keyLow&&x.keyHigh===r.requestViews[0].keyHigh);};
 await attempt('f5-original-deadline-post',()=>test('f5-original-deadline-post','f5-budget-post',r=>{verify(r.elapsedMs<800,'original first deadline must bound actual POST');verify(r.requests['/api/resume']===1&&!(r.requests['/api/state']??0),'expired POST is not replayed or followed by a new-budget GET');verify(terminal(r)==='exhausted'&&r.leaseArms===0,'partial slow response never recovers');}));
 await attempt('f5-original-deadline-get',()=>test('f5-original-deadline-get','f5-budget-get',r=>{verify(r.elapsedMs<800,'original first deadline must bound actual GET');verify(r.requests['/api/resume']===1&&r.requests['/api/state']===1,'GET spends same first deadline');verify(terminal(r)==='exhausted'&&r.leaseArms===0,'slow GET never becomes success');}));
 await attempt('f5-queue-expired',()=>test('f5-queue-expired','f5-queue-expired',r=>{verify(Object.keys(r.requests).length===0,'expired original queued ticket cannot send');verify(!r.reopened&&r.elapsedMs<800,'Ready cannot refund expiry after queue wait');}));
 await attempt('f5-ready-retains-unknown',()=>test('f5-ready-retains-unknown','f5-ready-unknown',r=>{verify(r.requests['/api/resume']===1&&r.requests['/api/state']===1,'unknown original POST never repeats on Ready');verify(r.requestViews.length===2&&sameRequest(r),'fresh Native observation preserves complete original request and deadline');verify(r.requestViews[1].attempts===1&&!r.requestViews[1].mayPost,'actual F5 successor remains POST closed');verify(terminal(r)==='recovered'&&r.unexpectedDrains===0,'ordinary read-only successor uses the original classifier');}));
 await attempt('f5-three-shared-slots',()=>test('f5-three-shared-slots','f5-three-slots',r=>{verify(r.requests['/api/resume']===3&&r.requests['/api/state']===1,'fourth fresh observation does not send fourth POST');verify(r.requestViews.length===4&&sameRequest(r),'three retries and final observation retain one first business');verify(JSON.stringify(r.lifecycle.filter(x=>x.event==='fan-host-resume-attempt').map(x=>x.details.attempt))==='[1,2,3]','actual transport consumes original sequential three slots');verify(terminal(r)==='recovered'&&r.unexpectedDrains===0,'no extra pending worker or false failure introduced');}));
 await attempt('f5-terminal-tombstone',()=>test('f5-terminal-tombstone','f5-terminal',r=>{verify(r.requests['/api/resume']===1&&!r.reopened,'terminal original business refuses duplicate automatic POST');verify(terminal(r)==='recovered'&&r.requestViews.length===1,'actual terminal stays one result');}));

 for(const name of lifetimeCases.filter(x=>x.startsWith('scheduler-')||x.startsWith('manual-')))await attempt(name,()=>test(name,'scheduler',r=>{
  verify(r.returned===r.created&&r.gateClosed,'all real scheduled threads and gate returned');
  const posts=r.requests['/api/resume']??0;
  const same=r.tickets.every(t=>t.request===r.tickets[0].request&&t.deadline===r.tickets[0].deadline&&t.started===r.tickets[0].started&&t.low===r.tickets[0].low&&t.high===r.tickets[0].high);
  if(['scheduler-expired-queue','scheduler-exit-queued','scheduler-off-queued','scheduler-off-on-queued','manual-rejections'].includes(name))verify(posts===0,'retired queued work must not send POST');else verify(posts===((name==='manual-entry'||name==='scheduler-intent-during-post')?2:1),'only original eligible business sends expected POST count');
  if(name==='scheduler-ready-queued'||name==='manual-commit-ready')verify(r.created===2&&r.tickets.length===2&&same,'same-business queued Ready must drain once and retain first deadline/key');
  if(name==='scheduler-publication-duplicate')verify(r.created===1&&r.tickets.length===1,'publication-only copy cannot schedule another worker');
  if(name==='scheduler-new-generation')verify(r.created===2&&r.tickets.length===2&&r.tickets[0].request!==r.tickets[1].request&&r.tickets[1].generation===2,'new Native/Fan generation drains its own retained request');
  if(name==='scheduler-create-failure'||name==='scheduler-create-failure-pending')verify(r.constructed===2&&r.created===1&&r.returned===1&&r.tickets[0].request!==r.tickets[1].request&&r.lifecycle.some(x=>x.event==='fan-host-resume-thread-failed'),'failed creation releases old owner and preserves newer manual business');
  if(name==='manual-entry')verify(r.manualToken===2&&r.replies.every(x=>x.admitted)&&r.tickets.length===2&&r.tickets[0].request!==r.tickets[1].request,'only genuine manual IPC calls mint two separate internal operations');
  if(name==='manual-rejections')verify(r.manualToken===0&&r.created===0&&r.replies.length===3&&r.replies.every(x=>!x.admitted),'stale/Suspending/exit refuse before any manual token or worker');
  if(name==='scheduler-create-failure-log-pending')verify(r.constructed===2&&r.created===1&&r.returned===1&&r.tickets.length===2&&r.tickets[0].request!==r.tickets[1].request,'diagnostic throw cannot orphan pending original request');
  if(name==='scheduler-intent-during-post')verify(r.created===2&&r.returned===2&&r.leaseArms===1&&r.tickets.length===2&&r.tickets[0].request!==r.tickets[1].request,'retired HTTP receipt cannot arm lease and new intent recovers with its own business');
  if(name==='manual-commit-ready')verify(r.manualToken===1&&r.commits===1&&r.replies[0].admitted,'manual Resuming->Ready continuation never mints another manual operation');
 }));
 for(const name of ['formal-first-post','formal-host-denied'])await attempt(name,()=>test(name,'formal',r=>{
  verify(r.created===1&&r.returned===1&&r.gateClosed,'actual original scheduler thread returned');
  if(name==='formal-host-denied'){verify((r.requests['/api/resume']??0)===0&&r.captures.length===0,'denied original held Host emits no formal offer or POST');return;}
  verify(r.requests['/api/resume']===1&&r.captures.length===1&&r.receivedFormalHeaders.length===1,'one original slot creates one actual POST and formal header');
  const capture=r.captures[0],ticket=r.tickets[0];verify(capture.header===r.receivedFormalHeaders[0],'actual wire contains the original encoded offer');
  verify(capture.requestLow===ticket.low&&capture.requestHigh===ticket.high&&capture.nativeGeneration===1&&capture.providerPid===capture.consumerPid,'descriptor retains exact Native Request and held self-consumer test identity');
  verify(r.currentAfterHttp&&r.currentAfterDeadline&&!r.currentAfterOff,'HTTP completion and fifteen-second clock never cancel healthy long permit; actual OFF does');
  verify(r.settlements.length===1&&r.settlements[0].before===1&&r.settlements[0].accepted&&r.settlements[0].after===0,'only explicit no-work fixture settlement retires original scope; no synthetic HTTP return');
 }));
 await attempt('formal-peer-replaced',()=>test('formal-peer-replaced','peer',r=>{
  verify(r.childSignaled&&r.childExited&&r.childExitCode===0&&r.childHandleClosed&&r.eventClosed&&r.gateClosed,'actual distinct dormant peer naturally returned and its owned handles closed');
  verify(r.created===r.returned&&r.created===3,'original queued workers returned for old/rejected/new-manual businesses');
  verify(r.lifecycle.filter(x=>x.event==="fan-host-resume-native-trigger"&&x.details.terminal==="recovered").length===1,"replacement peer cannot complete original business from global Ready");verify(r.checkpoints.length===3&&r.checkpoints[0].captures===1&&r.checkpoints[1].captures===1&&r.checkpoints[2].captures===2,'same original Request cannot recapture replacement Host');
  verify(r.receivedFormalHeaders.length===2&&r.captures.length===2,'only initial original and explicitly new manual business send formal POST');
  const first=r.captures[0],last=r.captures[1];verify(first.consumerPid===r.providerPid&&last.consumerPid===r.childPid&&first.requestLow!==last.requestLow&&r.manualToken===1,'different real process identities require different Native business');
  verify(r.settlements.length===2&&r.settlements.every(x=>x.accepted)&&r.settlements.at(-1).remaining===0,'empty fixture returns each exact consumer scope without borrowing another process ACK');
 }));
 for(const name of ["business-terminal","business-awaiting-old-superseded","business-unknown","business-other-request","business-running-global-ready","business-duplicate-key"])await attempt(name,()=>test(name,'business',r=>{
  verify(r.created===r.returned&&r.gateClosed,'all original business workers returned');
  const gets=Object.keys(r.requests).filter(x=>x.startsWith('/api/state?'));verify(gets.length===1,'all observations select one complete original Request');
  const selected=new URL(gets[0],'http://localhost'),first=r.captures[0];verify(selected.searchParams.get('resumeRequestLow')===BigInt(first.requestLow).toString(16).padStart(16,'0')&&selected.searchParams.get('resumeRequestHigh')===BigInt(first.requestHigh).toString(16).padStart(16,'0'),'GET selector is exact full128 hex without Number conversion');
  const terminals=r.lifecycle.filter(x=>x.event==='fan-host-resume-native-trigger').map(x=>x.details.terminal);
  verify((r.requests['/api/resume']??0)===1,'exact query never replays original POST');
  if(name==='business-terminal'){verify(terminals.at(-1)==='recovered'&&r.leaseArms===1&&r.captures.length===1,'only exact completed business adopts lease');}
  else if(name==='business-awaiting-old-superseded'){verify(r.replies.length===2&&!r.replies[0].admitted&&r.replies[1].admitted&&r.replies[1].resumeScheduled&&r.manualToken===0,'actual observed wake IPC rejects pending power and continues without minting a manual operation');verify(terminals[0]==='superseded'&&terminals.at(-1)==='recovered'&&r.leaseArms===1&&r.captures.length===2,'exact live business survives old attempt superseded and receives one new parent');const next=r.captures[1];verify(next.requestLow===first.requestLow&&next.requestHigh===first.requestHigh&&next.scopeLow!==first.scopeLow&&r.lifecycle.at(-1).details.attempts===2,'new parent keeps original request and consumes second original slot');const [oldWorker,newWorker]=r.tickets;verify(r.tickets.length===2&&oldWorker.request===newWorker.request&&oldWorker.started===newWorker.started&&oldWorker.deadline===newWorker.deadline&&newWorker.attempts===1,'same request retains first deadline and consumed attempt');verify(r.requests[gets[0]]===2&&r.receivedFormalHeaders.length===2,'read-only query precedes formal same-business parent delivery');}
  else {verify(!terminals.includes('recovered')&&r.leaseArms===0&&r.captures.length===1,'missing/mismatched/running/duplicate business cannot borrow global Ready or mint a scope');verify(r.clockAdvancedAfterObservationSleeps===1,'original deadline advanced only after one complete observation decision');}
  verify(r.fixtureClaims===r.captures.length&&r.settlements.length===r.fixtureClaims&&r.settlements.every(x=>x.accepted)&&r.settlements.at(-1).remaining===0,'fixture claims and explicit scope returns remain separate from completion');
 }));
 }else{

 if(!sharedPowerOnly&&!sharedOwnerOnly){
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
 await attempt('unknown500-no-post-repeat',()=>test('partial500','resume',r=>{assert.equal(r.requests['/api/resume'],1);assert.equal(r.leaseArms,1);assert((r.requests['/api/state']??0)>0);assert.equal(r.lifecycle.find(x=>x.event==='fan-host-resume-native-trigger')?.details.terminal,'recovered');}));
 await attempt('partial404-no-legacy-close',()=>test('partial404','suspend',r=>{assert.equal(r.ok,false);assert.equal(r.requests['/api/close']??0,0);assert.equal(r.requests['/api/suspend'],1);}));
 await attempt('complete500-keeps-bounded-retry',()=>test('error500','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.requests['/api/resume'],2);}));
 await attempt('post-drip-deadline',()=>test('slow','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert.equal(r.requests['/api/resume'],1);assert(r.elapsedMs<3000);}));
 await attempt('get-drip-deadline',()=>test('get-slow','get',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert(r.elapsedMs<3000);}));
 }
 if(!sharedOwnerOnly){
 for(const kind of ['shared-unknown-resume','shared-pending-resume'])await attempt(kind,()=>test(kind,'post-any',r=>{assert.equal(r.ok,false,'unadmitted shared power reached resume POST');assert.equal(r.requests['/api/resume']??0,0);assert.equal(r.bodyBytes,0);}));
 await attempt('shared-publication-resume',()=>test('shared-publication-resume','post-any',r=>{assert.equal(r.ok,true);assert.equal(r.requests['/api/resume'],1);}));
 await attempt('shared-unknown-get',()=>test('shared-unknown-get','get',r=>assert.equal(r.ok,true)));
 await attempt('shared-unknown-close',()=>test('shared-unknown-close','post-safe',r=>assert.equal(r.ok,true)));
 await attempt('shared-cancel-resume',()=>test('shared-cancel-resume','post-any',r=>{assert.equal(r.ok,false);assert.equal(r.bodyBytes,0);assert(r.elapsedMs<500,'public cancellation did not interrupt resume observer');assert.equal(r.requests['/api/resume'],1,'unknown resume must not repeat');}));
 }
 if(!sharedPowerOnly){
 for(const kind of ['shared-owner-revoke','shared-owner-pending','shared-owner-serial'])await attempt(kind,()=>test(kind,'resume',r=>{assert.equal(r.lifecycle.some(x=>x.event==='fan-host-resume-native-trigger'&&x.details.terminal==='recovered'),false,'retired shared observation promoted native recovery');assert.equal(r.leaseArms,0);assert.equal(r.requests['/api/resume'],1);}));
 await attempt('shared-owner-publication',()=>test('shared-owner-publication','resume',r=>{assert.equal(r.ok,true);assert.equal(r.requests['/api/resume'],1);}));
 await attempt('shared-owner-retry',()=>test('shared-owner-retry','resume-retry',r=>{assert.equal(r.ok,true);assert.deepEqual(r.lifecycle.filter(x=>x.event==='fan-host-resume-native-trigger').map(x=>x.details.terminal),['superseded','recovered']);assert.equal(r.requests['/api/resume'],2);assert.equal(r.leaseArms,1);}));
 }
}
}finally{for(const s of sockets)s.destroy();await new Promise(r=>server.close(r));}
const result={ok:failures.length===0,selection:requestLifetimeOnly?'request-lifetime-only':sharedPowerOnly?'shared-power-only':sharedOwnerOnly?'shared-owner-only':resumeExistingOnly?'resume-existing-only':'all',checks:rows.length,assertions:actualChecks,expectedCases:requestLifetimeOnly?expectedLifetime:null,actualCases:requestLifetimeOnly?rows.map(x=>x.kind):null,rows,failures,scope:'extracted actual native boundary POST/GET+safe classifier+Suspend+resume owner; actual WinHTTP; fixture identity/credential/log hooks; no FanHost/HC/device/CPU benchmark'};fs.writeFileSync(path.join(root,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failures},null,2));process.exitCode=result.ok?0:1;
