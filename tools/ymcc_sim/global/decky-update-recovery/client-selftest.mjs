import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
export async function clientTests(root,out){
 const req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),built=await build({entryPoints:[path.join(root,'decky-plugin/src/mirrorClient.ts')],bundle:true,write:false,platform:'node',format:'esm'}),{MirrorClient}=await import('data:text/javascript;base64,'+Buffer.from(built.outputFiles[0].contents).toString('base64'));
 let id=0,cases=0,now=0;const timers=new Map(),sockets=[],events=new Map(),reloads=[];
 const host={__YMCC_DECKY_FRONTEND__:{active:true,buildId:'1'.repeat(64)},__YMCC_DECKY_MIRROR__:{endpoint:'ws://127.0.0.1:1822/mirror',token:'a'.repeat(64),runId:'b'.repeat(32)},setTimeout(fn,delay){timers.set(++id,{fn,delay});return id;},clearTimeout:id=>timers.delete(id),addEventListener(k,f){if(!events.has(k))events.set(k,new Set());events.get(k).add(f);},removeEventListener(k,f){events.get(k)?.delete(f);},dispatchEvent(e){for(const fn of events.get(e.type)||[])fn(e);}};
 class Socket{handlers=new Map();sent=[];closed=false;constructor(url){this.url=url;sockets.push(this);}addEventListener(k,f){this.handlers.set(k,f);}send(s){this.sent.push(JSON.parse(s));}close(){this.closed=true;this.handlers.get('close')?.();}emit(k,value){this.handlers.get(k)?.({data:JSON.stringify(value)});}}
 const client=new MirrorClient({bootstrap:()=>host.__YMCC_DECKY_MIRROR__,socket:url=>new Socket(url),schedule:(fn,delay)=>host.setTimeout(fn,delay),cancel:host.clearTimeout,clientId:'fixture',now:()=>now});
 const snapshot=(runId,revision=1)=>({runId,generation:1,revision,ready:true,game:{label:'field-game',identity:'app:480:pid101:birthA',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'balanced'}]}}},fan:{supported:true,enabled:true,preset:'soft',choices:[{data:'soft',label:'soft'}]}});
 const flush=async()=>{for(let n=0;n<40;n++)await Promise.resolve();};
 client.setVisible(true);const old=sockets[0];old.emit('open');old.emit('message',{type:'snapshot',runId:host.__YMCC_DECKY_MIRROR__.runId,snapshot:snapshot(host.__YMCC_DECKY_MIRROR__.runId)});assert(client.snapshot().connected);cases++;
 const pending=client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);await flush();assert(old.sent.some(s=>s.command==='fan.setPreset'));cases++;
 const next={endpoint:'ws://127.0.0.1:21822/mirror',token:'c'.repeat(64),runId:'d'.repeat(32),frontendBuildId:'2'.repeat(64)};
 host.__YMCC_DECKY_MIRROR__=next;client.bootstrapChanged();assert(old.closed);assert.equal(sockets.length,2);assert.equal(sockets[1].url,next.endpoint+'?token='+next.token);assert(await pending instanceof Error);cases++;
 const fresh=sockets[1];fresh.emit('open');assert.deepEqual(fresh.sent.map(s=>s.command),['snapshot']);assert(!fresh.sent.some(s=>s.command==='fan.setPreset'));cases++;
 old.emit('message',{type:'snapshot',runId:'b'.repeat(32),snapshot:snapshot('b'.repeat(32),999)});assert.equal(client.snapshot().connected,false);cases++;
 fresh.emit('message',{type:'snapshot',runId:next.runId,snapshot:snapshot(next.runId)});assert(client.snapshot().connected);assert.equal(client.snapshot().snapshot.runId,next.runId);assert.equal(timers.size,0);cases++;
 host.DeckyPluginLoader={plugins:[{name:'ymcc-sidebar',version:'0.0.1',loadType:1}],importPlugin(...args){reloads.push(args);host.__YMCC_DECKY_FRONTEND__={active:true,buildId:next.frontendBuildId};host.dispatchEvent({type:'ymcc-decky-frontend-ready'});return Promise.resolve();}};
 const update=vm.runInNewContext(fs.readFileSync(path.join(root,'native/decky_sidebar_frontend_update.js'),'utf8'),{window:host,Error});update(next);await flush();assert.equal(reloads.length,1);assert.equal(host.__YMCC_DECKY_FRONTEND_UPDATE__.phase,'current');assert.equal(timers.size,0);cases++;
 update(next);await flush();assert.equal(reloads.length,1);assert.equal(timers.size,0);// Execute the complete actual native retained bootstrap against the production
 // MirrorClient. This rebind is transport only, with a real pending mutation.
 const source=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8');
 const line=source.split('\n').find(l=>l.includes('const std::string expression='));
 const cpp=line.slice(line.indexOf('expression=')+11).trim().replace(/;$/,'');
 const token=/"(?:\\.|[^"\\])*"|bootstrap\.dump\(\)|std::string\((?:frontendUpdateScript\(\)|healthScript\(\)|ymcc::deckyidentity::version)\)/g;
 const parts=cpp.match(token);assert.equal(parts.length,9,'actual complete native bootstrap builder contract');
 const version=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/decky-loader.lock.json'),'utf8')).version;
 const retained={endpoint:'ws://127.0.0.1:21823/mirror',token:'f'.repeat(64),runId:'e'.repeat(32),frontendBuildId:next.frontendBuildId};
 const expression=parts.map(p=>p[0]==='"'?JSON.parse(p):p==='bootstrap.dump()'?JSON.stringify(retained):p.includes('frontendUpdateScript')?fs.readFileSync(path.join(root,'native/decky_sidebar_frontend_update.js'),'utf8'):p.includes('healthScript')?fs.readFileSync(path.join(root,'native/decky_sidebar_health.js'),'utf8'):version).join('');
 const retainedLine=source.split('\n').find(l=>l.includes('const std::string retainedGate ='));
 const gate=retainedLine.slice(retainedLine.indexOf(' = ')+3).trim().replace(/;$/,'').replace('nlohmann::json(bootstrap.value("frontendBuildId",std::string{})).dump()','JSON.stringify(expected)').replace('nlohmann::json(std::string(ymcc::deckyidentity::version)).dump()','JSON.stringify(version)');
 const complete=vm.runInNewContext(gate,{expression,expected:retained.frontendBuildId,version});
 host.top=host;host.location={origin:'https://steamloopback.host'};host.document={querySelectorAll:()=>[]};host.MutationObserver=class{observe(){}disconnect(){}};host.DeckyPluginLoader.deckyState={publicState:()=>({versionInfo:{current:version}})};
 host.addEventListener('ymcc-decky-bootstrap',()=>client.bootstrapChanged());
 const oldPending=client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);await flush();
 assert.equal(vm.runInNewContext(complete,{window:host,location:host.location,Event:class{constructor(type){this.type=type;}},Error}),retained.runId);
 assert(fresh.closed);assert(await oldPending instanceof Error);assert.equal(sockets.length,3);assert.equal(reloads.length,1,'same retained module never invokes official reload');cases++;
 const rebound=sockets[2];rebound.emit('open');assert.deepEqual(rebound.sent.map(v=>v.command),['snapshot'],'unconfirmed writes must not cross the new broker');
 fresh.emit('message',{type:'snapshot',runId:next.runId,snapshot:snapshot(next.runId,999)});assert.equal(client.snapshot().connected,false);
 rebound.emit('message',{type:'snapshot',runId:retained.runId,snapshot:snapshot(retained.runId)});assert(client.snapshot().connected);assert.equal(client.snapshot().snapshot.runId,retained.runId);assert.equal(timers.size,0);host.__YMCC_DECKY_HEALTH__?.dispose();cases++;
 client.dispose();assert(rebound.closed);assert.equal(timers.size,0);cases++;
 fs.writeFileSync(path.join(out,'client-result.json'),JSON.stringify({passed:true,cases,newBootstrapReplacesDeadAddress:true,oldWritesReplayed:0,newVersionReceiptRequired:true,healthyPollingTimers:0,realSteam:false},null,2));return cases;
}
