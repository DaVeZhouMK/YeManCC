import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
const expectedDataSha256="5d91842b3c3528a988331ff31e8bb350bd472fcdbe9e77c9a4926d32b0b380b9";
const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
function loadOfficial(){
 const raw=fs.readFileSync(new URL('./official-methods.json',import.meta.url));assert.equal(digest(raw),expectedDataSha256,'OFFICIAL_DATA_CHANGED');
 const official=JSON.parse(raw);assert.equal(official.primary.sha256,"4b818cd371388e06c66fe8c0bb592b7dfd4a6fb55eb56844d1b360b64aec3046");
 for(const item of official.excerpts){assert.equal(Buffer.byteLength(item.text),item.bytes);assert.equal(digest(item.text),item.sha256);assert.equal(item.text.length,item.utf16Length);assert(Number.isSafeInteger(item.utf16Offset)&&item.utf16Offset>=0);}
 const resolve=(key)=>{const item=official.excerpts.find(item=>item.key===key);assert(item,'OFFICIAL_EXCERPT_MISSING:'+key);return item.text};
 for(const [key,text]of Object.entries(official.methods))assert.equal(text,resolve(key));
 for(const group of ['lookup','profile','activation','focusCache','activationSubscription','windowFocus','popupTree','popupNativeFocus'])for(const [key,text]of Object.entries(official[group]))assert.equal(text,resolve(group+'.'+key));
 assert.equal(digest(official.windowFocusLibrary.decorator),official.windowFocusLibrary.decoratorSha256);
 assert.equal(official.popupBrowserViewHook,resolve("original-menu-browser-view-hook"));
 assert.equal(official.popupOriginalActivatedCallback,resolve("popupOriginalActivatedCallback"));
 return official;
}
export function createCompositionEnvironment({fixture,identityFixture,source}){
 const official=loadOfficial();
 const common=fixture.toString();let identity=identityFixture.toString();
 const from="logical=3301072349,full='14177997780718452736',registered=healthy?full:'2054970',app=healthy?logical:2054970";
 assert.equal(identity.split(from).length,2);identity=identity.replace(from,"logical=4279376404,full='18379781702487638016',registered=healthy?full:String(logical),app=healthy?logical:(logical&0xffffff)");
 const makeOriginal=vm.runInNewContext('(()=>{'+common+'\n'+identity+';return identityFixture})()', {assert,source,vm,performance,setTimeout,clearTimeout,console});
 const methods=official.lookup,resolverStart="sr=>this.WindowStore.GetOverlayInstances(sr).find(lr=>lr.IsGamepadUIOverlayWindow())?.GetFocusNavContext()";
 const checks=[],cases=[];
 const check=(name,predicate)=>{assert(predicate,name);checks.push({name,pass:true});};
 const plain=value=>JSON.parse(JSON.stringify(value));
 function make(options={}){
 const f=makeOriginal(options), windows=f.window.SteamUIStore.WindowStore,ui=f.window.SteamUIStore,w=f.overlay;
 const calls=[],forced=[];let focused=null;
 const held={RootWindow:{document:{activeElement:{tag:'held-document'}}},ActiveWindow:null,LogName:()=> 'held',m_LastActiveFocusNavTree:null,
  OnActivate(window){this.ActiveWindow=window;this.activations=(this.activations??0)+1},OnDeactivate(){this.ActiveWindow=null}};
 w.GetFocusNavContext=()=>held;
 w.IsDesktopOverlayWindow=()=>false;
 f.main.IsMainGamepadUIWindow=()=>true;
 f.main.IsGamepadUIOverlayWindow=()=>false;
 windows.m_mapOverlayPopupByPID=new Map([[f.proof.hostPid,w]]);windows.MainWindowInstance=f.main;
 const Re={iP:{GetFocusedOverlayAppID:()=>f.proof.appid,GetFocusedOverlayPID:()=>f.proof.hostPid}};
 const deps=[{$q:0,qL:{IsSteamVRRunningAndHMDAwake:false}},Re,{Input:{ForceConfiguratorFocus:value=>forced.push(value)}},{$l2:4},{WindowStore:windows},()=>{},{},{BV:{GamepadUI:{GameAPIOSK:()=>'/gameapi',AppRunning:()=>'/apprunning'}}},{Debug:()=>{}}];
 const method=text=>new Function('$','Re','SteamClient','r','fn','he','n','Le','Qi','return ({'+text+'});')(...deps);
 Object.assign(windows,method(methods.single),method(methods.plural),method(methods.fallback));
 const single=windows.GetOverlayInstance,plural=windows.GetOverlayInstances;
 windows.GetOverlayInstance=function(...args){calls.push({kind:'single',self:this,args});return single.apply(this,args)};
 windows.GetOverlayInstances=function(...args){calls.push({kind:'plural',self:this,args});return plural.apply(this,args)};
 ui.GetFocusedWindowInstance=()=>focused;ui.m_mainInstanceUIMode=4;ui.m_WindowStore=windows;
 Object.defineProperties(ui,Object.getOwnPropertyDescriptors(method(methods.active)));
 Object.assign(ui,method(methods.activated));
 ui.WindowStore=windows;
 const resolver=new Function('return ('+resolverStart+');').call(ui);
 const manager=Object.assign(method(methods.target),{m_nSystemKeyAppIDHint:f.proof.appid,m_fnSystemKeyContextResolver:resolver,
   GetActiveContext:()=>null,BCanActivateContext:()=>true});
 f.window.FocusedAppWindowStore=Re.iP;
 f.ui=ui;f.windows=windows;f.forced=forced;f.nav=held;f.manager=manager;f.lookupCalls=calls;f.setFocused=v=>{focused=v};
 f.originalDescriptors={single:Object.getOwnPropertyDescriptor(windows,'GetOverlayInstance'),plural:Object.getOwnPropertyDescriptor(windows,'GetOverlayInstances'),destroy:Object.getOwnPropertyDescriptor(w,'Destroy'),state:Object.getOwnPropertyDescriptor(f.client.Overlay,'SetOverlayState')};
 return f;
}
const profile=official.profile;
function navFixture(options={}){
 const f=make(options),m=f.manager,c=f.nav,w=f.overlay,ui=f.ui;
 ui.m_GamepadNavigationManager=m;ui.NavigationManager=m;
 m.m_ActiveContext=null;m.m_rgCatchAllGamepadInput=[];m.m_rgAllContexts=[c];m.m_nSystemKeyAppIDHint=undefined;
 const methods=new Function('return ({'+profile.GetActiveContext+','+profile.SetSystemKeyAppIDHint+','+profile.BCanActivateContext+'});')();
 Object.assign(m,methods);m.BHasVRGamepadNavigationContext=()=>false;
 w.BrowserWindow=c.RootWindow;w.m_BrowserWindow=c.RootWindow;w.m_FocusNavContext=c;
 w.IsVRGamepadUIOverlayWindow=()=>false;
 w.GetFocusNavContext=new Function('l','return ({'+profile.GetFocusNavContext+'}).GetFocusNavContext;')({oy:ui});
 m.FindContextForRootWindow=window=>window===c.RootWindow?c:undefined;
 c.m_controller=m;c.m_bMounted=true;c.m_bVR=false;c.m_bIsGamepadInputSuppressed=false;c.IsActive={Value:false};
 c.BIsVR=()=>c.m_bVR;c.BIsGamepadInputSuppressed=()=>c.m_bIsGamepadInputSuppressed;
 c.OnActivate=function(window){this.ActiveWindow=window;this.activations=(this.activations??0)+1;this.IsActive.Value=true;m.m_ActiveContext=this;f.onActivate?.();};
 return f;
}

const activationMethods=official.activation;

function actualActivation(options={}){
 const f=navFixture(options),c=f.nav,m=f.manager;
 const make=text=>new Function('H','queueMicrotask','"use strict";return ({'+text+'});')(()=>{},queueMicrotask);
 Object.assign(c,make(activationMethods.activate),make(activationMethods.set),make(activationMethods.active));
 Object.assign(m,make(activationMethods.context),make(activationMethods.batch));
 c.m_rootWindow=c.RootWindow;c.m_rgGamepadNavigationTrees=[];
 c.m_activeWindow=c.RootWindow;c.m_activeBrowserView=undefined;
 Object.defineProperty(c,'ActiveWindow',{get(){return this.m_activeWindow},configurable:true});
 c.m_valueIsActive=c.IsActive;c.m_valueIsActive.Set=function(value){this.Value=value;f.onActivate?.()};
 m.m_fnGamepadEventUpdateBatcher=fn=>fn();
 return f;
}

function onResolve(f,action){
 const original=f.windows.GetOverlayInstances;
 f.windows.GetOverlayInstances=function(...args){const value=original.apply(this,args);if(args[0]===f.proof.appid)action();return value};
}

 const compositionMethods=Object.fromEntries(['EnsureCorrectComposition','GetAppFocusedWindowID','EnsureCorrectGameOverlayState'].map(name=>[name,official.methods[name]]));
 for(const text of Object.values(compositionMethods))check('official-method-exact-primary-source',official.excerpts.some(item=>item.text===text&&digest(text)===item.sha256));
 return {popupNativeFocus:official.popupNativeFocus,originalContextActivate:official.activation.activate,popupOriginalActivatedCallback:official.popupOriginalActivatedCallback,popupBrowserViewHook:official.popupBrowserViewHook,popupTree:official.popupTree,focusCoordinator:official.windowFocus,focusLibrary:official.windowFocusLibrary,cacheFocusMethods:official.focusCache,actualActivation,check,checks,cases,plain,compositionMethods,equalMethod:official.methods.BNumberArraysEqual,getState:official.methods.GetCompositionState,officialDataSha256:expectedDataSha256,assemblyIdentity:{fixture:common,identity,globals:['assert','source','vm','performance','setTimeout','clearTimeout','console']}};
}
