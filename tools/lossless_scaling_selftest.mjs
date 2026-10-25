// Exact original quickapp owner under inert file/process boundaries, plus real LS list read-only.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';
import {root,task} from './fixtures/decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const source=fs.readFileSync(path.join(root,'src/bridge/quickapp.ts'),'utf8');
function load(text,modules={}){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,Uint8Array,btoa:value=>Buffer.from(value,'binary').toString('base64'),require:name=>{if(!(name in modules))throw Error('Unexpected import '+name);return modules[name];}});return module.exports;}
const profiles=load(fs.readFileSync(path.join(root,'src/bridge/losslessProfiles.ts'),'utf8'));
const profile=(title,p,enabled=true)=>`    <Profile><Title>${title}</Title><Path>${p}</Path><AutoScale>${enabled}</AutoScale><ScalingType>FSR</ScalingType><FrameGeneration>LSFG3</FrameGeneration></Profile>`;
const xml=`<Settings><Unknown>retained</Unknown>\n  <GameProfiles>\n${profile('A','D:\\Games\\a.exe')}\n${profile('B','D:\\Games\\b.exe')}\n  </GameProfiles>\n</Settings>`;
const observedScripts=new Set();
const file='Q:\\LocalData\\Lossless Scaling\\Settings.xml',exe='C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
function fixture(options={}){
 let runtime;const files=new Map([[file,options.xml??xml],[exe,'locked-asset']]);const originalSet=files.set.bind(files);files.set=(p,value)=>{runtime?.invalidateLosslessRuntime();return originalSet(p,value);};const events=[];let running=!!options.running,stamp='123',liveXml=options.liveXml;const hooks=new Proxy({closeDenied:!!options.closeDenied,backupDenied:false,sourceChange:false,startDenied:false,observeUnknown:false,replaceProcess:false,statusMissing:false},{set(object,key,value){object[key]=value;runtime?.invalidateLosslessRuntime();return true;}});let title='A',button='缩放';
 const api={windowApi:{losslessReturnTarget:async(target,enabled)=>{events.push('focus-target:'+JSON.stringify({target,enabled}));return {ok:true};}},fs:{exists:async p=>files.has(p),readTextFile:async p=>{if(!files.has(p))throw Error('missing fixture '+p);return files.get(p);},writeTextFileAtomic:async(p,text)=>{events.push('write:'+p);if(p.endsWith('.ymccbak')&&hooks.backupDenied)throw Error('backup denied');if(p===file&&running)throw Error('SHARING_VIOLATION_RUNNING_LS');files.set(p,text);if(p.endsWith('.ymccbak')&&hooks.sourceChange)files.set(file,xml.replace('retained','external-update'));}},
 losslessRuntime:{detect:async()=>{events.push('runtime-detect');if(hooks.runtimeUnknown)return {known:false,running:false,readable:false,pid:0,processCreated:'0',path:'',button:'',status:'',reason:'multiple-lossless-instances',configRevision:0};if(running)events.push('status-readonly');return {known:true,running,readable:running&&!hooks.observeUnknown,pid:running?71:0,processCreated:hooks.replaceProcess?stamp+'new':stamp,path:hooks.runtimePath??exe,button,status:hooks.statusMissing?'':'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"',reason:'',configRevision:0};},watch:async()=>true,ensureRunning:async p=>{const started=!running;assert.equal(p,hooks.runtimePath??exe);if(hooks.startDenied)return {ok:false,reason:'denied',win32Error:5};if(!running){events.push('launch:'+JSON.stringify(['-StartMinimized']));running=true;title='A';button='缩放';}else events.push('reuse');return {ok:true,started,reused:!started,pid:71,processCreated:stamp,path:hooks.runtimePath??exe};},
 observe:async identity=>{assert.equal(identity.exe,exe);assert.equal(identity.pid,71);events.push('status-readonly');if(hooks.observeUnknown)return {ok:false,button:'',status:''};return {ok:true,button,status:hooks.statusMissing?'':'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"'};},
 resetFailed:async request=>{assert.equal(request.pid,71);assert.equal(request.processCreated,stamp);assert.ok(request.retryAttempt===0||request.retryAttempt===1);events.push('native-timeout-reset');if(hooks.resetDenied)return {failureResetRequested:true,failureResetAttempted:true,failureResetTerminated:false,failureResetClosed:false,failureResetReason:'failure-reset-terminate-denied'};running=false;return {failureResetRequested:true,failureResetAttempted:true,failureResetTerminated:true,failureResetClosed:true};},
 ensureScaling:async request=>{assert.equal(request.exe,hooks.runtimePath??exe);assert.equal(request.pid,71);assert.equal(request.processCreated,stamp);events.push('native-ensure-scaling');assert.ok(request.retryAttempt===0||request.retryAttempt===1);if(hooks.injectedErrorOnce){const error=hooks.injectedErrorOnce;hooks.injectedErrorOnce=null;throw error;}if(hooks.timeoutOnce){hooks.timeoutOnce=false;throw Object.assign(new Error('IPC command timed out: lossless.ensureScaling'),{code:'IPC_TIMEOUT',command:'lossless.ensureScaling'});}if(hooks.timeoutEvery)throw Object.assign(new Error('IPC command timed out: lossless.ensureScaling'),{code:'IPC_TIMEOUT',command:'lossless.ensureScaling'});assert.equal(request.reapply,true);assert.equal(request.refreshWindow,true);assert.equal(request.resetOnFailure,false);if(hooks.activationGate)await hooks.activationGate;events.push('game-target:'+JSON.stringify({pid:request.gamePid,processCreated:request.gameProcessCreated,path:request.gamePath}));if(hooks.observeUnknown)return {ok:false,enabled:null,invoked:false,pending:false,cancelled:false,reason:'no-focus-automation-unavailable'};if(hooks.activationReason)return {ok:false,enabled:null,invoked:true,pending:true,cancelled:hooks.activationReason==='target-not-foreground',reason:hooks.activationReason,...(hooks.legacyResetReceipt?{failureResetRequested:true,failureResetClosed:true}:{})};const already=button==='取消缩放';if(already&&title!==request.profileTitle)return {ok:false,enabled:false,invoked:false,pending:false,cancelled:false,reason:'another-profile-active'};if(already&&request.reapply)events.push('native-reapply');title=request.profileTitle;button='取消缩放';events.push('native-window-refresh');return {windowRefreshed:!hooks.omitRefreshReceipt,refreshAttempted:true,ok:true,enabled:true,invoked:!already||request.reapply,pending:false,cancelled:false,restarted:already&&request.reapply,stopInvoked:already&&request.reapply,reason:''};}},
 registry:{read:async()=>null},proc:{findExact:async p=>({found:p===(hooks.runtimePath??exe)?running:/[ab]\.exe$/i.test(p),pid:p===(hooks.runtimePath??exe)?(running?71:0):110}),identity:async pid=>pid===71?({valid:running,pid,processCreated:hooks.replaceProcess?stamp+'new':stamp,path:hooks.runtimePath??exe}):({valid:!hooks.gameMissing,pid,processCreated:'4567',path:options.gamePath??'D:\\Games\\a.exe'})},
 shell:{execute:async(p,args)=>{events.push('launch:'+JSON.stringify(args));if(hooks.startDenied)return false;running=true;title=args.includes('-auto')?'A':title;button=args.includes('-auto')?'取消缩放':button;return true;},run:async(p,args)=>{if(args.includes('Write-Output $env:LOCALAPPDATA'))return {exitCode:0,stdout:'Q:\\LocalData',stderr:''};const script=args.includes('-EncodedCommand')?Buffer.from(args.at(-1),'base64').toString('utf16le'):args.join(' ');if(args.includes('-EncodedCommand'))observedScripts.add(script);if(script.includes('YMCC_LOSSLESS_CLOSE')){events.push('normal-close');assert.match(script,/GetProcessTimes/);assert.match(script,/QueryFullProcessImageName/);assert.match(script,/PostMessage/);assert.ok(!script.includes('CloseMainWindow'));assert.ok(!script.includes('ShowWindow'));if(hooks.closeDenied)return {exitCode:1,stdout:'',stderr:'close to tray'};running=false;if(liveXml)files.set(file,liveXml);return {exitCode:0,stdout:'',stderr:''};}if(script.includes('原开启动作不重发')){events.push('bounded-start-observation');return {exitCode:0,stdout:'',stderr:''};}if(script.includes('UIAutomationClient')){events.push('status-readonly');assert.ok(!script.includes('.Invoke('));if(hooks.observeUnknown)return {exitCode:1,stdout:'',stderr:'UIA denied'};return {exitCode:0,stdout:JSON.stringify({title:options.selectedTitle??title,button,status:hooks.statusMissing?'':'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"'}),stderr:''};}throw Error('Unexpected actual shell operation');}}};
 runtime=load(fs.readFileSync(path.join(root,'src/bridge/losslessRuntimeState.ts'),'utf8'),{'./api':api,'./ipc':{on:()=>()=>{}},'./gamePolicyTarget':{subscribePolicyGameStatus:()=>()=>{}}});
 const owner=load(source+'\nexport const unsafeImportForRegression=ensureLsProfileUnlocked;',{'./api':api,'./losslessProfiles':profiles,'./losslessRuntimeState':runtime,'./gamedetect':{},'./optiscalerClientSync':{}});
 return {owner,files,events,hooks,running:()=>running,observe:(t,b)=>{title=t;button=b;runtime.invalidateLosslessRuntime();},runtime};
}
const results=[];async function check(name,action){await action();results.push(name);console.log('PASS '+name);}
function realTransport(){
 const timers=new Map(),posted=[];let nextTimer=1,listener;
 const module={exports:{}};const window={chrome:{webview:{postMessage:packet=>posted.push(packet),addEventListener:(_,fn)=>{listener=fn;}}},dispatchEvent(){},addEventListener(){},removeEventListener(){}};
 vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/ipc.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,window,console,queueMicrotask,setTimeout:(fn,ms)=>{const id=nextTimer++;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id)});
 const ipc=module.exports,api=load(fs.readFileSync(path.join(root,'src/bridge/api.ts'),'utf8'),{'./ipc':ipc});
 return {ipc,api,timers,posted,fire:ms=>{for(const [id,t]of [...timers])if(t.ms<=ms){timers.delete(id);t.fn();}},reply:value=>listener({data:{id:posted.at(-1).id,...value}})};
}
const realRequest={exe,pid:71,processCreated:'123',gamePath:'D:/Games/a.exe',profileTitle:'A',refreshWindow:true,resetOnFailure:false,retryAttempt:0};
await check('actual IPC/API produces timeout metadata only after the 13-second response timer',async()=>{
 const t=realTransport(),waiting=t.api.losslessRuntime.ensureScaling(realRequest).catch(e=>e);assert.equal(t.posted[0].cmd,'lossless.ensureScaling');assert.equal([...t.timers.values()][0].ms,13000);t.fire(12999);assert.equal(t.timers.size,1);t.fire(13000);const error=await waiting;assert.equal(error.code,'IPC_TIMEOUT');assert.equal(error.command,'lossless.ensureScaling');
 const f=fixture();f.hooks.injectedErrorOnce=error;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);assert.equal(result.confirmed,true);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,1);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,2);
});
await check('actual returned native timeout text is a receipt and cannot authorize killing',async()=>{
 const t=realTransport(),waiting=t.api.losslessRuntime.ensureScaling(realRequest).catch(e=>e);t.reply({error:'provider timed out: lossless.ensureScaling'});const error=await waiting;assert.ok(error instanceof t.ipc.IpcRemoteError);assert.equal(error.code,undefined);assert.equal(t.timers.size,0);
 const f=fixture();f.hooks.injectedErrorOnce=error;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);assert.equal(result.confirmed,false);assert.equal(result.pending,false);assert.equal(f.running(),true);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,0);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
});
await check('an unrelated IPC timeout cannot authorize the LS scaling watchdog',async()=>{
 const f=fixture();f.hooks.injectedErrorOnce=Object.assign(new Error('IPC command timed out: lossless.getState'),{code:'IPC_TIMEOUT',command:'lossless.getState'});const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);assert.equal(result.confirmed,false);assert.equal(f.running(),true);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,0);
});
await check('actual reset API always supplies the explicit scaling-timeout trigger',async()=>{
 const t=realTransport(),waiting=t.api.losslessRuntime.resetFailed({exe,pid:71,processCreated:'123',retryAttempt:0});assert.equal(t.posted[0].args.trigger,'scaling-ipc-timeout');assert.equal(t.posted[0].cmd,'lossless.resetFailed');t.reply({result:{failureResetRequested:true,failureResetClosed:true}});assert.equal((await waiting).failureResetClosed,true);assert.equal(t.timers.size,0);
});

