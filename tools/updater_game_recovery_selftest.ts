import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
let now=1e9,seq=0,receive:(e:any)=>void=()=>{},gate=false,activityFault=false;
const timers=new Map<number,{at:number;fn:()=>unknown}>(),calls:any[]=[],records:string[]=[];
const parts=APP_VERSION.split('.').map(Number),version=(n:number)=>`${parts[0]}.${parts[1]}.${parts[2]+n}`;
let manifest={version:version(1),sha256:'B'.repeat(64)},pending:any,native:any={phase:'idle',downloadActive:false};
const io={game:null as any,error:true,reads:0,generation:1,identityGeneration:1};
const saved={setTimeout,clearTimeout,now:Date.now,performance:globalThis.performance};
(globalThis as any).setTimeout=(fn:any,ms:number)=>{timers.set(++seq,{fn,at:now+ms});return seq;};(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);Date.now=()=>now;
Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>now}});Object.defineProperty(globalThis,'navigator',{configurable:true,value:{onLine:true}});
(globalThis as any).CustomEvent=class extends Event {detail:any;constructor(name:string,init:any){super(name);this.detail=init.detail;}};
const doc:any=new EventTarget();doc.visibilityState='visible';doc.createElement=(name:string)=>{assert.equal(name,'template','memory recovery test must not render UI');return {content:{}};};(globalThis as any).document=doc;
const win:any=new EventTarget();win.setTimeout=globalThis.setTimeout;win.clearTimeout=globalThis.clearTimeout;
const policy={enabled:true,bootId:'boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:0};
win.chrome={webview:{addEventListener(_name:string,cb:any){receive=cb;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 const ok=(result:any)=>receive({data:{id:req.id,result}}),fail=(error:string)=>receive({data:{id:req.id,error}});
 switch(req.cmd){
 case 'game.detect':io.reads++;if(io.error)fail('temporary game read failure');else ok({...io.game,pid:io.game?.pid??0,valid:!!io.game,generation:io.generation,identityGeneration:io.identityGeneration});break;
 case 'app.updatePredownloadActivity':gate=!!(req.args.known&&req.args.idle&&!activityFault);ok(!activityFault);break;
 case 'app.updatePredownloadRead':ok({...policy});break;
 case 'app.updatePredownloadSchedule':Object.assign(policy,req.args);ok(true);break;
 case 'app.updatePredownloadNotified':ok(true);break;
 case 'app.updatePredownloadTarget':ok(true);break;
 case 'app.updateState':ok({...native});break;
 case 'app.checkUpdate':if(req.args.background)assert.ok(gate&&!io.game);ok(manifest);break;
 case 'app.downloadUpdate':
  if(req.args.background)assert.ok(gate&&!io.game,'game excludes background download/cache verification');
  if(req.args.cacheOnly){fail('Verified cached update package is unavailable');break;}
  pending=req;native={...req.args,phase:'downloading',stage:'download',downloadActive:true,activeDownloadOperation:req.args.operationId};break;
 case 'app.cancelUpdateDownload':ok(true);if(pending?.args.background){const old=pending;pending=null;native={...native,phase:'interrupted',downloadActive:false,activeDownloadOperation:''};receive({data:{id:old.id,error:'Background download paused'}});}break;
 case 'app.retireUpdateDownload':ok({retired:true,active:!!pending,activeOperation:pending?.args.operationId||''});break;
 default:throw Error('UNEXPECTED_NATIVE '+req.cmd);
 }
 });}}};(globalThis as any).window=win;
