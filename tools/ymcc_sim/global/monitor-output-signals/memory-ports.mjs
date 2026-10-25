export const memoryInit=()=>{
 const q=new URLSearchParams(location.search),id=Number(q.get('hudId')??1),nonce=q.get('nonce')??'monitor-output-fixture-000001';
 const events=new Map(),saved=new Map(),calls=[],timers=new Map(),pending=[];let now=1700000000000,next=0;
 Date.now=()=>now;
 window.setTimeout=(fn,ms=0)=>{timers.set(++next,{fn,at:now+ms});return next;};window.clearTimeout=id=>timers.delete(id);
 const defaultPrefs={enabled:true,output:'ymcc',ymcc:{layout:'capsule',underlay:'none'}};
 window.__io={id,nonce,calls,pending,layoutMode:'true',settings:{rtss:{monitorOutput:defaultPrefs,sentinel:'keep'}},overlay:'Empty.ovl',backend:true,
 on(name,fn){const set=events.get(name)??new Set();set.add(fn);events.set(name,set);saved.set(name,fn);return()=>set.delete(fn);},
 emit(name,value){for(const fn of events.get(name)??[])fn(structuredClone(value));},late(name,value){saved.get(name)?.(structuredClone(value));},
 async invoke(command,args){calls.push({command,args:structuredClone(args)});if(command==='desktopHud.ready')return{id:q.get('ready')==='wrong'?999:id,width:1280,height:720,workArea:{left:0,top:0,width:1280,height:680}};
  if(command==='desktopHud.layout'){if(this.layoutMode==='defer')return new Promise(resolve=>pending.push(resolve));return this.layoutMode!=='false';}
  if(command==='desktopHud.close')return true;throw Error('UNREVIEWED_MEMORY_IPC:'+command);},
 async advance(ms){const end=now+ms;let budget=300;while(timers.size){const rows=[...timers].sort((a,b)=>a[1].at-b[1].at),[key,t]=rows[0];if(t.at>end)break;if(--budget<=0)throw Error('TIMER_BUDGET');now=t.at;timers.delete(key);t.fn();for(let i=0;i<10;i++)await Promise.resolve();}now=end;for(let i=0;i<10;i++)await Promise.resolve();},
 inspect(){return{now,calls:structuredClone(calls),timerCount:timers.size,timers:[...timers.values()].map(t=>({at:t.at,callback:String(t.fn).slice(0,200)})),subscribers:Object.fromEntries([...events].map(([k,v])=>[k,v.size])),pendingLayouts:pending.length};},
 };
 window.fetch=()=>{throw Error('NETWORK_DENIED');};
};
export const ipcMock=`export const isNativeRuntime=true;export const on=(name,fn)=>window.__io.on(name,fn);export const invoke=(name,args)=>window.__io.invoke(name,args);`;
export const portMocks={
 './settingsRepository':`export async function readSettingsSection(name){return structuredClone(window.__io.settings[name])}export async function saveSettingsSection(){throw Error('unexpected section write')}export async function updateSettings(fn){fn(window.__io.settings)}`,
 './deckySidebar':`export const deckySidebar={get(){throw Error('Steam/Decky access outside desktop scope')}}`,
 './steamSettings':`export const steamSettings={get(){throw Error('Steam settings access outside desktop scope')},applyMonitor(){throw Error('Steam apply outside desktop scope')}}`,
 './yeman':`export async function rtssRunning(){return window.__io.backend}export async function toggleRtss(on){window.__io.backend=on}export async function readOverlayLayout(){return window.__io.overlay}export async function setOverlayLayout(mode){window.__io.overlay='Empty.ovl';return {mode:'live'}}export async function readRtssZoom(){return 1}`,
};