await check('baseline locked-file failure reproduced inside the exact unguarded original import body',async()=>{const f=fixture({running:true});await assert.rejects(f.owner.unsafeImportForRegression('D:\\Games\\c.exe'),/SHARING_VIOLATION/);assert.equal(f.files.get(file),xml);});
await check('fixed original import closes LS before fresh read/write and restores its app once',async()=>{const f=fixture({running:true});await f.owner.ensureLsProfile('D:\\Games\\c.exe');assert.equal(f.running(),true);assert.equal(f.events.filter(e=>e==='normal-close').length,1);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);assert.ok(f.events.indexOf('normal-close')<f.events.indexOf('write:'+file));const parsed=profiles.parseLsProfiles(f.files.get(file));assert.deepEqual(Array.from(parsed,p=>p.title),['A','B','c.exe']);assert.match(f.files.get(file),/<Unknown>retained<\/Unknown>/);});
await check('LS final in-memory save is read after normal close, not overwritten by the earlier snapshot',async()=>{const newer=xml.replace('retained','latest-live-save'),f=fixture({running:true,liveXml:newer});await f.owner.ensureLsProfile('D:\\Games\\c.exe');assert.match(f.files.get(file),/latest-live-save/);assert.equal(f.files.get(file+'.ymccbak'),newer);});
await check('closed LS import keeps it closed and does not launch an unrelated scaling session',async()=>{const f=fixture();await f.owner.ensureLsProfile('D:\\Games\\c.exe');assert.equal(f.running(),false);assert.equal(f.events.filter(e=>e==='normal-close'||e.startsWith('launch:')).length,0);});
await check('refused normal closure does not force-kill LS or touch either original file',async()=>{const f=fixture({running:true,closeDenied:true});await assert.rejects(f.owner.ensureLsProfile('D:\\Games\\c.exe'),/close to tray/);assert.equal(f.files.get(file),xml);assert.equal(f.files.has(file+'.ymccbak'),false);assert.equal(f.running(),true);});
await check('backup failure preserves main and restores the original LS app',async()=>{const f=fixture({running:true});f.hooks.backupDenied=true;await assert.rejects(f.owner.ensureLsProfile('D:\\Games\\c.exe'),/backup denied/);assert.equal(f.files.get(file),xml);assert.equal(f.running(),true);});
await check('a source change after backup is rejected before original-file replacement',async()=>{const f=fixture({running:true});f.hooks.sourceChange=true;await assert.rejects(f.owner.ensureLsProfile('D:\\Games\\c.exe'),/配置已变化/);assert.match(f.files.get(file),/external-update/);assert.ok(!f.events.includes('write:'+file));assert.equal(f.running(),true);});
await check('malformed XML fails before closing the live LS process or replacing its backup',async()=>{const f=fixture({running:true,xml:'<broken>'});f.files.set(file+'.ymccbak','known-good');await assert.rejects(f.owner.ensureLsProfile('D:\\Games\\c.exe'),/格式/);assert.deepEqual(f.events.filter(e=>e!=='runtime-detect'&&e!=='status-readonly'),[]);assert.equal(f.files.get(file+'.ymccbak'),'known-good');assert.equal(f.running(),true);});
await check('AutoScale true does not mean actual running/scaling when LS is closed',async()=>{const f=fixture();const state=await f.owner.getLosslessGameState('D:\\Games\\a.exe');assert.equal(state.profile.enabled,true);assert.equal(state.enabled,false);assert.equal(state.running,false);assert.deepEqual(f.events.filter(e=>e!=='runtime-detect'&&e!=='status-readonly'),[]);});
await check('readonly actual LS active-profile receipt and stop-button identify this EXE only',async()=>{const f=fixture({running:true});f.observe('A','取消缩放');assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).enabled,true);assert.equal((await f.owner.getLosslessGameState('D:\\Games\\b.exe')).enabled,false);assert.ok(f.events.every(e=>e==='status-readonly'||e==='runtime-detect'));});
await check('unreadable actual status is unknown and never fabricated as hardware off/on',async()=>{const f=fixture({running:true});f.hooks.observeUnknown=true;assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).enabled,null);});
await check('original on quietly starts with fixed native arguments and no profile reset',async()=>{const f=fixture();const result=await f.owner.oneClickFrameGen('D:\\Games\\a.exe');assert.equal(result.alreadyHadProfile,true);assert.equal(f.files.get(file),xml);assert.equal(f.events.filter(e=>e.startsWith('launch:'))[0],'launch:'+JSON.stringify(['-StartMinimized']));});
await check('shared original on confirms its read-only receipt after one launch, without replay or XML writes',async()=>{const f=fixture();const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);assert.equal(result.confirmed,true);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);assert.equal(f.files.get(file),xml);assert.ok(f.events.includes('status-readonly'));assert.equal(result.runtimeConfirmed,true);});
await check('original launch refusal is visible rather than a false success',async()=>{const f=fixture();f.hooks.startDenied=true;await assert.rejects(f.owner.oneClickFrameGen('D:\\Games\\a.exe'),/启动请求失败/);});
await check('off closes selected EXE session and disables only its AutoScale after process exit',async()=>{const f=fixture({running:true});f.observe('A','取消缩放');const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);assert.equal(result.confirmed,true);assert.equal(f.running(),false);assert.equal(profiles.matchLsProfile(profiles.parseLsProfiles(f.files.get(file)),'D:\\Games\\a.exe').enabled,false);assert.equal(profiles.parseLsProfiles(f.files.get(file)).length,2);assert.equal(profiles.parseLsProfiles(f.files.get(file))[1].title,'B');assert.ok(f.events.indexOf('normal-close')<f.events.indexOf('write:'+file));assert.equal(f.files.get(file+'.ymccbak'),xml);});
await check('original off refuses another EXE session and keeps LS running',async()=>{const f=fixture({running:true});f.observe('B','取消缩放');await assert.rejects(f.owner.stopLosslessScaling('D:\\Games\\a.exe'),/另一 EXE/);assert.equal(f.running(),true);assert.ok(!f.events.includes('normal-close'));});
await check('existing generated EXE list is required; missing match never creates a sidebar preset',async()=>{const f=fixture();const state=await f.owner.getLosslessGameState('D:\\Games\\missing.exe');assert.equal(state.profile,null);assert.equal(state.controlAvailable,true);assert.equal(f.files.get(file),xml);assert.deepEqual(f.events.filter(e=>e!=='runtime-detect'&&e!=='status-readonly'),[]);});
await check('Xbox UIA failure keeps explicit controls usable without falsely confirming scaling',async()=>{
 const f=fixture();f.hooks.observeUnknown=true;
 const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(result.runtimeConfirmed,null);assert.match(result.notice,/待确认/);
 const state=await f.owner.getLosslessGameState('D:\\Games\\a.exe');assert.equal(state.enabled,null);assert.equal(state.configuredEnabled,true);assert.equal(state.controlAvailable,true);assert.equal(state.running,true);
 assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);
 const stopped=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);assert.equal(stopped.confirmed,true);assert.equal(f.running(),false);
 assert.equal(profiles.matchLsProfile(profiles.parseLsProfiles(f.files.get(file)),'D:\\Games\\a.exe').enabled,false);
});
await check('unreadable unowned session cannot close another game or delete configuration',async()=>{
 const f=fixture({running:true});f.hooks.observeUnknown=true;
 await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false),/不可读|未确认/);
 assert.equal(f.running(),true);assert.equal(f.files.get(file),xml);assert.ok(!f.events.includes('normal-close'));
});
await check('recorded LS launch ownership is invalid after process birth changes',async()=>{
 const f=fixture();f.hooks.observeUnknown=true;await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 f.hooks.replaceProcess=true;await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false),/不可读|未确认/);
 assert.equal(f.files.get(file),xml);assert.ok(!f.events.includes('normal-close'));
});
await check('off still closes its owned session after external profile removal without inventing other-session runtime proof',async()=>{
 const f=fixture();f.hooks.observeUnknown=true;await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 const old=profiles.matchLsProfile(profiles.parseLsProfiles(f.files.get(file)),'D:\\Games\\a.exe');f.files.set(file,f.files.get(file).slice(0,old.start)+f.files.get(file).slice(old.end));
 const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);assert.equal(result.confirmed,true);assert.equal(result.runtimeConfirmed,false);assert.equal(f.running(),false);
 const other=fixture({running:true,xml:f.files.get(file)});const off=await other.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);assert.equal(off.confirmed,true);assert.equal(off.runtimeConfirmed,null);assert.equal(other.running(),true);assert.ok(!other.events.includes('normal-close'));
});
await check('off with LS already closed disables current profile without launching',async()=>{
 const f=fixture();const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);
 assert.equal(result.confirmed,true);assert.equal(result.runtimeConfirmed,false);assert.equal(f.running(),false);
 assert.equal(profiles.parseLsProfiles(f.files.get(file))[1].title,'B');assert.ok(!f.events.some(e=>e.startsWith('launch:')));
 const before=f.events.length;await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);assert.equal(f.events.filter(e=>e.startsWith('launch:')||e==='normal-close').length,0);
});
await check('off failed normal close preserves profile and backup without force termination',async()=>{
 const f=fixture({running:true,closeDenied:true});f.observe('A','取消缩放');
 await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false),/close to tray/);
 assert.equal(f.files.get(file),xml);assert.ok(!f.files.has(file+'.ymccbak'));assert.equal(f.running(),true);
});
await check('off reads final LS save before disabling only selected block',async()=>{
 const newer=xml.replace('retained','latest-final-save'),f=fixture({running:true,liveXml:newer});f.observe('A','取消缩放');
 await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);
 assert.match(f.files.get(file),/latest-final-save/);assert.equal(f.files.get(file+'.ymccbak'),newer);assert.equal(profiles.parseLsProfiles(f.files.get(file))[1].title,'B');
});
await check('off backup failure or concurrent external update cannot overwrite current XML',async()=>{
 for(const kind of ['backupDenied','sourceChange']){
  const f=fixture({running:true});f.observe('A','取消缩放');f.hooks[kind]=true;
  await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false));
  assert.equal(profiles.parseLsProfiles(f.files.get(file)).length,2);assert.equal(f.running(),false);
 }
});
await check('on reenables only AutoScale while retaining custom scaling fields',async()=>{
 const custom=xml.replace('<AutoScale>true</AutoScale>','<AutoScale>false</AutoScale>').replace('<FrameGeneration>LSFG3</FrameGeneration>','<FrameGeneration>LSFG4-custom</FrameGeneration>'),f=fixture({xml:custom});
 await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 const list=profiles.parseLsProfiles(f.files.get(file));assert.equal(list[0].enabled,true);assert.match(list[0].block,/LSFG4-custom/);assert.equal(list.length,2);
});
await check('LS enable and disable never register a return-game focus target',async()=>{
 const f=fixture(),target={pid:110,processCreated:'4567',path:'D:\\Games\\a.exe'};
 await f.owner.setLosslessScalingEnabled(target.path,true,()=>{},target);
 await f.owner.setLosslessScalingEnabled(target.path,false,()=>{},target);
 const calls=f.events.filter(e=>e.startsWith('focus-target:')).map(e=>JSON.parse(e.slice('focus-target:'.length)));
 assert.deepEqual(calls,[]);
});
await check('warm ON reuses the exact instance without a shell launch or focus target',async()=>{
 const f=fixture({running:true});await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,0);assert.ok(f.events.includes('reuse'));
 assert.ok(!f.events.some(e=>e.startsWith('focus-target:')));
});
await check('AutoScale active Profile is authoritative even while the selected row is another game',async()=>{
 const f=fixture({running:true,selectedTitle:'B'});f.observe('A','无缩放');
 assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).enabled,true);
 await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);
 assert.equal(f.running(),false);assert.equal(profiles.matchLsProfile(profiles.parseLsProfiles(f.files.get(file)),'D:\\Games\\a.exe').enabled,false);
 assert.ok(!f.events.some(e=>e.startsWith('launch:')||e.startsWith('focus-target:')));
});
await check('missing active Profile receipt is unknown, never proof that the selected game is active',async()=>{
 const f=fixture({running:true});f.observe('B','无缩放');f.hooks.statusMissing=true;
 assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).enabled,null);
 await assert.rejects(f.owner.stopLosslessScaling('D:\\Games\\a.exe'),/不可读|未确认/);
 assert.ok(!f.events.includes('normal-close'));assert.equal(f.files.get(file),xml);
});
await check('refused OFF never sends an ON or focus request on status refresh',async()=>{
 const f=fixture({running:true,closeDenied:true});f.observe('A','无缩放');
 await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false),/close to tray/);
 for(let i=0;i<3;i++)assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).configuredEnabled,true);
 assert.ok(!f.events.some(e=>e.startsWith('launch:')||e.startsWith('focus-target:')));
 assert.equal(f.events.filter(e=>e==='normal-close').length,1);
});
await check('same-name matching is case-insensitive, exact-path first, and ambiguous duplicates fail closed',()=>{const list=profiles.parseLsProfiles(xml);assert.equal(profiles.matchLsProfile(list,'d:/games/A.EXE').title,'A');assert.equal(profiles.matchLsProfile(list,'Q:/Moved/a.exe').title,'A');const duplicate=profiles.parseLsProfiles(xml.replace('</GameProfiles>',profile('A2','Q:\\Other\\a.exe')+'</GameProfiles>'));assert.equal(profiles.matchLsProfile(duplicate,'D:/Games/a.exe').title,'A');assert.throws(()=>profiles.matchLsProfile(duplicate,'Z:/Other/a.exe'),/同名 EXE/);});
await check('mirror delegates only to shared original YMCC owner, and both original views use that same entry',async()=>{
 const game={pid:101,processCreated:'native-created',path:'D:\\Games\\a.exe'};let current=game,calls=0,released=0;
 const key={gameMirrorIdentity:g=>g?JSON.stringify(g):''},shared={getLosslessGameState:async()=>({xml,profile:{path:game.path},enabled:false,configuredEnabled:true,controlAvailable:true,running:false}),setLosslessScalingEnabled:async(p,on,checkpoint)=>{checkpoint();assert.equal(p,game.path);assert.equal(on,true);calls++;return {confirmed:true,notice:'original-owner'};},onLosslessScalingChanged:()=>()=>{}};
 const adapter=load(fs.readFileSync(path.join(root,'src/bridge/losslessScaling.ts'),'utf8'),{'./deckyLosslessState':{waitForDeckyLosslessAction:async(run,checkpoint)=>({pending:false,result:await run(checkpoint)}),deckyLosslessText:value=>value},'./api':{powerLifecycle:{get:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true})}},'./quickapp':shared,'./gamePolicyTarget':{getPolicyGame:()=>current},'./deckyGameActions':key,'./quickActionLock':{tryAcquireQuickAction:()=>()=>released++}});
 const context={gameIdentity:JSON.stringify(game),gameAdmission:{lossless:xml},generation:1,checkpoint:()=>{}};assert.equal((await adapter.setLosslessFromMirror({field:'losslessScaling',value:'on'},context)).applied,true);assert.equal(calls,1);assert.equal(released,1);
 current={...game,pid:102};await assert.rejects(adapter.setLosslessFromMirror({field:'losslessScaling',value:'on'},context),/UNCONFIRMED/);assert.equal(calls,1);
 const thin=fs.readFileSync(path.join(root,'src/bridge/losslessScaling.ts'),'utf8');for(const token of ['shell.run','shell.execute','writeTextFile','AutoScale>','UIAutomationClient','InvokePattern'])assert.ok(!thin.includes(token),token);
 for(const p of ['src/components/GameQuickActions.vue','src/views/QuickAppView.vue'])assert.ok(fs.readFileSync(path.join(root,p),'utf8').includes('setLosslessScalingEnabled'));
});
await check('native activation receives the exact current YMCC game PID and birth, without focus calls',async()=>{
 for(const target of [{pid:110,processCreated:'4567',path:'D:\\Games\\a.exe'},{pid:220,processCreated:'8901',path:'D:\\Games\\b.exe'}]){
  const f=fixture();await f.owner.setLosslessScalingEnabled(target.path,true,()=>{},target);
  assert.deepEqual(f.events.filter(e=>e.startsWith('game-target:')).map(e=>JSON.parse(e.slice('game-target:'.length))),[target]);
  assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);assert.ok(!f.events.some(e=>e.startsWith('focus-target:')));
 }
});
await check('supplied path mismatch is rejected; absent PID is an optional path-only activation',async()=>{
 const mismatch=fixture();const result=await mismatch.owner.oneClickFrameGen('D:\\Games\\a.exe',()=>{},{pid:110,processCreated:'4567',path:'D:\\Games\\b.exe'});
 assert.equal(result.activation.reason,'game-identity-unavailable');assert.ok(!mismatch.events.includes('native-ensure-scaling'));
 const missing=fixture();missing.hooks.gameMissing=true;const pathOnly=await missing.owner.oneClickFrameGen('D:\\Games\\a.exe');assert.equal(pathOnly.activation.ok,true);assert.ok(missing.events.includes('native-ensure-scaling'));assert.deepEqual(missing.events.filter(e=>e.startsWith('game-target:')).map(e=>JSON.parse(e.slice('game-target:'.length))),[{path:'D:\\Games\\a.exe'}]);
});
await check('busy native activation and foreground mismatch do not replay launch or trigger focus',async()=>{
 for(const reason of ['scaling-busy','target-not-foreground']){const f=fixture();f.hooks.activationReason=reason;const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);assert.equal(result.runtimeConfirmed,false);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);assert.ok(!f.events.some(e=>e.startsWith('focus-target:')));}
});
await check('two YMCC consumers share one discovery and status receipt; external manual changes invalidate without commands',async()=>{
 const f=fixture({running:true});f.observe('A','取消缩放');const [y,decky]=await Promise.all([f.owner.getLosslessGameState('D:\\Games\\a.exe'),f.owner.getLosslessGameState('D:\\Games\\a.exe')]);
 assert.equal(y.enabled,true);assert.equal(decky.enabled,true);assert.equal(f.events.filter(e=>e==='runtime-detect').length,1);
 for(let i=0;i<100;i++)await f.owner.getLosslessGameState('D:\\Games\\a.exe');assert.equal(f.events.filter(e=>e==='runtime-detect').length,1);
 f.observe('A','缩放');assert.equal((await f.owner.getLosslessGameState('D:\\Games\\a.exe')).enabled,false);
 assert.ok(!f.events.some(e=>e.startsWith('launch:')||e==='native-ensure-scaling'||e==='normal-close'));
});
await check('external startup from another directory is reused; unknown discovery never launches a second instance',async()=>{
 const f=fixture({running:true});f.hooks.runtimePath='E:\\Portable\\LosslessScaling.exe';
 assert.equal((await f.owner.resolveLs()).exe,f.hooks.runtimePath);await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.ok(!f.events.some(e=>e.startsWith('launch:')));assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
 const conflict=fixture();conflict.hooks.runtimeUnknown=true;await assert.rejects(conflict.owner.oneClickFrameGen('D:\\Games\\a.exe'),/实例未确认/);assert.ok(!conflict.events.some(e=>e.startsWith('launch:')));
});
await check('shared cache rejects late reads and older event receipts without inventing OFF',async()=>{
 const f=fixture();let release,calls=0;const pending=new Promise(resolve=>release=resolve);const state=new f.runtime.LosslessStateOwner(()=>{calls++;return pending;});
 const old=state.read(),peer=state.read();const fresh={known:true,running:true,readable:true,pid:81,processCreated:'890',path:exe,button:'取消缩放',status:'actual',reason:'',configRevision:0,observedAt:200};
 state.accept(fresh);state.accept({...fresh,pid:71,observedAt:100});release({...fresh,running:false,observedAt:50});
 assert.equal((await old).pid,81);assert.equal((await peer).pid,81);assert.equal(calls,1);
});
await check('newer OFF revokes pending ON and late activation never reopens or deletes custom parameters',async()=>{
 const f=fixture();let release;f.hooks.activationGate=new Promise(resolve=>release=resolve);
 const on=f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);const rejection=assert.rejects(on,/较新/);
 for(let i=0;i<150&&!f.events.includes('native-ensure-scaling');i++)await Promise.resolve();assert.ok(f.events.includes('native-ensure-scaling'));
 const off=f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false);release();await rejection;
 assert.equal((await off).confirmed,true);assert.equal(f.running(),false);const p=profiles.matchLsProfile(profiles.parseLsProfiles(f.files.get(file)),'D:\\Games\\a.exe');assert.equal(p.enabled,false);assert.match(p.block,/<FrameGeneration>LSFG3<\/FrameGeneration>/);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
});
await check('YMCC owns observation interest once for both consumers, suspends and disposes without retry timers',async()=>{
 const watches=[],handlers=new Map();let gameChanged;
 const runtime=load(fs.readFileSync(path.join(root,'src/bridge/losslessRuntimeState.ts'),'utf8'),{'./api':{losslessRuntime:{detect:async()=>({known:true,running:false}),watch:async(active,detailed)=>{watches.push({active,detailed});return true;}}},'./ipc':{on:(event,cb)=>{handlers.set(event,cb);return()=>handlers.delete(event);}},'./gamePolicyTarget':{subscribePolicyGameStatus:cb=>{gameChanged=cb;cb(null);return()=>{gameChanged=null;};}}});
 const flush=async()=>{for(let i=0;i<40;i++)await Promise.resolve();};
 const stopA=runtime.startLosslessRuntimeOwner(),stopB=runtime.startLosslessRuntimeOwner();await flush();assert.deepEqual(watches.at(-1),{active:false,detailed:false});
 gameChanged({path:'game.exe'});await flush();assert.deepEqual(watches.at(-1),{active:true,detailed:false});
 const y=runtime.retainLosslessObservation(),decky=runtime.retainLosslessObservation();await flush();assert.deepEqual(watches.at(-1),{active:true,detailed:true});
 y();await flush();assert.deepEqual(watches.at(-1),{active:true,detailed:true});
 handlers.get('power.suspending')();await flush();assert.deepEqual(watches.at(-1),{active:false,detailed:false});
 handlers.get('power.resumed')();await flush();assert.deepEqual(watches.at(-1),{active:true,detailed:true});
 decky();await flush();assert.deepEqual(watches.at(-1),{active:true,detailed:false});
 stopA();stopA();assert.ok(gameChanged);stopB();await flush();assert.equal(gameChanged,null);assert.equal(handlers.size,0);assert.deepEqual(watches.at(-1),{active:false,detailed:false});
});
await check('ON needing profile changes cannot close another active or unreadable external session',async()=>{
 for(const unreadable of [false,true]){
  const f=fixture({running:true});f.observe('B','取消缩放');f.hooks.observeUnknown=unreadable;
  await assert.rejects(f.owner.oneClickFrameGen('D:\\Games\\missing.exe'),/另一 EXE|不可读/);
  assert.equal(f.running(),true);assert.equal(f.files.get(file),xml);assert.ok(!f.events.includes('normal-close'));assert.ok(!f.events.some(e=>e.startsWith('write:')));
 }
});
await check('an unreadable manually started instance does not become owned after an unconfirmed ON',async()=>{
 const f=fixture({running:true});f.hooks.observeUnknown=true;const on=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);assert.equal(on.confirmed,false);
 await assert.rejects(f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',false),/不可读|未确认/);
 assert.equal(f.running(),true);assert.ok(!f.events.includes('normal-close'));assert.equal(f.files.get(file),xml);
});

