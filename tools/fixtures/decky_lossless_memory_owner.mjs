// Exact original quickapp owner; file/process/shell boundaries are strictly in memory.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {root} from './decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
function load(text,modules={}){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,Uint8Array,btoa:value=>Buffer.from(value,'binary').toString('base64'),require:name=>{assert.ok(Object.hasOwn(modules,name),'Unmocked original LS dependency '+name);return modules[name];}});return module.exports;}
export function createOriginalLosslessMemoryOwner({withProfile=true,uiReadable=true,initialRunning=false,settingsXml}={}){
 const profiles=load(fs.readFileSync(path.join(root,'src/bridge/losslessProfiles.ts'),'utf8'));
 const file='Q:\\LocalData\\Lossless Scaling\\Settings.xml',exe='C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
 const xml='<Settings><Unknown>retained</Unknown><GameProfiles><Profile><Title>Game</Title><Path>C:\\Games\\Game.exe</Path><AutoScale>true</AutoScale><ScalingType>FSR</ScalingType><FrameGeneration>LSFG3</FrameGeneration></Profile></GameProfiles></Settings>';
 const initialXml=settingsXml??(withProfile?xml:"<Settings>\n  <Unknown>retained</Unknown>\n  <GameProfiles>\n  </GameProfiles>\n</Settings>");let memoryWrites=0;
 const files=new Map([[file,initialXml],[exe,'inert-asset']]),events=[];let running=initialRunning,title='Game',button='缩放',launchDenied=false,closeDenied=false,pidValue=71,birth='123';const returnTargets=[];let failures=0,timeoutFailure=false,failureReason='scaling-paused';
 const api={losslessRuntime:{detect:async()=>({known:true,running,readable:running&&uiReadable,pid:running?pidValue:0,processCreated:birth,path:exe,button,status:'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"',reason:'',configRevision:0}),watch:async()=>true,ensureRunning:async p=>{const started=!running;assert.equal(p,exe);if(launchDenied){events.push('original-launch-refused');return {ok:false,reason:'denied',win32Error:5};}if(!running){events.push('original-launch');const selected=profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe');if(selected?.title)title=selected.title;running=true;button='缩放';}return {ok:true,started,pid:pidValue,processCreated:birth,path:exe};},observe:async identity=>{assert.equal(identity.exe,exe);events.push('readonly-status');return uiReadable?{ok:true,button,status:'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"'}:{ok:false,button:'',status:''};},resetFailed:async request=>{assert.equal(request.exe,exe);assert.equal(request.pid,pidValue);assert.equal(request.processCreated,birth);events.push('original-timeout-reset');running=false;return {failureResetRequested:true,failureResetAttempted:true,failureResetTerminated:true,failureResetClosed:true};},ensureScaling:async request=>{assert.equal(request.exe,exe);assert.equal(request.pid,pidValue);assert.equal(request.processCreated,birth);assert.ok((request.gamePid===undefined&&request.gameProcessCreated===undefined)||(request.gamePid>0&&request.gameProcessCreated));events.push('native-ensure-scaling');assert.equal(request.resetOnFailure,false);assert.ok(request.retryAttempt===0||request.retryAttempt===1);if(failures>0){failures--;if(timeoutFailure)throw Object.assign(new Error('IPC command timed out: lossless.ensureScaling'),{code:'IPC_TIMEOUT',command:'lossless.ensureScaling'});button='取消缩放';title=request.profileTitle;return {ok:false,enabled:null,invoked:true,pending:true,cancelled:failureReason==='target-not-foreground',reason:failureReason};}if(!uiReadable)return {ok:false,enabled:null,invoked:false,pending:false,cancelled:false,reason:'no-focus-automation-unavailable'};const already=button==='取消缩放';assert.equal(request.reapply,true);assert.equal(request.refreshWindow,true);title=request.profileTitle;button='取消缩放';return {windowRefreshed:true,refreshAttempted:true,ok:true,enabled:true,invoked:!already||request.reapply,pending:false,cancelled:false,restarted:already&&request.reapply,stopInvoked:already&&request.reapply,reason:''};}},fs:{exists:async p=>files.has(p),readTextFile:async p=>{assert.ok(files.has(p),'Undeclared memory LS file');return files.get(p);},writeTextFileAtomic:async(p,text)=>{assert.ok(p===file||p===file+'.ymccbak');if(p===file){assert.equal(running,false,'Original settings writes require LS exit');memoryWrites++;}files.set(p,text);}},registry:{read:async()=>null},proc:{findExact:async p=>({found:p===exe&&running,pid:running?pidValue:0}),identity:async pid=>({valid:running&&pid===pidValue,pid,processCreated:birth,path:exe})},shell:{execute:async(p,args)=>{assert.equal(p,'cmd');assert.equal(args[0],'/c');assert.equal(args[1],'start');assert.ok(args.includes(exe));if(args.includes('-auto')){assert.ok(args.includes('-path'));assert.ok(args.some(v=>/game\.exe$/i.test(v)));}if(launchDenied){events.push('original-launch-refused');return false;}events.push('original-launch');const selected=profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe');if(selected?.title)title=selected.title;running=true;button='取消缩放';return true;},run:async(p,args)=>{
  assert.equal(p,'powershell');if(args.includes('Write-Output $env:LOCALAPPDATA'))return {exitCode:0,stdout:'Q:\\LocalData',stderr:''};
  const source=args.includes('-EncodedCommand')?Buffer.from(args.at(-1),'base64').toString('utf16le'):args.join(' ');
  if(source.includes('YMCC_LOSSLESS_CLOSE')){assert.match(source,/GetProcessTimes/);assert.match(source,/PostMessage/);events.push('original-normal-close');if(closeDenied)return {exitCode:1,stdout:'',stderr:'normal close refused'};running=false;button='缩放';return {exitCode:0,stdout:'',stderr:''};}
  if(source.includes('原开启动作不重发')){events.push('bounded-start-observation');return {exitCode:0,stdout:'',stderr:''};}
  if(source.includes('UIAutomationClient')){assert.ok(!source.includes('.Invoke('));events.push('readonly-status');if(!uiReadable)return {exitCode:1,stdout:'',stderr:'UIAutomation unavailable'};return {exitCode:0,stdout:JSON.stringify({title,button,status:'Scaling: "window"\nResolution: fixture\nProfile: "'+title+'"'}),stderr:''};}
  throw Error('Undeclared actual LS shell operation');
 }},windowApi:{losslessReturnTarget:async(target,enabled)=>{returnTargets.push({target:structuredClone(target),enabled});return {ok:true};}}};
 const runtime=load(fs.readFileSync(path.join(root,'src/bridge/losslessRuntimeState.ts'),'utf8'),{'./api':api,'./ipc':{on:()=>()=>{}},'./gamePolicyTarget':{subscribePolicyGameStatus:()=>()=>{}}});
 const owner=load(fs.readFileSync(path.join(root,'src/bridge/quickapp.ts'),'utf8'),{'./api':api,'./losslessProfiles':profiles,'./losslessRuntimeState':runtime,'./gamedetect':{},'./optiscalerClientSync':{syncOptiClientGame:()=>{throw Error('Unrelated OptiScaler action');},flushOptiClientGames:()=>{throw Error('Unrelated OptiScaler action');}}});
 return {owner,setObservedRuntime:next=>{if('running' in next)running=next.running;if('title' in next)title=next.title;if('button' in next)button=next.button;runtime.invalidateLosslessRuntime();},failActivations:(count,timeout=false,reason='scaling-paused')=>{failures=count;timeoutFailure=timeout;failureReason=reason;},readLosslessProgram:runtime.readLosslessProgram,setUiReadable:value=>{assert.equal(typeof value,'boolean');uiReadable=value;runtime.invalidateLosslessRuntime();},setCloseDenied:value=>{assert.equal(typeof value,'boolean');closeDenied=value;},replaceProcess:({pid=pidValue,processCreated=birth}={})=>{pidValue=pid;birth=processCreated;runtime.invalidateLosslessRuntime();},readMemoryXml:()=>files.get(file),setLaunchDenied:value=>{assert.equal(typeof value,'boolean');launchDenied=value;},stats:()=>({running,events:[...events],returnTargets:structuredClone(returnTargets),profileXmlUnchanged:files.get(file)===initialXml,memoryProfileWrites:memoryWrites,profilePresent:!!profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe'),profileEnabled:profiles.matchLsProfile(profiles.parseLsProfiles(files.get(file)),'C:\\Games\\Game.exe')?.enabled===true,realFilesWritten:0,realProcessesStarted:0,physicalHardwareWrites:0})};
}

// Compile the actual Y-panel SFC and render its real button VNodes. Unrelated
// page components and OS boundaries stay inert; Vue refs/computed/watch are real.
export function createLosslessTopActions(ownerFixture,game,speedOwner=null){
 const require=createRequire(path.join(root,'package.json')),vue=require('vue'),{parse,compileScript,compileTemplate}=require('@vue/compiler-sfc');
 const source=fs.readFileSync(path.join(root,'src/components/GameQuickActions.vue'),'utf8');
 const {descriptor,errors}=parse(source);assert.deepEqual(errors,[]);
 const script=compileScript(descriptor,{id:'lossless-top-actions'});
 const template=compileTemplate({id:'lossless-top-actions',source:descriptor.template.content,compilerOptions:{bindingMetadata:script.bindings}});
 assert.deepEqual(template.errors,[]);const events=[],scope=vue.effectScope();let locked={...game,lockGeneration:1};
 const originalSession=load(fs.readFileSync(path.join(root,'src/bridge/gameQuickSession.ts'),'utf8'),{'./gamedetect':{}});
 const same=originalSession.isGameTargetSame;
 const program=load(fs.readFileSync(path.join(root,'src/bridge/losslessProgramState.ts'),'utf8'));
 const modules={vue:{...vue,onMounted(){},onBeforeUnmount(){}},
   '@/bridge/mouseModeState':load(fs.readFileSync(path.join(root,'src/bridge/mouseModeState.ts'),'utf8'),{vue,'./mouseModePresentation':load(fs.readFileSync(path.join(root,'src/bridge/mouseModePresentation.ts'),'utf8'))}),
  '@/bridge/losslessProgramState':program,'@/bridge/losslessRuntimeState':{retainLosslessObservation:()=>()=>{},readLosslessProgram:ownerFixture.readLosslessProgram},
  '@/bridge/quickapp':ownerFixture.owner,'@/bridge/gamePolicyTarget':{getPolicyGame:()=>game},
  '@/bridge/gamedetect':{isCurrentGameTarget:target=>same(target,game),getCurrentGame:()=>game,detectedGameName:g=>g?.name??'Game'},
  '@/bridge/gameQuickSession':{getLockedGameTarget:()=>locked,isGameTargetSame:same,captureAndLockGameTarget:async target=>same(target,game)?locked:null},
  '@/bridge/quickActionLock':{tryAcquireQuickAction:()=>()=>{}},
  '@/bridge/gameTrainer':{getReadyGameTrainer:()=>null,getGameTrainerState:()=>null},
  '@/bridge/speedhack':speedOwner??{SPEED_PRESETS:[1]},'@/bridge/gameproc':{},'@/bridge/api':{},
  '@/bridge/optiImportPolicy':{},'@/gamepad/focus':{},'@/gamepad/engine':{},'@/gamepad/activationEvidence':{}};
 function compile(text){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,require:name=>{
  if(Object.hasOwn(modules,name))return modules[name];
  if(name.endsWith('.vue'))return {render:()=>null};throw Error('Unmocked Y-panel dependency '+name);
 }});return module.exports;}
 const component=compile(script.content).default,render=compile(template.code).render;
 const props={open:true,game};let raw;scope.run(()=>{raw=component.setup(props,{expose(){},emit:(...args)=>events.push(args)});});
 const setup=vue.proxyRefs(raw);function nodes(node,out=[]){if(Array.isArray(node))node.forEach(n=>nodes(n,out));else if(node&&typeof node==='object'){out.push(node);nodes(node.children,out);}return out;}
 const tree=()=>render({...setup,...props},[],props,setup,{},{}),controls=()=>nodes(tree()).filter(n=>n.type==='button');
 const find=id=>controls().find(n=>n.props?.['data-gp-game-control']===id);
 const text=node=>Array.isArray(node)?node.map(text).join(''):node&&typeof node==='object'?text(node.children):node==null?'':String(node);

 const ts=require('typescript'),engine=fs.readFileSync(path.join(root,'src/gamepad/engine.ts'),'utf8');
 const engineAst=ts.createSourceFile('engine.ts',engine,ts.ScriptTarget.Latest,true);
 const nav=engineAst.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='moveGameMenuFocus');assert(nav);
 function navigate(id,dy){
  const elements=nodes(tree()).filter(n=>(n.type==='button'&&n.props?.['data-gp-game-row']||n.props?.['gp-row'])&&!n.props.disabled).map(n=>{
   const row=n.props['data-gp-game-row']??'actions-'+n.props['gp-row'],col=n.props['data-gp-col']??n.props['gp-col'];
   const el={id:n.props['data-gp-game-control']??(n.props['gp-row']?'speed-factor':''),dataset:{gpGameRow:row,gpCol:col},closest:()=>el,getBoundingClientRect:()=>({left:Number(col)*100,width:90})};return el;
  });
  const menu={contains:el=>elements.includes(el),querySelector:()=>null},module={exports:{}};
  vm.runInNewContext(transformSync(nav.getText(engineAst)+'\nmodule.exports=moveGameMenuFocus;',{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,focusables:()=>elements,gameMenuControlForRow:(_,row)=>elements.find(e=>e.dataset.gpGameRow===row)});
  return module.exports(menu,elements.find(e=>e.id===id),dy)?.id;
 }
 return {raw,events,refresh:()=>raw.refreshLosslessState(),find,text,navigate,stop:()=>scope.stop()};
}
