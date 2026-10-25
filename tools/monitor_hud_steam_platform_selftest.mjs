// Passive actual-Steam SDK/router capability check with INERT transport. No
// game binding/window, focus, input, source data, installed files or SDK reconnect.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {targets,connect} from './fixtures/decky_cdp.mjs';import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/product-output-20261010/platform');fs.mkdirSync(out,{recursive:true});
const slot='__YMCC_PRODUCT_HUD_PLATFORM_TEST_DISPOSE__';
const entry=`import {installSteamMonitorHud} from './decky-plugin/src/steamMonitorHud.tsx';const transport={subscribeExtra(){return ()=>{}},subscribe(cb){cb({connected:false,snapshot:null,notice:'INERT TEST ONLY',busy:false});return ()=>{}},setPassiveVisible(){},sendHud(){return false}};window.${slot}=installSteamMonitorHud({host:window,react:SP_REACT,reactDom:typeof SP_REACTDOM==='undefined'?undefined:SP_REACTDOM,ui:DFL,routerHook:window.DeckyPluginLoader.routerHook,client:transport});`;
const b=await build({stdin:{loader:'ts',contents:entry,resolveDir:root},bundle:true,platform:'browser',format:'iife',write:false,plugins:[monitorHudVuePlugin(root)]});const code=b.outputFiles[0].text;fs.writeFileSync(path.join(out,'platform-inert-installer.js'),code);
let c,installed=false;const report={actualSteamSdk:true,actualTransport:false,actualGameWindow:false,physicalMousePassVerified:false,installedRuntimeChanged:false,hardwareTouched:false};
try{
 const target=(await targets()).find(t=>t.title==='SharedJSContext');assert(target,'Running Steam SharedJSContext required');c=await connect(target.webSocketDebuggerUrl);
 report.before=await c.evaluate(`(()=>({hudSlot:!!window.__YMCC_STEAM_MONITOR_HUD__,components:[...window.DeckyPluginLoader.routerHook.globalComponentsState._components.keys()],plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),popups:[...window.g_PopupManager.GetPopups()].map(p=>p.GetName())}))()`);
 assert.equal(report.before.hudSlot,false,'Never replace an existing product HUD owner');
 await c.evaluate(`window.__YMCC_PLATFORM_EVENT_MAP__=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');`);
 await c.evaluate(code);installed=true;
 for(let n=0;n<30;n++){report.inspect=await c.evaluate('window.__YMCC_STEAM_MONITOR_HUD__.inspect()');if(report.inspect.roots>0)break;await new Promise(r=>setTimeout(r,100));}
 assert.equal(report.inspect.capable,true);assert(report.inspect.roots>=1);assert.equal(report.inspect.children,0);assert.equal(report.inspect.frame,null);assert.equal(report.inspect.desktopFallback,false);report.capabilityPass=true;
}catch(error){report.error=String(error.stack||error);process.exitCode=1;}
finally{
 if(c&&installed){report.cleanup=await c.evaluate(`(()=>{window.${slot}?.();delete window.${slot};const result={hudAbsent:!window.__YMCC_STEAM_MONITOR_HUD__,components:[...window.DeckyPluginLoader.routerHook.globalComponentsState._components.keys()],plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),popups:[...window.g_PopupManager.GetPopups()].map(p=>p.GetName()),sdkEventMapUnchanged:window.__YMCC_PLATFORM_EVENT_MAP__===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar')};delete window.__YMCC_PLATFORM_EVENT_MAP__;return result})()`);
 try{assert(report.cleanup.hudAbsent);assert(report.cleanup.sdkEventMapUnchanged);assert.deepEqual(report.cleanup.plugins,report.before.plugins);assert.deepEqual(report.cleanup.components,report.before.components);assert.deepEqual(report.cleanup.popups,report.before.popups);report.cleanupPass=true;}catch(error){report.cleanupError=String(error);process.exitCode=1;}
 }
 c?.close();report.scope='Exact production formal installer in real running Steam/Decky context; only live hook discovery and React root mount/cleanup. Transport deliberately inert; NO native popup, real sensor data, fullscreen game or physical mouse acceptance.';
 fs.writeFileSync(path.join(out,'STEAM-PLATFORM-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}