await check('program lamp reads the shared process receipt even without an EXE rule, XML or UIA',async()=>{
 for(const running of [false,true])for(const configuration of ['none','unbound','FSR-only','LSFG-only']){
  const f=fixture({running});f.hooks.observeUnknown=true;
  if(configuration==='none')f.files.delete(file);
  else if(configuration==='unbound')f.files.set(file,'<Settings><GameProfiles><Profile><Title>Manual</Title><Path></Path><AutoScale>false</AutoScale></Profile></GameProfiles></Settings>');
  else if(configuration==='FSR-only')f.files.set(file,xml.replaceAll('<FrameGeneration>LSFG3</FrameGeneration>','<FrameGeneration>NONE</FrameGeneration>'));
  else f.files.set(file,xml.replaceAll('<ScalingType>FSR</ScalingType>','<ScalingType>OFF</ScalingType><FSRType>ORIGINAL</FSRType>'));
  const [program,runtime]=await Promise.all([f.runtime.readLosslessProgram(),f.runtime.readLosslessRuntime()]);
  assert.equal(program.known,true);assert.equal(program.running,running);assert.equal(runtime.running,running);
  assert.equal(f.events.filter(e=>e==='runtime-detect').length,1);
  assert.equal(Object.keys(program).sort().join(','),'known,running');
  assert.ok(!f.events.some(e=>e.startsWith('write:')||e.startsWith('launch:')||e==='normal-close'||e==='native-ensure-scaling'));
 }
});
await check('program lamp never converts an unknown or multiple runtime into a stopped receipt',async()=>{
 const f=fixture({running:true});f.hooks.runtimeUnknown=true;const unknown=await f.runtime.readLosslessProgram();
 assert.equal(unknown.known,false);assert.equal(unknown.running,false);
 f.hooks.runtimeUnknown=false;const recovered=await f.runtime.readLosslessProgram();assert.equal(recovered.known,true);assert.equal(recovered.running,true);
 assert.ok(!f.events.some(e=>e.startsWith('write:')||e.startsWith('launch:')||e==='normal-close'));
});

