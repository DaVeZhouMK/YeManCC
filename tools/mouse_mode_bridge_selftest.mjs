import assert from 'node:assert/strict';
import {verifyMouseStatusHint} from './mouse_status_hint_selftest.mjs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
export async function verifyMouseBridge(root) {
 const {build}=createRequire(path.join(root,'package.json'))('esbuild');
 const bundle=await build({stdin:{contents:"import {closeJoyxoffIfRunning,setMouseModeEnabled,setMouseModePolicy,toggleMouseMode} from './src/bridge/gameproc';globalThis.__bridge={closeJoyxoffIfRunning,setMouseModeEnabled,setMouseModePolicy,toggleMouseMode};",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',plugins:[{name:'isolated-native-boundary',setup(builder){
  builder.onResolve({filter:/^\.\/(?:api|ipc)$/},args=>({path:args.path,namespace:'memory-native'}));
  builder.onLoad({filter:/.*/,namespace:'memory-native'},args=>({loader:'js',contents:args.path==='./ipc'?"export const invoke=(command,args)=>globalThis.__invoke(command,args);":"const reject=()=>{throw Error('Legacy system/shell route reached')};export const fs=new Proxy({},{get:()=>reject}),proc=fs,shell=fs;"}));
 }}]});
 const calls=[];let receipt={ok:true,closed:true,stateKnown:true};
 const context=vm.createContext({__invoke:async(command,args)=>{calls.push({command,args});return receipt;}});vm.runInContext(bundle.outputFiles[0].text,context);
 const bridge=context.__bridge;
 assert.equal(await bridge.closeJoyxoffIfRunning(),true);assert.equal(calls.at(-1).command,'mouseMode.releaseJoyxoff');
 receipt={ok:true,closed:false,stateKnown:true};assert.equal(await bridge.closeJoyxoffIfRunning(),false);
 receipt={ok:false,closed:false,stateKnown:true,error:'close failed'};await assert.rejects(bridge.closeJoyxoffIfRunning(),/close failed/);
 receipt={ok:true,closed:true,stateKnown:false};await assert.rejects(bridge.closeJoyxoffIfRunning(),/关闭未确认/);
 receipt={ok:true,on:true,stateKnown:true};await bridge.setMouseModeEnabled(true);assert.equal(calls.at(-1).command,'mouseMode.set');assert.equal(calls.at(-1).args.on,true);
 await bridge.setMouseModeEnabled(false);assert.equal(calls.at(-1).args.on,false);
 await bridge.toggleMouseMode();assert.equal(calls.at(-1).command,'mouseMode.toggle');
 for(const mode of ['auto','on','off']){await bridge.setMouseModePolicy(mode);assert.equal(calls.at(-1).command,'mouseMode.setPolicy');assert.equal(calls.at(-1).args.mode,mode);}
 const real=await build({stdin:{contents:"import {setMouseBackend} from './src/bridge/gameproc';import {steamDeckMouseGet,steamDeckMouseSet} from './src/bridge/steamDeckMouse';globalThis.__timed={setMouseBackend,steamDeckMouseGet,steamDeckMouseSet};",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife'});
 let message,posted,timerId=0;const timers=new Map();
 const timedContext=vm.createContext({console,window:{chrome:{webview:{postMessage(value){posted=value;},addEventListener(name,fn){message=fn;}}}},setTimeout(fn,ms){const id=++timerId;timers.set(id,{fn,ms});return id;},clearTimeout(id){timers.delete(id);}});
 vm.runInContext(real.outputFiles[0].text,timedContext);
 for(const call of [()=>timedContext.__timed.setMouseBackend('gamebar'),()=>timedContext.__timed.steamDeckMouseGet(),()=>timedContext.__timed.steamDeckMouseSet(100)]){
  const pending=call(),expired=posted.id;assert.equal(timers.size,1);const [id,timer]=[...timers][0];assert.equal(timer.ms,30000);timers.delete(id);timer.fn();
  await assert.rejects(pending,/timed out/);message({data:{id:expired,result:{ok:true,backend:'steam'}}});assert.equal(timers.size,0);
 }
 const retry=timedContext.__timed.setMouseBackend('joyxoff');message({data:{id:posted.id,result:{ok:true,backend:'joyxoff'}}});assert.equal((await retry).backend,'joyxoff');assert.equal(timers.size,0);
 return {status:'PASS',cases:14,statusHint:await verifyMouseStatusHint(root),consumer:'bundled-production-gameproc;native-IPC-only;system-boundary-rejects-all-calls'};
}
