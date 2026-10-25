import{createApp,h,reactive,nextTick}from'vue';import Gyro from '@/components/GyroSimulationWindow.vue';import{state,pad}from'./ports';
// uiLifecycle initializes root-owned presentation listeners during module evaluation.
// Capture function identities before mounting the component; its own new listeners must all be removed.
state.sharedListeners=new Map([...state.listeners].map(([key,set]:any)=>[key,new Set(set)]));
let app:any=null,props:any;
const flush=async()=>{for(let i=0;i<4;i++){await Promise.resolve();await nextTick();}};
const choose=(selector:string)=>{const e=document.querySelector<HTMLElement>(selector);if(!e)throw Error('UI_ELEMENT_MISSING:'+selector);return e;};
const api={
 async mount(options:any={}){Object.assign(state,options);props=reactive({motion:{x:.15,y:-.1},connected:true,enabled:true,admitted:true});app=createApp({render:()=>h(Gyro,{...props,onClose:()=>state.close++,onSettingsHost:(e:any)=>state.settings.push(e!==null)})});app.mount('#mount');await flush();},
 async patch(p:any){Object.assign(props,p);await flush();},
 async click(selector:string){choose(selector).click();await flush();},
 async step(count=1,ms=25){for(let i=0;i<count;i++){state.clock+=ms;const pending=[...state.raf.values()];state.raf.clear();for(const fn of pending)fn(state.clock);await flush();}},
 async key(code:string,pressed:boolean){choose('canvas').dispatchEvent(new KeyboardEvent(pressed?'keydown':'keyup',{code,key:code==='Space'?' ':code,bubbles:true,cancelable:true}));await flush();},
 async browserBlur(){state.focus=false;window.dispatchEvent(new Event('blur'));await flush();},
 async blur(){state.focus=false;window.dispatchEvent(new Event('ipc:window.blur'));await flush();},
 async hidden(value=true){state.hidden=value;document.dispatchEvent(new Event('visibilitychange'));await flush();},
 async native(connected:boolean){state.pad={...pad(),connected};for(const fn of state.ipc)fn(state.pad);await flush();},
 async unmount(){app?.unmount();app=null;await flush();},
 async resolve(kind:string){for(const resolve of state[kind+'Pending'].splice(0))resolve(kind==='cap'?{vibrationAvailable:true}:kind==='pulse'?{ok:true}:true);await flush();},
 snapshot(){const viewport=document.querySelector<HTMLElement>('.gyro-scene-viewport'),box=document.querySelector('.gyro-simulation-window')?.getBoundingClientRect();return {playing:viewport?.dataset.playing??null,scene:viewport?.dataset.scene??null,body:document.body.innerText,readout:document.querySelector('.gyro-game-readout')?.textContent,modelStatus:document.querySelector('.gyro-model-loading')?.textContent,raf:state.raf.size,listeners:[...state.listeners].reduce((n:number,[key,set]:any)=>n+[...set].filter(fn=>!state.sharedListeners.get(key)?.has(fn)).length,0),sharedListeners:[...state.sharedListeners.values()].reduce((n:number,set:any)=>n+set.size,0),sharedListenersRetained:[...state.sharedListeners].every(([key,set]:any)=>[...set].every(fn=>state.listeners.get(key)?.has(fn))),ipc:state.ipc.size,widths:state.widths.size,widthCalls:structuredClone(state.widthCalls),calls:structuredClone(state.calls),capPending:state.capPending.length,pulsePending:state.pulsePending.length,widthPending:state.widthPending.length,providerPending:state.providerPending.length,providerCalls:state.providerCalls,providerDisposed:state.providerDisposed,gamepadReads:state.gamepadReads,renderCalls:state.renderCalls,settings:[...state.settings],box:box?{x:box.x,y:box.y,width:box.width,height:box.height}:null};}
};(window as any).__gyro=api;(window as any).__gyroReady=true;
