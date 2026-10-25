// Actual Valve menu class + pinned Decky Navigation/patcher + production default-tab helper.
// Memory-only windows. This test cannot create/open a real Steam window or operate hardware.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {execFileSync} from 'node:child_process';import {createRequire} from 'node:module';import crypto from 'node:crypto';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const archive='G:/YeManCC-Work/Archives/Steam-QAM-YMCC-Wishlist-20261009';
const source=fs.readFileSync(process.argv[2]||path.join(validation,'valve-menu-class.js'),'utf8');
const tar=entry=>execFileSync('tar',['-xOf',archive+'/downloads/decky--ui--4.12.1.tgz',entry],{encoding:'utf8'});
const patcherSource=tar('package/src/utils/patcher.ts'),navigationSource=tar('package/src/modules/Router.ts');
const evaluate=(text,globals={})=>{const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console:{debug(){},warn(){},error(){},info(){}},...globals});return module.exports;};
const patcher=evaluate(patcherSource),helper=evaluate(fs.readFileSync(path.join(root,'decky-plugin/src/qamDefaultTab.ts'),'utf8'));
const id=100019,cases=[];const check=(name,fn)=>{fn();cases.push(name);};
function fixture(){
 const events=[],selections=[];
 const enums={None:0,Main:1,QuickAccess:2},tabs={Settings:4,Friends:3,Decky:999,Notifications:0};
 const Menu=vm.runInNewContext(source+';h;',{i:enums,n:tabs,a:{Gn(){},fm:fn=>fn},m:{Pf:class{Init(){}}},p:{oy:{GamepadUIAudio:{PlayNavSound:sound=>events.push(sound)}}},c:{PN:{OpenSideMenu:'open',CloseSideMenu:'close'}},d:{y:{ReportTrackedAction:()=>{}}},g:{LN:{SetQAMFriendsChatExpanded(){}}},console:{info(){}}});
 const windowStore={SteamUIWindows:[],OverlayWindows:[]};let focused=null;
 const host={SteamUIStore:{GetFocusedWindowInstance:()=>focused}};let owner=true;
 const Router={WindowStore:windowStore};
 const navigation=evaluate(navigationSource,{window:host,require:name=>{if(name==='../logger')return {default:class{warn(){}error(){}},__esModule:true};if(name==='../webpack')return {findModuleExport:()=>Router};throw Error('Unexpected navigation import '+name);}});
 const dfl={...patcher,...navigation,Router};
 function addWindow({vr=false,gamepad=true,primary=false}={}){
  const w={IsAnyVRWindow:()=>vr,IsGamepadUIWindow:()=>gamepad};w.MenuStore=new Menu(w);
  // Emulate Steam's actual immutable own MobX bound action; leave it intact throughout.
  const original=w.MenuStore.OpenQuickAccessMenu;
  Object.defineProperty(w.MenuStore,'OpenQuickAccessMenu',{value:function(...args){selections.push(args);return original.apply(w.MenuStore,args);},writable:false,configurable:false});
  windowStore.SteamUIWindows.push(w);if(primary)windowStore.GamepadUIMainWindowInstance=w;return w;
 }
 const main=addWindow({primary:true});focused=main;
 const h=helper.installConsoleDefault(dfl,host,id,()=>owner);
 return {h,dfl,host,main,menu:main.MenuStore,events,selections,addWindow,focus:w=>{focused=w;},revoke:()=>{owner=false;},Menu};
}
check('installation performs no selection or opening and preserves the immutable bound action',()=>{const f=fixture();assert.equal(f.h.active,true);assert.equal(f.menu.GetOpenSideMenu(),0);assert.equal(f.menu.GetQuickAccessTab(),4);assert.equal(f.selections.length,0);assert.equal(Object.getOwnPropertyDescriptor(f.menu,'OpenQuickAccessMenu').writable,false);f.h.dispose();});
check('three-dot toggle selects console before the single original menu opening',()=>{const f=fixture();f.menu.ToggleSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),id);assert.equal(f.menu.GetOpenSideMenu(),2);assert.deepEqual(f.events,['open']);assert.deepEqual(f.selections,[[id,false]]);f.h.dispose();});
check('the next three-dot press closes without reopening or changing the user tab',()=>{const f=fixture();f.menu.ToggleSideMenu(2);f.menu.OpenQuickAccessMenu(5,false);const n=f.selections.length;f.menu.ToggleSideMenu(2);assert.equal(f.menu.GetOpenSideMenu(),0);assert.equal(f.menu.GetQuickAccessTab(),5);assert.equal(f.selections.length,n);assert.deepEqual(f.events,['open','close']);f.h.dispose();});
check('every fresh open defaults back to console after navigating another tab',()=>{const f=fixture();for(let n=0;n<100;n++){f.menu.ToggleSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),id);f.menu.OpenQuickAccessMenu(4,false);f.menu.ToggleSideMenu(2);}assert.equal(f.events.length,200);assert.equal(f.menu.GetOpenSideMenu(),0);f.h.dispose();});
check('native main-menu toggle retains its original selected tab and sound count',()=>{const f=fixture();f.menu.ToggleSideMenu(1);assert.equal(f.menu.GetQuickAccessTab(),4);assert.equal(f.menu.GetOpenSideMenu(),1);assert.equal(f.selections.length,0);f.h.dispose();});
check('explicit false toggle neither opens nor selects',()=>{const f=fixture();f.menu.ToggleSideMenu(2,false);assert.equal(f.menu.GetOpenSideMenu(),0);assert.equal(f.selections.length,0);f.h.dispose();});
check('explicit true toggle opens once and repeated true does not steal active navigation',()=>{const f=fixture();f.menu.ToggleSideMenu(2,true);f.menu.OpenQuickAccessMenu(3,false);const n=f.selections.length;f.menu.ToggleSideMenu(2,true);assert.equal(f.menu.GetQuickAccessTab(),3);assert.equal(f.selections.length,n);assert.deepEqual(f.events,['open']);f.h.dispose();});
check('generic Decky Navigation OpenSideMenu selects console once before opening',()=>{const f=fixture();f.dfl.Navigation.OpenSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),id);assert.equal(f.menu.GetOpenSideMenu(),2);assert.deepEqual(f.selections,[[id,false]]);f.h.dispose();});
check('Decky Navigation opening the default Decky entry goes directly to console',()=>{const f=fixture();f.dfl.Navigation.OpenQuickAccessMenu(999);assert.equal(f.menu.GetQuickAccessTab(),id);assert.equal(f.menu.GetOpenSideMenu(),2);assert.deepEqual(f.selections,[[id]]);f.h.dispose();});
check('Decky Navigation with no requested tab defaults to console',()=>{const f=fixture();f.dfl.Navigation.OpenQuickAccessMenu();assert.equal(f.menu.GetQuickAccessTab(),id);assert.equal(f.menu.GetOpenSideMenu(),2);f.h.dispose();});
check('explicit native Navigation target such as notifications or performance is retained',()=>{for(const key of [0,3,4,5,6,7,90001]){const f=fixture();f.dfl.Navigation.OpenQuickAccessMenu(key);assert.equal(f.menu.GetQuickAccessTab(),key);assert.equal(f.menu.GetOpenSideMenu(),2);f.h.dispose();}});
check('manual native tab selection, including original Decky home, remains available',()=>{const f=fixture();f.menu.ToggleSideMenu(2);for(const key of [0,4,5,999,id]){f.menu.OpenQuickAccessMenu(key,false);assert.equal(f.menu.GetQuickAccessTab(),key);assert.equal(f.menu.GetOpenSideMenu(),2);}f.h.dispose();});
check('already visible generic opening preserves a user selected tab',()=>{const f=fixture();f.menu.ToggleSideMenu(2);f.menu.OpenQuickAccessMenu(0,false);f.dfl.Navigation.OpenSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),0);assert.deepEqual(f.events,['open']);f.h.dispose();});
check('suppressed menu requests do not change its selection or open it',()=>{const f=fixture();f.menu.m_cSuppressRequests=1;f.menu.ToggleSideMenu(2);f.dfl.Navigation.OpenSideMenu(2);assert.equal(f.menu.GetOpenSideMenu(),0);assert.equal(f.menu.GetQuickAccessTab(),4);assert.equal(f.selections.length,0);f.h.dispose();});
check('future gamepad overlay using the verified class follows the same default',()=>{const f=fixture();const overlay=f.addWindow();f.focus(overlay);overlay.MenuStore.ToggleSideMenu(2);assert.equal(overlay.MenuStore.GetQuickAccessTab(),id);assert.equal(f.menu.GetOpenSideMenu(),0);overlay.MenuStore.CloseSideMenus();f.dfl.Navigation.OpenQuickAccessMenu(999);assert.equal(overlay.MenuStore.GetQuickAccessTab(),id);f.h.dispose();});
check('VR and non-gamepad windows retain original navigation',()=>{for(const flags of [{vr:true},{gamepad:false}]){const f=fixture();const w=f.addWindow(flags);f.focus(w);w.MenuStore.ToggleSideMenu(2);assert.equal(w.MenuStore.GetQuickAccessTab(),4);f.dfl.Navigation.OpenQuickAccessMenu(999);assert.equal(w.MenuStore.GetQuickAccessTab(),999);f.h.dispose();}});
check('missing focused window uses the same original Navigation fallback',()=>{const f=fixture();f.focus(null);f.dfl.Navigation.OpenSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),id);f.h.dispose();});
check('revoked console registration disables automatic selection immediately',()=>{const f=fixture();f.revoke();f.menu.ToggleSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),4);f.dfl.Navigation.OpenQuickAccessMenu(999);assert.equal(f.menu.GetQuickAccessTab(),999);f.h.dispose();});
check('unload restores the verified prototype and Navigation wrapper without touching state',()=>{const f=fixture();const original=f.dfl.Navigation.OpenSideMenu.__deckyPatch.original,protoOriginal=f.Menu.prototype.ToggleSideMenu.__deckyPatch.original;f.h.dispose();f.h.dispose();assert.equal(f.Menu.prototype.ToggleSideMenu,protoOriginal);assert.equal(f.dfl.Navigation.OpenSideMenu,original);f.menu.ToggleSideMenu(2);assert.equal(f.menu.GetQuickAccessTab(),4);});
check('patches added after ours remain active when our plugin is unloaded',()=>{const f=fixture();let called=0;const other=patcher.beforePatch(f.Menu.prototype,'ToggleSideMenu',()=>called++);f.h.dispose();f.menu.ToggleSideMenu(2);assert.equal(called,1);assert.equal(f.menu.GetQuickAccessTab(),4);assert.equal(other.hasUnpatched,false);other.unpatch();});
check('unrecognized enum or Steam class safely retains existing behavior',()=>{const f=fixture();f.h.dispose();for(const dfl of [{...f.dfl,SideMenu:{None:0,QuickAccess:10}},{...f.dfl,Router:{WindowStore:{}}},{...f.dfl,beforePatch:undefined}]){assert.equal(helper.installConsoleDefault(dfl,f.host,id,()=>true).active,false);}assert.equal(f.menu.GetQuickAccessTab(),4);});
const result={status:'passed',checks:cases.length,cases,actualValveMenuClassSha256:crypto.createHash('sha256').update(source).digest('hex'),exactDeckyNavigation:true,exactDeckyPatcher:true,actualSteamOperated:false,timersCreated:0};fs.writeFileSync(path.join(validation,'QAM-DEFAULT-REGRESSION.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
