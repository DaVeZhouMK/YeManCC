import {strict as assert} from 'node:assert';
import {WallpaperPlayers} from '../decky-plugin/src/wallpaperPlayers';
import {WallpaperForeground} from '../decky-plugin/src/wallpaperForeground';
import {randomGameMediaRange,applyGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK} from '../src/bridge/gameMediaPlayback';
let checks=0;const check=(ok:any,label:string)=>{checks++;assert.ok(ok,label)};
const a=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>0),b=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>.5),c=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>1);
check(a.start===8&&a.end===120&&a.initialStart===8,'whole video after intro is the playable loop');check(b.initialStart===63.5,'random start spans whole video rather than the first30sec');check(c.initialStart===119,'latest point is near actual end, not duration minus30sec');
for(const duration of [5,20,38,39,60,120,327.233333])for(const start of [0,8,15])for(const length of [30,60])for(const seed of [0,.1,.5,.9,1]){const range=randomGameMediaRange(duration,{start,end:start+length},()=>seed);check(range.initialStart>=range.start&&range.initialStart<range.end,'initial frame remains inside full playable media');check(range.end===duration,'segment setting never caps Steam playback');if(duration>start+1)check(range.start===start&&range.initialStart>=start,'intro is excluded both initially and on loop');}
check(randomGameMediaRange(0,DEFAULT_GAME_MEDIA_PLAYBACK).end===0,'unknown duration does not seek invalid media');check(randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>NaN).initialStart===8,'invalid random falls back after intro');
const chosen=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>.5);const video:any={readyState:2,duration:120,currentTime:120};applyGameMediaRange(video,chosen);check(video.currentTime===8,'whole-video loop returns after intro');video.currentTime=110;applyGameMediaRange(video,chosen);check(video.currentTime===110,'video can play beyond first30sec without rewinding');
// Execute the production player with inert media nodes. The Edge regression
// separately exercises real decoding and the Surface foreground event chain.
let focused=true,allowed=true,created=0,plays=0;const nodes:any[]=[];
const doc:any={visibilityState:'visible',hasFocus:()=>focused,defaultView:null,createElement:()=>{
 ++created;const listeners=new Map<string,Array<()=>void>>(),attributes=new Map<string,string>();
 const node:any={ownerDocument:doc,style:{},dataset:{},readyState:2,seeking:false,duration:120,currentTime:0,paused:true,parentNode:null,
 setAttribute:(k:string,v:string)=>attributes.set(k,v),removeAttribute:(k:string)=>attributes.delete(k),getAttribute:(k:string)=>attributes.get(k),
 addEventListener:(key:string,fn:()=>void)=>{const list=listeners.get(key)||[];list.push(fn);listeners.set(key,list);},fire:(key:string)=>{for(const fn of listeners.get(key)||[])fn();},
 pause:()=>{node.paused=true;},load:()=>{},remove:()=>{node.parentNode=null;},play:()=>{++plays;node.paused=false;return Promise.resolve();}};
 Object.defineProperty(node,'src',{get:()=>attributes.get('src'),set:(value:string)=>attributes.set('src',value)});nodes.push(node);return node;
}};
const target:any={setAttribute(){},removeAttribute(){},getAttribute(){return null;}},container:any={parentElement:target,appendChild:(node:any)=>node.parentNode=container};
const frame:any={contextSeq:1,appId:'570',entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'video',url:'http://fixture/video.mp4'}};
const pool=new WallpaperPlayers(doc,()=>{throw Error('background interruption is not an asset failure');},{allowed:()=>allowed});
focused=false;pool.show(container,frame);check(created===0&&plays===0,'unfocused page creates no video or source');
focused=true;doc.visibilityState='hidden';pool.show(container,frame);check(created===0,'hidden page creates no source');
doc.visibilityState='visible';allowed=false;pool.show(container,frame);check(created===0,'blur latch vetoes stale positive document focus');
allowed=true;pool.show(container,frame);check(created===1&&plays===1,'foreground creates the selected source');const old=nodes.at(-1);
allowed=false;old.fire('loadedmetadata');check(old.paused&&!old.src&&old.parentNode===null,'late metadata releases source in background');
const count=plays;for(const event of ['loadeddata','canplay','seeked','timeupdate'])old.fire(event);check(plays===count,'late readiness cannot restart background video');
allowed=true;pool.show(container,{...frame,contextSeq:2});check(created===2&&plays===count+1,'foreground recovery creates one source');const current=nodes.at(-1);current.fire('loadedmetadata');current.fire('canplay');
focused=false;current.fire('timeupdate');check(current.paused&&!current.src,'real focus loss vetoes playback boundary');
focused=true;pool.show(container,{...frame,contextSeq:3});pool.stop();check(nodes.every(n=>!n.src&&n.paused),'stop releases all sources');
const finalPlays=plays;for(const node of nodes)for(const event of ['loadeddata','canplay','seeked'])node.fire(event);await Promise.resolve();check(plays===finalPlays,'disposed events and play completion cannot revive video');