await check('explicit retry cannot be confirmed by a stale On cache after native failure',async()=>{
 const f=fixture({running:true});f.observe('A','取消缩放');f.hooks.activationReason='restart-stop-unconfirmed';
 const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(result.runtimeConfirmed,null);assert.match(result.notice,/停止未获确认/);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
 assert.equal(f.files.get(file),xml);assert.ok(!f.events.includes('normal-close'));
});
await check('explicit retry rebuilds current profile without changing XML, closing process or sending focus',async()=>{
 const f=fixture({running:true});f.observe('A','取消缩放');const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(result.confirmed,true);assert.match(result.notice,/重建本次会话/);assert.equal(f.events.filter(e=>e==='native-reapply').length,1);
 assert.equal(f.files.get(file),xml);assert.equal(f.running(),true);assert.ok(!f.events.some(e=>e.startsWith('focus-target:')||e==='normal-close'));
});
const scriptDir=path.join(task,'Build/LS-Owner-Scripts');fs.mkdirSync(scriptDir,{recursive:true});for(const [index,script] of [...observedScripts].entries())fs.writeFileSync(path.join(scriptDir,'ls-owner-'+index+'.ps1'),script);
// The user's two real imports are only read here. No applications or games are launched.
await check('shared explicit owner requests one native window refresh and reports its receipt',async()=>{
 const f=fixture();const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(result.confirmed,true);assert.match(result.notice,/已提交游戏前台请求/);assert.equal(f.events.filter(e=>e==='native-window-refresh').length,1);
});
await check('denied native window refresh is not confirmed by cached active profile',async()=>{
 const f=fixture();await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);f.events.length=0;f.hooks.activationReason='window-refresh-activation-denied';
 const result=await f.owner.setLosslessScalingEnabled('D:\\Games\\a.exe',true);
 assert.equal(result.confirmed,false);assert.match(result.notice,/系统未允许窗口刷新/);assert.equal(f.events.filter(e=>e==='native-window-refresh').length,0);
});
await check('denied redraw request cannot be confirmed from stale active state',async()=>{
 const f=fixture();await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);f.events.length=0;f.hooks.activationReason='window-redraw-request-denied';
 const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.match(result.notice,/未接受原生重绘请求/);assert.doesNotMatch(result.notice,/已应用/);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
});
await check('denied frame request cannot be confirmed from stale active state',async()=>{
 const f=fixture();await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);f.events.length=0;f.hooks.activationReason='window-frame-request-denied';
 const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.match(result.notice,/未接受原生窗口框架刷新请求/);assert.doesNotMatch(result.notice,/已应用/);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
});
await check('returned paused activation preserves instance and XML; next explicit click still works',async()=>{
 const f=fixture();f.hooks.activationReason='scaling-paused';
 const first=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(first.confirmed,false);assert.equal(first.pending,false);assert.equal(f.running(),true);assert.match(first.notice,/已保留小黄鸭运行/);assert.equal(f.files.get(file),xml);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,0);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);
 f.hooks.activationReason='';const second=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);assert.equal(second.confirmed,true);assert.equal(f.running(),true);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);
});
await check('denied timeout reset ends UI wait without claiming closed or retrying',async()=>{
 const f=fixture();f.hooks.timeoutOnce=true;f.hooks.resetDenied=true;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(result.pending,false);assert.equal(f.running(),true);assert.match(result.notice,/强制结束小黄鸭未获确认/);assert.doesNotMatch(result.notice,/已清理/);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,1);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);assert.equal(f.files.get(file),xml);
});
await check('returned pause, foreground loss, unreadable or provider errors never auto-kill or auto-retry',async()=>{
 for(const reason of ['scaling-paused','target-not-foreground','scaling-state-unavailable','no-focus-automation-unavailable','window-frame-request-denied']){const f=fixture();f.hooks.activationReason=reason;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);assert.equal(result.confirmed,false);assert.equal(result.pending,false);assert.equal(f.running(),true);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,0);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);assert.equal(f.files.get(file),xml);}
});
await check('unreturned native action uses independent timeout reset then one successful retry',async()=>{
 const f=fixture();f.hooks.timeoutOnce=true;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,true);assert.match(result.notice,/已自动重试 1 次/);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,1);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,2);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,2);
});
await check('two unreturned native actions stop after two exact watchdog resets',async()=>{
 const f=fixture();f.hooks.timeoutEvery=true;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(result.pending,false);assert.equal(f.running(),false);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,2);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,2);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,2);
});
await check('denied method3 foreground request cannot use stale active profile to confirm',async()=>{
 const f=fixture();f.hooks.activationReason='window-foreground-request-denied';
 const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.match(result.notice,/未接受游戏前台请求/);assert.doesNotMatch(result.notice,/已应用/);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);
});
await check('a paused reply on the one timeout retry preserves the new instance',async()=>{
 const f=fixture();f.hooks.timeoutOnce=true;f.hooks.activationReason='scaling-paused';const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(result.pending,false);assert.equal(f.running(),true);assert.match(result.notice,/自动重试 1 次/);assert.match(result.notice,/已保留小黄鸭运行/);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,1);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,2);
});
await check('legacy reset metadata on an ordinary reply cannot authorize another launch',async()=>{
 const f=fixture();f.hooks.activationReason='scaling-paused';f.hooks.legacyResetReceipt=true;const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.equal(f.events.filter(e=>e==='native-ensure-scaling').length,1);assert.equal(f.events.filter(e=>e==='native-timeout-reset').length,0);assert.equal(f.events.filter(e=>e.startsWith('launch:')).length,1);
});
await check('missing window-refresh receipt cannot confirm the explicit request',async()=>{
 const f=fixture();f.hooks.omitRefreshReceipt=true;
 const result=await f.owner.setLosslessScalingEnabled('D:/Games/a.exe',true);
 assert.equal(result.confirmed,false);assert.doesNotMatch(result.notice,/已应用/);
});
const actualPath=path.join(process.env.LOCALAPPDATA,'Lossless Scaling/Settings.xml'),hash=v=>crypto.createHash('sha256').update(v).digest('hex');
let realListReadOnly={available:false};
if(fs.existsSync(actualPath)){
 const bytes=fs.readFileSync(actualPath),actual=profiles.parseLsProfiles(bytes.toString('utf8')),realGames=actual.filter(p=>p.path);
 if(process.argv.includes('--require-two-real-exes')){assert.equal(realGames.length,2);for(const key of ['cyberpunk2077.exe','warno.exe'])assert.ok(realGames.some(p=>profiles.lsExeKey(p.path)===key));}
 assert.equal(hash(fs.readFileSync(actualPath)),hash(bytes));realListReadOnly={available:true,path:actualPath,sha256:hash(bytes),profileCount:actual.length};
}
const report={pass:true,stage:'Lossless-Toggle-Xbox-20261022',cases:results.length,results,
 sourceHashes:Object.fromEntries(['src/bridge/quickapp.ts','src/bridge/ipc.ts','src/bridge/api.ts','src/bridge/losslessProfiles.ts','src/bridge/losslessScaling.ts','src/bridge/deckyLosslessState.ts','tools/lossless_scaling_selftest.mjs'].map(name=>[name,hash(fs.readFileSync(path.join(root,name)))])),
 exactOriginalOwnerExecuted:true,sideEffectBoundaries:'inert memory fs/process/shell; not live YMCC acceptance',realListReadOnly,actualProductYmccExecuted:false,actualLSControlActions:0,realSettingsWrites:0,realGameLaunches:0};
fs.writeFileSync(path.join(task,'validation/MAINLINE20-LOSSLESS-OWNER.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
