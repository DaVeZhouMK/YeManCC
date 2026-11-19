import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
const selectedName='fatal stop before the queued import microtask prevents its first dispatch';const pagehideName="pagehide retires a queued pending or retrying frontend update";const selections=new Set([selectedName,pagehideName]);let out=null,onlyCase=null,excludeCase=null;const args=process.argv.slice(2);for(let n=0;n<args.length;n++){const arg=args[n];if(arg==='--case'||arg==='--exclude-case'){assert(n+1<args.length,'FRONTEND_UPDATE_SELECTION_EMPTY');const value=args[++n];assert(value&&!value.startsWith('--'),'FRONTEND_UPDATE_SELECTION_EMPTY');if(arg==='--case'){assert(onlyCase===null,'FRONTEND_UPDATE_SELECTION_DUPLICATE');onlyCase=value;}else{assert(excludeCase===null,'FRONTEND_UPDATE_SELECTION_DUPLICATE');excludeCase=value;}}else{assert(arg&&!arg.startsWith('-')&&out===null,'FRONTEND_UPDATE_ARGUMENT_UNKNOWN');out=arg;}}assert(onlyCase===null||selections.has(onlyCase),'FRONTEND_UPDATE_CASE_UNKNOWN');assert(excludeCase===null||selections.has(excludeCase),'FRONTEND_UPDATE_EXCLUDE_UNKNOWN');assert(onlyCase===null||excludeCase===null,'FRONTEND_UPDATE_SELECTION_CONFLICT');
const root=path.resolve(import.meta.dirname,'..'),source=fs.readFileSync(path.join(root,'native/decky_sidebar_frontend_update.js'),'utf8');let cases=0,checks=0;const results=[],omitted=[],dispatchRetirementObservations=[],dispatchRetirementFacts=[];
const same=(a,b,label)=>{checks++;assert.deepEqual(a,b,label);};const settle=async()=>{for(let n=0;n<24;n++)await Promise.resolve();};
function fixture({loaded=true,fresh=false,mode='success'}={}){
 let now=0,next=0;const timers=new Map(),listeners=new Map(),calls=[];let action=mode;const id='a'.repeat(64),value={frontendBuildId:id,runId:'b'.repeat(32)};
 const host={__YMCC_DECKY_MIRROR__:value,setTimeout(fn,delay){assert.equal(this,host);timers.set(++next,{fn,due:now+delay,delay});return next;},clearTimeout(timer){assert.equal(this,host);timers.delete(timer);},addEventListener(type,fn){const set=listeners.get(type)||new Set();set.add(fn);listeners.set(type,set);},removeEventListener(type,fn){listeners.get(type)?.delete(fn);},dispatchEvent(event){for(const fn of [...(listeners.get(event.type)||[])])fn(event);}};
 const ready=(buildId=id)=>{host.__YMCC_DECKY_FRONTEND__={buildId,active:true};host.dispatchEvent({type:'ymcc-decky-frontend-ready'});};
 host.DeckyPluginLoader={plugins:loaded?[{name:'ymcc-sidebar',version:'0.0.1',loadType:1},{name:'another-plugin',version:'7'}]:[],importPlugin(...args){calls.push(args);if(action==='fail')return Promise.reject(Error('temporary backend loss'));if(action==='queued')return undefined;if(action==='wrong'){ready('e'.repeat(64));return Promise.resolve();}ready();return Promise.resolve();}};
 if(fresh)ready();const context=vm.createContext({window:host,Error});const update=vm.runInContext(source,context);const inject=(v=value)=>{host.__YMCC_DECKY_MIRROR__=v;return update(v);};
 const advance=async ms=>{const end=now+ms;for(;;){const row=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;now=row[1].due;timers.delete(row[0]);row[1].fn();await settle();}now=end;await settle();};
 return {host,value,id,inject,ready,timers,listeners,calls,advance,mode:value=>action=value,state:()=>host.__YMCC_DECKY_FRONTEND_UPDATE__};
}
const report=(ok,failure=null)=>({ok,cases:cases+(failure?1:0),checks,actualSteam:false,scope:'exact production frontend update script in isolated VM; mocked Decky lifecycle and clock, no live reload or UI mutation',onlyCase,excludeCase,results:failure?[...results,failure.name]:results,omitted,dispatchRetirementObservations,dispatchRetirementFacts,...(failure?{failure}:{})});const write=value=>{if(out)fs.writeFileSync(out,JSON.stringify(value,null,2),{flag:'wx'});};
async function run(name,fn){if(onlyCase!==null&&name!==onlyCase||name===excludeCase){omitted.push(name);return;}try{await fn();cases++;results.push(name);console.log('PASS '+name);}catch(error){write(report(false,{name,error:String(error.stack||error)}));throw error;}}
await run('a matching ready receipt completes an official import promise which never returns',async()=>{
 for(const receipt of ['ready-event','ready-marker-only']){
  const edge=fixture({mode:'queued'});edge.inject();
  if(receipt==='ready-event')edge.ready();else edge.host.__YMCC_DECKY_FRONTEND__={buildId:edge.id,active:true};
  await settle();
  console.log(JSON.stringify({boundary:'READY_BEFORE_DISPATCH',receipt,imports:edge.calls.length,phase:edge.state().phase,pending:edge.state().pending,timers:edge.timers.size}));
  same(edge.calls.length,0,'a live matching frontend before dispatch must not be imported again');
  same(edge.state().phase,'current','current receipt settles the original wait');same(edge.state().pending,false,'current receipt releases pending');same(edge.timers.size,0,'current receipt returns the original deadline');
  same(edge.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'current receipt returns ready listener');same(edge.listeners.get('ymcc-decky-health-stop')?.size||0,0,'current receipt returns stop listener');
  await edge.advance(60000);same(edge.calls.length,0,'confirmed frontend never queues a later import');same(edge.timers.size,0,'confirmed frontend has no polling');
 }

 const f=fixture({mode:'queued'});f.host.DeckyPluginLoader.importPlugin=(...args)=>{f.calls.push(args);f.ready();return new Promise(()=>{});};
 f.inject();await settle();console.log(JSON.stringify({hungImportReady:true,phase:f.state().phase,pending:f.state().pending,timers:f.timers.size,readyListeners:f.listeners.get('ymcc-decky-frontend-ready')?.size||0}));
 same(f.state().phase,'current','matching live receipt must retire a hung import wait');same(f.state().pending,false,'ready receipt releases pending');same(f.timers.size,0,'ready receipt releases deadline');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'ready listener released');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'stop listener released');
 await f.advance(60000);same(f.calls.length,1,'healthy module never imports again');same(f.timers.size,0,'healthy module has no polling');
});
await run('a late official import rejection cannot retry an already confirmed live module',async()=>{
 const f=fixture({mode:'queued'});let rejectImport;f.host.DeckyPluginLoader.importPlugin=(...args)=>{f.calls.push(args);f.ready();return new Promise((_,reject)=>{rejectImport=reject;});};
 f.inject();await settle();same(f.state().phase,'current','ready completion does not wait for import settlement');
 rejectImport(Error('late import failure'));await settle();await f.advance(60000);same(f.state().phase,'current','late rejection cannot overwrite actual ready');same(f.calls.length,1,'late rejection does not retry');same(f.timers.size,0,'late rejection adds no timers');
});
await run('an unresolved import without any receipt keeps the original bounded attempts',async()=>{
 const f=fixture({mode:'queued'});f.host.DeckyPluginLoader.importPlugin=(...args)=>{f.calls.push(args);return new Promise(()=>{});};
 f.inject();await settle();same(f.state().phase,'pending','unconfirmed import stays pending');await f.advance(60000);same(f.calls.length,3,'same three original attempts only');same(f.state().phase,'failed','unconfirmed import ends failed');same(f.state().pending,false,'exhaustion releases pending');same(f.timers.size,0,'exhaustion owns no timers');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'exhaustion removes ready listener');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'exhaustion removes stop listener');
});
await run('a wrong build receipt cannot release an unresolved official import',async()=>{
 const f=fixture({mode:'queued'});f.host.DeckyPluginLoader.importPlugin=(...args)=>{f.calls.push(args);f.ready('e'.repeat(64));return new Promise(()=>{});};
 f.inject();await settle();same(f.state().phase,'pending','wrong build remains unconfirmed');same(f.state().pending,true,'wrong build retains receipt wait');await f.advance(10000);same(f.state().phase,'failed','wrong build reaches original deadline');same([...f.timers.values()].map(x=>x.delay),[5000],'original first retry delay');f.state().cancel();same(f.timers.size,0,'explicit stop releases retry');
});
await run('fatal stop retires an unresolved official import before its late ready marker',async()=>{
 const f=fixture({mode:'queued'});let finish;f.host.DeckyPluginLoader.importPlugin=(...args)=>{f.calls.push(args);return new Promise(resolve=>{finish=()=>{f.ready();resolve();};});};
 f.inject();await settle();f.host.dispatchEvent({type:'ymcc-decky-health-stop'});await settle();same(f.state().phase,'canceled','unresolved import canceled');same(f.state().pending,false,'canceled import releases pending');same(f.timers.size,0,'canceled import returns deadline');
 finish();await settle();await f.advance(60000);same(f.state().phase,'canceled','late ready cannot undo cancellation');same(f.calls.length,1,'cancellation never replays import');same(f.timers.size,0,'late marker adds no timers');same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'cancellation returns ready listener');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'cancellation returns stop listener');
});
await run('fatal stop before the queued import microtask prevents its first dispatch',async()=>{
 const f=fixture({mode:'queued'});f.inject();f.host.dispatchEvent({type:'ymcc-decky-health-stop'});await settle();
 console.log(JSON.stringify({dispatchBoundary:'fatal-stop',imports:f.calls.length,timers:f.timers.size,phase:f.state().phase}));
 same(f.calls.length,0,'canceled grant must be rechecked before import microtask');same(f.state().phase,'canceled','stopped grant remains canceled');same(f.timers.size,0,'pre-dispatch stop returns deadline');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'pre-dispatch ready listener returned');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'pre-dispatch stop listener returned');
 const verify=async(name,fn)=>{try{await fn();dispatchRetirementObservations.push({name,status:'passed'});}catch(error){dispatchRetirementObservations.push({name,status:'failed',error:String(error.stack||error)});}};
 const clean=(f,phase)=>{same(f.state().phase,phase,'bounded updater phase');same(f.state().pending,false,'pending returned');same(f.timers.size,0,'all original timers returned');same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'ready listener returned');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'stop listener returned');};
 for(const point of ['first','retry'])for(const mutation of ['replaced','removed'])await verify(point+'-'+mutation,async()=>{
  const f=fixture({mode:point==='retry'?'fail':'queued'});f.inject();if(point==='retry'){await settle();same(f.calls.length,1,'one prior authorized import');same(f.timers.size,1,'original retry pending');}
  if(mutation==='removed')delete f.host.DeckyPluginLoader;else f.host.DeckyPluginLoader={plugins:[],importPlugin(){throw Error('replacement owner must not receive old grant');}};
  await settle();await f.advance(60000);same(f.calls.length,point==='retry'?1:0,'retired Loader receives no subsequent import');clean(f,'canceled');
  dispatchRetirementFacts.push({edge:point+'-'+mutation,imports:f.calls.length,phase:f.state().phase,timers:f.timers.size});
 });
 await verify('replacement-already-ready',async()=>{const f=fixture({mode:'queued'});f.inject();f.host.DeckyPluginLoader={plugins:[],importPlugin(){throw Error('ready owner needs no import');}};f.ready();await settle();await f.advance(60000);same(f.calls.length,0,'matching ready frontend wins without import');clean(f,'current');dispatchRetirementFacts.push({edge:'replacement-already-ready',imports:0,phase:f.state().phase,timers:f.timers.size});});
 await verify('same-owner-new-plugin-array',async()=>{const f=fixture();const loader=f.host.DeckyPluginLoader;f.inject();loader.plugins=loader.plugins.map(p=>({...p}));await settle();same(f.calls.length,1,'same public owner retains valid dispatch despite replaced plugin objects');clean(f,'current');dispatchRetirementFacts.push({edge:'same-owner-new-plugin-array',imports:1,phase:f.state().phase,timers:f.timers.size});});
 await verify('replacement-new-grant',async()=>{const f=fixture({mode:'queued'});f.inject();const retired=f.state();let newImports=0;f.host.DeckyPluginLoader={plugins:[{name:'ymcc-sidebar',version:'0.0.1',loadType:1}],importPlugin(){newImports++;f.ready();}};f.inject({...f.value,runId:'d'.repeat(32)});await settle();same(f.calls.length,0,'old owner not dispatched');same(newImports,1,'new current grant reaches new public owner');same(retired.phase,'canceled','old grant retired');clean(f,'current');dispatchRetirementFacts.push({edge:'replacement-new-grant',imports:0,newImports,phase:f.state().phase,timers:f.timers.size});});
 await verify('already-dispatched-receipt',async()=>{const f=fixture({mode:'queued'});f.inject();await settle();same(f.calls.length,1,'original authorized import already dispatched');f.host.DeckyPluginLoader={plugins:[]};f.ready();await settle();await f.advance(60000);same(f.calls.length,1,'receipt after replacement never replays import');clean(f,'current');dispatchRetirementFacts.push({edge:'already-dispatched-receipt',imports:1,phase:f.state().phase,timers:f.timers.size});});
 same(dispatchRetirementObservations.length,8,'exact retirement control count');assert(dispatchRetirementObservations.every(x=>x.status==='passed'),'retired Loader controls failed: '+dispatchRetirementObservations.filter(x=>x.status!=='passed').map(x=>x.name).join(','));

});
await run('account revocation before the queued import microtask prevents its first dispatch',async()=>{
 const f=fixture({mode:'queued'});f.inject();f.host.__YMCC_DECKY_MIRROR__={...f.value,revoked:true};await settle();
 same(f.calls.length,0,'revoked native grant cannot reach first import');same(f.state().phase,'canceled','revoked grant retires itself');same(f.timers.size,0,'revoked grant deadline returned');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'revoked ready listener returned');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'revoked stop listener returned');
});
await run('replacement native run before the import microtask dispatches only the new grant',async()=>{
 const f=fixture({mode:'queued'});f.inject();const retired=f.state();f.inject({...f.value,runId:'d'.repeat(32)});await settle();
 same(f.calls.length,1,'only the replacement run dispatches');same(retired.phase,'canceled','old run retired');same(f.timers.size,1,'only new ready deadline remains');
 f.ready();await settle();same(f.state().phase,'current','replacement run can complete');same(f.timers.size,0,'replacement completion returns deadline');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'healthy completion returns stop listener');
});
await run('a matching live frontend never reloads and has no timer',async()=>{const f=fixture({fresh:true});f.inject();await settle();same(f.calls.length,0,'fresh bundle remains mounted');same(f.timers.size,0,'no resident timer');});
await run('old or unmarked code reloads only the existing YMCC plugin through public lifecycle',async()=>{const f=fixture();f.inject();await settle();same(f.calls,[['ymcc-sidebar','0.0.1',1,true,10000]],'exact current plugin, load type and bounded timeout');same(f.state().phase,'current','receipt confirms loaded code');same(f.host.DeckyPluginLoader.plugins[1].name,'another-plugin','other plugins preserved');same(f.timers.size,0,'healthy update clears deadline');same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'ready listener removed');});
await run('bootstrap bursts and same-version app restarts do not repeatedly reload',async()=>{const f=fixture();for(let n=0;n<50;n++)f.inject();await settle();f.inject({...f.value,runId:'c'.repeat(32)});await settle();same(f.calls.length,1,'one actual version transition');same(f.timers.size,0,'idle app restart has no retry timer');same(f.state().phase,'current','healthy completed update remains current after binding refresh');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'healthy updater owns no stop listener');});
await run('disabled or absent plugins and malformed version hints are not enabled',async()=>{const f=fixture({loaded:false});f.inject();f.inject({...f.value,frontendBuildId:'malformed'});await settle();same(f.calls.length,0,'no implicit installation or enabling');same(f.timers.size,0,'absence is not polled');});
await run('queue admission is not proof of a completed reload',async()=>{const f=fixture({mode:'queued'});f.inject();await settle();same(f.state().phase,'pending','loader queue returns early but state remains pending');same(f.timers.size,1,'one bounded receipt deadline');f.ready();await settle();same(f.state().phase,'current','actual ready receipt completes update');same(f.timers.size,0,'deadline cleared');});
await run('a ready event from another version cannot complete this update',async()=>{const f=fixture({mode:'wrong'});f.inject();await settle();same(f.state().phase,'pending','wrong marker ignored');await f.advance(10000);same(f.state().phase,'failed','missing real version times out');same([...f.timers.values()].map(t=>t.delay),[5000],'only failed update owns retry');f.state().cancel();});
await run('backend outages have three bounded attempts and no idle polling after exhaustion',async()=>{const f=fixture({mode:'fail'});f.inject();await settle();same(f.calls.length,1,'first attempt');await f.advance(5000);same(f.calls.length,2,'second attempt');await f.advance(15000);same(f.calls.length,3,'third attempt');same(f.state().phase,'failed','failure remains explicit');same(f.timers.size,0,'outage retries are capped');});
await run('late successful module load cancels failed-update retry without another import',async()=>{const f=fixture({mode:'fail'});f.inject();await settle();f.ready();await f.advance(5000);same(f.calls.length,1,'matching later receipt makes retry unnecessary');same(f.state().phase,'current','actual code current');same(f.timers.size,0,'no remaining deadline');});
await run('a replacement run fences old pending completion and cancels its timer',async()=>{const f=fixture({mode:'queued'});f.inject();await settle();const previous=f.state();f.inject({...f.value,runId:'d'.repeat(32)});await settle();same(previous===f.state(),false,'new run has separate state');same(f.timers.size,1,'old deadline cancelled, only new deadline retained');f.ready();await settle();same(f.state().phase,'current','new run receipt completes');same(f.timers.size,0,'cleanup complete');});
await run('upstream #976: a fresh YMCC module refreshes only the same already-open stale panel',async()=>{
 const f=fixture({fresh:true}),loader=f.host.DeckyPluginLoader;const old={...loader.plugins[0]};let active=old,sets=0;
 loader.deckyState={publicState:()=>({activePlugin:active}),setActivePlugin:name=>{assert.equal(name,'ymcc-sidebar');active=loader.plugins[0];sets++;}};
 f.inject();await settle();same(active,loader.plugins[0],'active panel follows actual current plugin object');same(sets,1,'one public reselect');same(f.calls.length,0,'no import or UI restart');
 f.inject();await settle();same(sets,1,'stable panel has no repeat update');
 active={name:'another-plugin'};f.inject();await settle();same(sets,1,'another active plugin keeps its page');
});
await run('revoked offline account cannot dispatch a queued frontend import',async()=>{const f=fixture({mode:'queued'});f.inject();await settle();const count=f.calls.length;f.host.__YMCC_DECKY_MIRROR__={runId:f.value.runId,frontendBuildId:f.id,revoked:true};await f.advance(60000);same(f.calls.length,count,'old authenticated intent is not replayed after revocation');same(f.timers.size,0,'old recovery bounded and released');});
await run('fatal teardown cancels an already queued frontend update without another import',async()=>{
 const f=fixture({mode:'fail'});f.inject();await settle();same(f.calls.length,1,'first attempt was dispatched');same(f.timers.size,1,'one retry queued before teardown');
 f.host.dispatchEvent({type:'ymcc-decky-health-stop'});await settle();
 same(f.state().phase,'canceled','fatal stop retires the updater');same(f.state().pending,false,'canceled update is not pending');same(f.timers.size,0,'queued retry returned immediately');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'no ready listener after stop');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'stop listener returned');
 await f.advance(60000);f.inject();await settle();same(f.calls.length,1,'same old grant cannot resurrect the stopped import');same(f.timers.size,0,'no stopped polling');
});
await run('fatal teardown fences a pending update and its late ready receipt',async()=>{
 const f=fixture({mode:'queued'});f.inject();await settle();same(f.state().pending,true,'waiting for official ready receipt');same(f.timers.size,1,'receipt deadline owned');
 f.host.dispatchEvent({type:'ymcc-decky-health-stop'});await settle();
 same(f.state().phase,'canceled','pending update canceled');same(f.state().pending,false,'pending flag released');same(f.timers.size,0,'receipt deadline returned');
 same(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0,'ready callback removed');same(f.listeners.get('ymcc-decky-health-stop')?.size||0,0,'stop callback removed');
 f.ready();await f.advance(60000);same(f.state().phase,'canceled','late success cannot revive canceled state');same(f.calls.length,1,'late receipt never replays import');same(f.timers.size,0,'late receipt adds no work');
});
await run(pagehideName,async()=>{
 for(const edge of ['before-dispatch','waiting-receipt','retry-queued']){
  const f=fixture({mode:edge==='retry-queued'?'fail':'queued'}),loader=f.host.DeckyPluginLoader;
  let reselects=0;const old={...loader.plugins[0]};
  loader.deckyState={publicState:()=>({activePlugin:old}),setActivePlugin:()=>{reselects++;}};
  f.inject();if(edge!=='before-dispatch')await settle();
  const expected=edge==='before-dispatch'?0:1;
  same(f.calls.length,expected,edge+' initial imports');same(f.timers.size,1,edge+' initial deadline or retry');
  if(edge!=='retry-queued')same(f.state().pending,true,edge+' pending receipt');
  f.host.dispatchEvent({type:'pagehide'});await settle();
  const fact={edge,imports:f.calls.length,phase:f.state().phase,pending:f.state().pending,timers:f.timers.size,
    pagehideListeners:f.listeners.get('pagehide')?.size||0,stopListeners:f.listeners.get('ymcc-decky-health-stop')?.size||0,
    readyListeners:f.listeners.get('ymcc-decky-frontend-ready')?.size||0,reselects};
  console.log('PAGEHIDE_UPDATE '+JSON.stringify(fact));
  same(fact,{edge,imports:expected,phase:'canceled',pending:false,timers:0,pagehideListeners:0,stopListeners:0,readyListeners:0,reselects:0},edge+' retires only page-owned work');
  f.ready();await f.advance(60000);f.inject();await settle();
  same(f.calls.length,expected,edge+' late receipt and reinjection cannot import again');same(reselects,0,edge+' no late QAM reselect');
  same(f.state().phase,'canceled',edge+' old grant stays retired');same(f.timers.size,0,edge+' no retry survives');
 }
});
const result=report(true);write(result);
console.log(JSON.stringify(result));