// The shared document owner survives route teardown without creating timers.
class TrackedEvents extends EventTarget {
 registrations=new Map<string,Set<any>>();
 addEventListener(name:string,listener:any,options?:any){const entries=this.registrations.get(name)||new Set();entries.add(listener);this.registrations.set(name,entries);super.addEventListener(name,listener,options);}
 removeEventListener(name:string,listener:any,options?:any){this.registrations.get(name)?.delete(listener);super.removeEventListener(name,listener,options);}
 count(){return [...this.registrations.values()].reduce((n,s)=>n+s.size,0);}
}
const win:any=new TrackedEvents();win.closed=false;
const pageDoc:any=new TrackedEvents();pageDoc.defaultView=win;pageDoc.visibilityState='visible';pageDoc.hasFocus=()=>true;
const foreground=new WallpaperForeground(pageDoc);let updates=0;let detach=foreground.subscribe(()=>updates++);
check(foreground.allowed(),'visible focused document is admitted');const registrations=win.count()+pageDoc.count();
detach();win.dispatchEvent(new Event('blur'));detach=foreground.subscribe(()=>updates++);check(!foreground.allowed(),'new route subscription preserves a blur recorded with no routes');
win.dispatchEvent(new Event('focus'));check(foreground.allowed(),'real window focus restores document admission');
pageDoc.dispatchEvent(new Event('freeze'));detach();detach=foreground.subscribe(()=>updates++);check(!foreground.allowed(),'remount cannot erase a document freeze');
win.dispatchEvent(new Event('focus'));check(!foreground.allowed(),'window focus does not thaw a frozen document');pageDoc.dispatchEvent(new Event('resume'));check(foreground.allowed(),'document resume restores the foreground route');
for(let i=0;i<100;i++){detach();detach=foreground.subscribe(()=>updates++);}check(win.count()+pageDoc.count()===registrations,'one event owner survives repeated route subscriptions');
pageDoc.visibilityState='hidden';pageDoc.dispatchEvent(new Event('visibilitychange'));check(!foreground.allowed(),'hidden document remains blocked with a focused snapshot');pageDoc.visibilityState='visible';pageDoc.dispatchEvent(new Event('visibilitychange'));check(foreground.allowed(),'visible foreground document recovers');
win.dispatchEvent(new Event('pagehide'));detach();win.dispatchEvent(new Event('pageshow'));check(foreground.allowed(),'page transition resumes even while no route is mounted');
win.dispatchEvent(new Event('blur'));foreground.revalidateFocus(new Event('focusin'));check(!foreground.allowed(),'synthetic controller focus cannot override background blur');
pageDoc.hasFocus=()=>false;foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted DOM selection without document focus stays paused');
pageDoc.hasFocus=()=>true;foreground.revalidateFocus({isTrusted:true} as Event);check(foreground.allowed(),'trusted focused selection recovers a missing window focus event');
win.dispatchEvent(new Event('blur'));pageDoc.visibilityState='hidden';foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted focus cannot activate a hidden document');
pageDoc.visibilityState='visible';pageDoc.dispatchEvent(new Event('freeze'));foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted focus cannot thaw a frozen document');pageDoc.dispatchEvent(new Event('resume'));foreground.revalidateFocus({isTrusted:true} as Event);check(foreground.allowed(),'trusted foreground selection recovers after document resume');
const last=updates;foreground.stop();foreground.stop();win.dispatchEvent(new Event('focus'));pageDoc.dispatchEvent(new Event('resume'));check(updates===last&&!foreground.allowed(),'stopped event owner cannot publish or authorize playback');check(win.count()+pageDoc.count()===0,'disposal unregisters all document and window callbacks');
console.log(JSON.stringify({ok:true,checks,scope:'production full-video range and foreground player guard with inert media events; no network or Steam'}));
