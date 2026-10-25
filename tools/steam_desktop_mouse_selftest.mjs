import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
import {createHash} from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
export async function verifySteamDesktopMouse(evidence){
 assert(typeof evidence==='string'&&evidence.length>0,'Explicit evidence directory required');
 const out=path.resolve(evidence),relative=path.relative(root,out);assert(relative&&(relative.startsWith('..'+path.sep)||path.isAbsolute(relative)),'Evidence must be outside source');
 fs.mkdirSync(out,{recursive:true});assert(!fs.existsSync(path.join(out,'steam-desktop-mouse-result.json')));
 const source=fs.readFileSync(path.join(root,'native/steam_live.js'),'utf8'),passed=[];
 // No editor, input API or controller enumeration may be reached, even by an old request.
 for(const operation of ['desktopMouse.get','desktopMouse.set'])for(const enabled of [false,true]){
  let io=0;const reject=()=>{io++;throw Error('retired-controller-IO-reached');};
  const window={settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>42}}}};
  Object.defineProperties(window,{controllerConfiguratorStore:{get:reject},ControllerStore:{get:reject}});
  const fn=vm.runInNewContext(source,{window,SteamClient:new Proxy({},{get:reject}),performance,setTimeout,clearTimeout,console});
  const result=await fn({operation,account:42,slot:0,enabled,percent:100,installPreset:true,presetUrl:'autosave://S:/obsolete.vdf'});
  assert.equal(result.ok,false);assert.equal(result.reason,'steam-desktop-mouse-retired');assert.equal(result.mutated,false);assert.equal(io,0);assert(!window.__ymccSteamLiveBusy);
  passed.push(operation+' '+enabled+' refuses before Steam IO');
 }
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 assert(!/steamDesktopMouse|g_steamMouseSharedVirtualTarget|steam_desktop_mouse_runtime/.test(main),'retired ordinary provider must have no native consumer');
 assert(main.includes('return fail("steamdeck-not-enabled")'),'SteamDeck-only sensitivity guard remains');
 assert(!fs.existsSync(path.join(root,'native/steam_desktop_mouse_runtime.h'))&&!fs.existsSync(path.join(root,'native/steam_desktop_mouse_preset.h')),'retired import and preset removed');
 const files=['native/main.cpp','native/steam_live.js','native/steam_live_script.h','src/components/ControllerShortcutsCard.vue','tools/steam_desktop_mouse_selftest.mjs'];
 const result={status:'PASS',scope:'retired-standard-Steam-mouse-no-IO;SD-sensitivity-remains',jsCases:passed.length,passed,hardwareOperations:0,installedApplicationChanged:false,sourceHashes:Object.fromEntries(files.map(n=>[n,createHash('sha256').update(fs.readFileSync(path.join(root,n))).digest('hex')]))};
 fs.writeFileSync(path.join(out,'steam-desktop-mouse-result.json'),JSON.stringify(result,null,2));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))console.log(JSON.stringify(await verifySteamDesktopMouse(process.argv[2]||'')));