const flush=async()=>{for(let i=0;i<150;i++)await Promise.resolve();};
async function advance(ms:number){now+=ms;for(let i=0;i<100;i++){const due=[...timers].filter(([,t])=>t.at<=now);if(!due.length)break;for(const[id,t]of due){timers.delete(id);void t.fn();}await flush();}}
const networkCalls=()=>calls.filter(c=>['app.checkUpdate','app.downloadUpdate'].includes(c.cmd)).length;
function observe(game:any){io.game=game;io.generation++;io.identityGeneration=io.generation;win.dispatchEvent(new CustomEvent('ipc:game.target.changed',{detail:{generation:io.generation,identityGeneration:io.identityGeneration}}));}
function check(label:string,fn:()=>void){try{fn();records.push(label);}catch(error){console.error(JSON.stringify({failed:label,now,io,gate,native,calls:calls.slice(-24).map(c=>({cmd:c.cmd,args:c.args})),timers:[...timers.values()].map(t=>t.at-now)}));throw error;}}
try{
 const {startUpdatePredownload}=await import('../src/bridge/updatePredownloadService');
 const m=await import('../src/bridge/updateManager');
 const {getPolicyGame,refreshPolicyGameStrict}=await import('../src/bridge/gamePolicyTarget');
 const service=startUpdatePredownload();await flush();await advance(15000);
 check('failed initial game observation blocks background work safely',()=>assert.equal(networkCalls(),0));
 io.error=false;await advance(5000);await advance(1000);
 check('successful observation self-heals initial unknown state without app restart',()=>{assert.ok(pending?.args.background);assert.equal(io.game,null);assert.equal(gate,true);});
 const game={pid:77,processCreated:'770001',name:'fixture game',path:'C:/fixture/game.exe'};
 observe(game);await flush();
 check('entry pauses active automatic download immediately',()=>{assert.equal(pending,null);assert.equal(gate,false);assert.equal(policy.failureCount,0);});
 const before=networkCalls();await advance(60000);
 check('gameplay blocks automatic manifest, package and cache verification requests',()=>assert.equal(networkCalls(),before));
 const manual=m.prepareUpdate();await flush();
 check('explicit manual download is admitted while a game runs',()=>{assert.ok(pending);assert.notEqual(pending.args.background,true);});
 const manualReq=pending;pending=null;native={...native,phase:'downloaded',downloadActive:false,activeDownloadOperation:''};receive({data:{id:manualReq.id,result:'fixture-package'}});assert.equal(await manual,true);
 const beforeExit=networkCalls();io.error=true;observe(null);await flush();await advance(9000);await assert.rejects(refreshPolicyGameStrict(),/temporary game read failure/);
 check('brief detector absence does not resume in the middle of gameplay',()=>{assert.equal(networkCalls(),beforeExit);assert.equal(getPolicyGame()?.pid,77);});
 io.error=true;await advance(1000);
 check('failed exit confirmation retains pause and remains retryable',()=>{assert.equal(networkCalls(),beforeExit);assert.equal(getPolicyGame()?.pid,77);});
 io.error=false;manifest={...manifest,version:version(3)};await advance(5000);await advance(1000);
 check('fresh confirmed exit resumes automatically and skips superseded package',()=>{assert.ok(pending?.args.background);assert.equal(pending.args.version,version(3));assert.equal(getPolicyGame(),null);});
 observe(game);await flush();observe(null);await flush();activityFault=true;await advance(10000);await advance(1000);
 check('temporary native admission failure does not authorize background work',()=>assert.equal(pending,null));
 activityFault=false;await advance(60000);await advance(1000);
 check('periodic recovery retries admission and avoids a permanent pause',()=>{assert.ok(pending?.args.background);assert.equal(gate,true);});
 service.stop();await flush();
 console.log(JSON.stringify({status:'PASS',checks:records.length,records,scope:'Actual updater service/manager/scheduler, shared game detector and policy facade. Only native snapshot IO, browser event targets and time are injected; no copied hysteresis, real game, network or installation.'}));
}finally{globalThis.setTimeout=saved.setTimeout;globalThis.clearTimeout=saved.clearTimeout;Date.now=saved.now;Object.defineProperty(globalThis,'performance',{configurable:true,value:saved.performance});}
