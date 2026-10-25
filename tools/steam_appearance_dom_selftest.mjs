// Real PS5.1 HTTP/WebSocket and actual browser execution of the production
// receipt expression against bundled CSS. No installed Steam or CSS process.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import http from 'node:http';import crypto from 'node:crypto';
import {execFile} from 'node:child_process';import {promisify} from 'node:util';import {createRequire} from 'node:module';
const exec=promisify(execFile),root=process.cwd(),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_fixture.cjs')('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const context=await browser.newContext();await context.route('**/*',route=>route.abort());
const pack=path.join(root,'PowerControl/steam-appearance'),manifest=JSON.parse(fs.readFileSync(path.join(pack,'manifest.json'),'utf8'));const names=manifest.themes.map(row=>row.name);
const definitions=Object.fromEntries(names.map(name=>[name,{manifest:JSON.parse(fs.readFileSync(path.join(pack,'themes',name,'theme.json'),'utf8')),config:JSON.parse(fs.readFileSync(path.join(pack,'themes',name,'config_USER.json'),'utf8'))}]));
function configFor(name,opts,render=false){const c={...definitions[name].config,...opts.configOverrides?.[name],...(render?opts.renderConfigOverrides?.[name]:{})};if(opts.missingConfigKey?.name===name)delete c[opts.missingConfigKey.key];if(opts.disabled)c.active=false;return c;}
// Independent expected-selection fixture: it uses actual theme/config values,
// while the plan under test is produced by the exact PowerShell implementation.
function selectedFiles(name,config,family='SP'){const m=definitions[name].manifest,set=new Set();const collect=map=>{for(const [file,tabs] of Object.entries(map??{}))if(!file.startsWith('--')&&Array.isArray(tabs)&&tabs.includes(family))set.add(file)};collect(m.inject);for(const [label,patch] of Object.entries(m.patches??{}))collect(patch.values[Object.hasOwn(config,label)?config[label]:patch.default]);return [...set].map(file=>name+'/'+file);}
function selectedVariables(name,config,family='SP'){const m=definitions[name].manifest,vars={};const collect=map=>{for(const[key,tokens]of Object.entries(map??{}))if(key.startsWith('--')&&tokens.slice(1).includes(family))vars[key]=tokens[0];};collect(m.inject);for(const[label,patch]of Object.entries(m.patches??{}))collect(patch.values[Object.hasOwn(config,label)?config[label]:patch.default]);return vars;}
function variableCss(name,config,opts={},id='',family='SP'){const vars={...(config.active===false?{}:selectedVariables(name,config,family)),...(family==='SP'?opts.variableOverrides?.[name]:{}),...opts.targetVariableOverrides?.[id]?.[name]};for(const key of [...(family==='SP'?(opts.missingVariables??[]):[]),...(opts.targetMissingVariables?.[id]??[])])delete vars[key];return ':root{'+Object.entries(vars).map(([key,value])=>key+':'+value+';').join('')+'}';}
const defaultPatchFiles=names.flatMap(name=>selectedFiles(name,definitions[name].config)).filter(file=>!file.endsWith('/shared.css'));
const sourceHashes=Object.fromEntries(names.map(name=>{const file=path.join(pack,'themes',name,'shared.css');return [path.relative(root,file).replaceAll('\\','/'),crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')]}));
for(const relative of ['PowerControl/steam-appearance/idle.ps1','PowerControl/steam-appearance.ps1','PowerControl/steam-appearance/manifest.json'])sourceHashes[relative]=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,relative))).digest('hex');
let scenario={},port=0,queries=0,frames=0,expressions=[],themeWrites=[],visualSnapshots=[];const sockets=new Set(),pagesByTarget=new Map(),failures=[];
const familyFor=(id)=>{const tab=(scenario.pages??[]).find(p=>p.id===id);const title=tab?.title||(id==='popup'?'QuickAccess':'');return /^MainMenu(?:_uid\d+)?$/.test(title)?'MainMenu':/^QuickAccess(?:_uid\d+)?$/.test(title)?'QuickAccess':'SP'};
const page=(id,title='Steam Big Picture')=>({id,type:'page',title,url:'about:blank?browserType=3',webSocketDebuggerUrl:`ws://127.0.0.1:${port}/devtools/page/${id}`});
const server=http.createServer((req,res)=>{
 if(req.url==='/fixture/apply'){
  let text='';req.on('data',b=>{text+=b;if(text.length>4096)req.destroy()});req.on('end',()=>{void(async()=>{
   const request=JSON.parse(text);assert(names.includes(request.name));assert.equal(typeof request.state,'boolean');themeWrites.push({name:request.name,state:request.state});
   if(!scenario.keepMissing){
    for(const [id,target]of pagesByTarget){const family=familyFor(id),files=request.state?selectedFiles(request.name,configFor(request.name,scenario),family):[],content=files.map(file=>({file,css:fs.readFileSync(path.join(pack,'themes',file),'utf8')}));if(request.state)content.push({file:'variables',css:variableCss(request.name,configFor(request.name,scenario),scenario.keepVariables?scenario:{},id,family)});
     await target.evaluate(({name,files})=>{for(const old of [...document.querySelectorAll('style[data-fixture-theme]')])if(old.getAttribute('data-fixture-theme')===name)old.remove();for(const file of files){const style=document.createElement('style');style.className='css-loader-style';style.setAttribute('data-fixture-theme',name);style.textContent=file.css;document.head.appendChild(style);}}, {name:request.name,files:content});
    }
   }
   res.writeHead(200,{'Content-Type':'application/json'});res.end('{"ok":true}');
  })().catch(error=>{failures.push(String(error));res.writeHead(500);res.end()})});return;
 }
 if(req.url!=='/json/list'){res.writeHead(404);res.end();return;}++queries;
 let pages=scenario.pages??[page('main'),{...page('popup','QuickAccess'),url:'about:blank?popup=1'},{...page('shared','SharedJSContext'),url:'https://steamloopback.host/'}];
 if(scenario.badUrl)pages[0].webSocketDebuggerUrl='ws://example.invalid:8080/devtools/page/main';
 res.writeHead(200,{'Content-Type':'application/json; charset=utf-8'});res.end(JSON.stringify(pages));
});
function send(socket,value){if(socket.destroyed)return;const body=Buffer.from(JSON.stringify(value));assert(body.length<=65535);let header;if(body.length<126)header=Buffer.from([0x81,body.length]);else{header=Buffer.alloc(4);header[0]=0x81;header[1]=126;header.writeUInt16BE(body.length,2)}socket.write(Buffer.concat([header,body]));}
async function renderTarget(id){
 if(pagesByTarget.has(id))return pagesByTarget.get(id);
 const created=await context.newPage();pagesByTarget.set(id,created);const family=familyFor(id);
 let included=scenario.included??(scenario.disabled?[]:names);
 included=included.filter(name=>!(scenario.missing??[]).includes(name)&&!(scenario.targetMissing?.[id]??[]).includes(name));
 let files=included.flatMap(name=>selectedFiles(name,configFor(name,scenario,true),family));
 files=files.filter(file=>!(scenario.missingFiles??[]).includes(file)&&!(scenario.targetMissingFiles?.[id]??[]).includes(file));files.push(...(scenario.extraFiles??[]),...(scenario.targetExtraFiles?.[id]??[]));
  let css=files.map(file=>{
  const current=fs.readFileSync(path.join(pack,'themes',file),'utf8');let body=current;
  if((scenario.staleContentFiles??[]).includes(file)){body=current.replace(/--ymcc-content-[0-9a-f]{16}/g,'--ymcc-content-0000000000000000');if(file==='Round/round-outline.css')body=body.replaceAll('animation: none !important;','animation: fixture_twitch 1.2s ease-in-out infinite;')+'\n@keyframes fixture_twitch {50%{opacity:.25}}';}
  if((scenario.duplicateWithinStyle??[]).includes(file))body+=body;
  const copies=(scenario.duplicateFiles??[]).includes(file)?2:1;
  return Array.from({length:copies},()=>'<style class="css-loader-style" data-fixture-theme="'+file.split('/')[0]+'">'+body+'</style>').join('\n');
 }).join('\n');
 for(const name of new Set([...included,...Object.keys(scenario.variableOverrides??{})])){const value=variableCss(name,included.includes(name)?configFor(name,scenario,true):{active:false},scenario,id,family);css+=`<style class="css-loader-style" data-fixture-theme="${name}">${value}</style>`;}
 const extras=scenario.extraStyles??0;for(let i=0;i<extras;i++)css+='<style class="css-loader-style">.fixture-unrelated { color: red; }</style>';
 await created.setContent('<!doctype html><html><head>'+css+'</head><body class="BasicUI"><div>Owned CSS fixture</div><div id="proof-round" class="DialogInput">Actual rounded surface</div><div id="proof-ring" class="focusring_FocusRing_1IZrQ">Actual focus border</div><div id="proof-carousel" class="basicgamecarousel_BasicGameCarousel_3MdH5">Actual carousel</div></body></html>');
 return created;
}
async function reply(req,socket,request){
 assert.equal(request.method,'Runtime.evaluate');assert.equal(request.params.returnByValue,true);assert.equal(typeof request.params.expression,'string');++frames;expressions.push(request.params.expression);
 if(scenario.unrelated)send(socket,{method:'Runtime.consoleAPICalled'});
 if(scenario.rpcError){send(socket,{id:request.id,error:{code:-32000,message:'fixture rejected'}});return;}
 if(scenario.targetRpcError?.includes(req.url.split('/').at(-1))){send(socket,{id:request.id,error:{code:-32000,message:'target closed'}});return;}
 const target=await renderTarget(req.url.split('/').at(-1));
 visualSnapshots.push({target:familyFor(req.url.split('/').at(-1)),...await target.evaluate(()=>({roundRadius:getComputedStyle(document.getElementById('proof-round')).borderRadius,ringAnimation:getComputedStyle(document.getElementById('proof-ring')).animationName,carouselTransform:getComputedStyle(document.getElementById('proof-carousel')).transform,scaleValue:getComputedStyle(document.documentElement).getPropertyValue('--scale-value').trim()}))});
 let value;try{value=await target.evaluate(request.params.expression);}catch(error){send(socket,{id:request.id,result:{exceptionDetails:{text:String(error)},result:{type:'undefined'}}});return;}
 if(scenario.badProtocol&&value&&typeof value==='object')value.protocol=99;
 if(scenario.notReady&&value&&typeof value==='object')value.ready=false;
 if(scenario.truncatedValues&&value?.values)value.values.pop();
 if(scenario.nonStringValue&&value?.values)value.values[0]=true;
 if(scenario.booleanProtocol&&value&&typeof value==='object')value.protocol=true;
 if(scenario.exceptionDetails){send(socket,{id:request.id,result:{exceptionDetails:{text:'upstream swallowed JS evaluation error'},result:{type:'object',value}}});return;}
 send(socket,{id:request.id,result:{result:{type:scenario.wrongType?'string':typeof value,value:scenario.wrongType?String(value):value}}});
}
server.on('upgrade',(req,socket)=>{
 const key=req.headers['sec-websocket-key'];if(!key){socket.destroy();return;}
 socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64')+'\r\n\r\n');
 sockets.add(socket);socket.on('close',()=>sockets.delete(socket));socket.on('error',()=>{});let pending=Buffer.alloc(0);
 socket.on('data',chunk=>{pending=Buffer.concat([pending,chunk]);while(pending.length>=2){
  const op=pending[0]&15,masked=Boolean(pending[1]&128);let size=pending[1]&127,at=2;
  if(size===126){if(pending.length<4)return;size=pending.readUInt16BE(2);at=4;}if(size===127){socket.destroy();return;}
  const maskAt=at;if(masked)at+=4;if(pending.length<at+size)return;const payload=Buffer.from(pending.subarray(at,at+size));if(masked)for(let i=0;i<size;i++)payload[i]^=pending[maskAt+(i%4)];pending=pending.subarray(at+size);
  if(op===8){socket.end();return;}if(op!==1)continue;
  void reply(req,socket,JSON.parse(payload)).catch(error=>{failures.push(String(error));socket.destroy()});
 }});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));port=server.address().port;
const cases=[];
async function probe(name,opts={},moduleFile=process.env.YMCC_DOM_MODULE_OVERRIDE||path.join(pack,'idle.ps1')){
 for(const p of pagesByTarget.values())await p.close();pagesByTarget.clear();scenario={...opts};queries=0;frames=0;expressions=[];themeWrites=[];visualSnapshots=[];
 const module=fs.readFileSync(moduleFile,'utf8').replaceAll('8080',String(port)),target=path.join(out,name+'-idle.ps1');fs.writeFileSync(target,'\ufeff'+module.replace(/^\ufeff/,''));
 const wrapper=path.join(out,name+'.ps1'),result=path.join(out,name+'.json');
 const inputs=names.map(name=>({name,manifest:definitions[name].manifest,config:configFor(name,opts)}));if(opts.badSnapshot)inputs.pop();if(opts.badConfigured)inputs[0].config.active='true';
 const encoded=Buffer.from(JSON.stringify(inputs),'utf8').toString('base64');
 const ps=String.raw`$ErrorActionPreference='Stop'
. '__MODULE__'
$script:AppearanceContentManifestFile='__CONTENT_MANIFEST__'
function Get-AppearanceProcessPath($p){$p.Path}
function Get-NetTCPConnection {param($State,$LocalPort) @([pscustomobject]@{LocalAddress='__ADDRESS__';OwningProcess=42})}
function Get-Process {param($Id) [pscustomobject]@{Path='C:\fixture\Steam\__IMAGE__';SessionId=1}}
function Test-CurrentUserProcess($p){__OWNER__}
$inputs=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__STATUS__'))|ConvertFrom-Json
$status=[pscustomobject]@{themes=@(foreach($item in $inputs){
 $plan=$null;$planError=''
 try {if(Get-Command Get-AppearanceThemeRenderPlan -ErrorAction SilentlyContinue){$plan=Get-AppearanceThemeRenderPlan $item.name $item.manifest $item.config;if(__BAD_CONTEXT_PLAN__){$plan.contextProtocol=$true}}}catch{$planError=$_.Exception.Message}
 [pscustomobject]@{name=$item.name;installed=$true;configured=$item.config.active;error=$planError;renderPlan=$plan}
})}
$session=[pscustomobject]@{root='C:\fixture\Steam';pid=42;birth='100'}
$timer=[Diagnostics.Stopwatch]::StartNew()
if(__WRAPPER__){$observed=$null;$value=Test-AppearanceRendered $session $status}elseif(Get-Command Get-AppearanceRenderedStatus -ErrorAction SilentlyContinue){$observed=Get-AppearanceRenderedStatus $session $status;$value=$observed.confirmed}else{$observed=$null;$value=Test-AppearanceRendered $session $status}

$repairResult=$null;$repairMessage=''
if(__REPAIR__){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile('__CONTROL__',[ref]$tokens,[ref]$errors)
 if($errors.Count){throw $errors[0]}
 foreach($name in @('Sync-AppearanceBackend','Invoke-AppearanceControl')){
  $fn=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
  Invoke-Expression $fn.Extent.Text
 }
 $status|Add-Member -NotePropertyName revision -NotePropertyValue ('b'*64)
 $status|Add-Member -NotePropertyName targetId -NotePropertyValue ('a'*16)
 $status|Add-Member -NotePropertyName backendReady -NotePropertyValue $true
 $status|Add-Member -NotePropertyName configuredMatchesBackend -NotePropertyValue $true
 $status|Add-Member -NotePropertyName packOwned -NotePropertyValue $true
 $status|Add-Member -NotePropertyName packPrepared -NotePropertyValue $true
 $script:ThemeNames=@($status.themes|ForEach-Object name);$script:FixtureBackend=@{}
 foreach($row in $status.themes){$script:FixtureBackend[$row.name]=[bool]$row.configured}
 function New-AppearanceMutex($Purpose){New-Object Threading.Mutex($false,('Local\YMCC.DomFixture.'+[guid]::NewGuid().ToString('N')))}
 function Get-AppearanceStatus {$status}
 $script:EditorChecks=0
 function Test-CssDesktopEditing {$script:EditorChecks++;return (__EDITOR_RACE__ -and $script:EditorChecks -gt 1)}
 function Assert-CssDesktopNotEditing {if(Test-CssDesktopEditing){throw 'Desktop editor opened; no correction submitted'}}
 function Get-ThemeRevision($Rows){if(__CONFIG_RACE__){return ('c'*64)};return ('b'*64)}
 function Get-ThemeTargetId {'a'*16}
 function Get-AppearanceSteamSession {$session}
 function Invoke-OwnedPresetResourceUpdate($Current){$Current}
 function Get-ThemeRows {foreach($row in $status.themes){[pscustomobject]@{name=$row.name;installed=$true;configured=[bool]$row.configured}}}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  if($Method -eq 'get_themes'){return @($status.themes|ForEach-Object {[pscustomobject]@{name=$_.name;enabled=[bool]$script:FixtureBackend[$_.name]}})}
  if($Method -ne 'set_theme_state'){throw 'FIXTURE_LOADER_METHOD_DENIED'}
  $reply=Invoke-RestMethod -Uri 'http://127.0.0.1:__PORT__/fixture/apply' -Method Post -Body ($Arguments|ConvertTo-Json -Compress) -ContentType 'application/json' -TimeoutSec 3
  if(!$reply.ok){throw 'FIXTURE_LOADER_UPDATE_REJECTED'}
  $script:FixtureBackend[$Arguments.name]=[bool]$Arguments.state
 }
 try {$repairResult=Invoke-AppearanceControl 'Reconcile';$value=[bool]$repairResult.ok;$observed=Get-AppearanceRenderedStatus $session $status}
 catch {$value=$false;$repairMessage=$_.Exception.Message}
}
$variableDefaults=@{};if(Get-Command Get-AppearanceVariableDefaults -ErrorAction SilentlyContinue){foreach($item in $inputs){$variableDefaults[$item.name]=Get-AppearanceVariableDefaults $item.name}}
[pscustomobject]@{variableDefaults=$variableDefaults;confirmed=[bool]$value;receipt=$observed;repairOk=$(if($repairResult){[bool]$repairResult.ok}else{$null});repairMessage=$repairMessage;powerShell=$PSVersionTable.PSVersion.ToString();elapsedMs=$timer.ElapsedMilliseconds}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
`;
 fs.writeFileSync(wrapper,'\ufeff'+ps.replace('__MODULE__',target.replaceAll("'","''")).replace('__CONTENT_MANIFEST__',path.join(pack,'manifest.json').replaceAll("'","''")).replace('__RESULT__',result.replaceAll("'","''")).replace('__ADDRESS__',opts.address||'127.0.0.1').replace('__IMAGE__',opts.wrongOwnerPath?'other.exe':'bin\\cef\\steamwebhelper.exe').replace('__OWNER__',opts.foreignOwner?'$false':'$true').replace('__STATUS__',encoded).replace('__BAD_CONTEXT_PLAN__',opts.badContextPlan?'$true':'$false').replace('__WRAPPER__',opts.wrapper?'$true':'$false').replace('__REPAIR__',opts.repair?'$true':'$false').replace('__PORT__',String(port)).replace('__EDITOR_RACE__',opts.editorRace?'$true':'$false').replace('__CONFIG_RACE__',opts.configRace?'$true':'$false').replace('__CONTROL__',path.join(root,'PowerControl/steam-appearance.ps1').replaceAll("'","''")));
 await exec(process.env.YMCC_DOM_POWERSHELL||'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe',['-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',wrapper],{windowsHide:true,timeout:15000});
 const row={name,...JSON.parse(fs.readFileSync(result,'utf8').replace(/^\ufeff/,'')),queries,frames,expressions,themeWrites:[...themeWrites],visualSnapshots:[...visualSnapshots]};cases.push(row);return row;
}
try{
 if(process.argv[3]){const old=await probe('before-missing-active-patch',{missingFiles:['Round/intensity/10px.css']},path.resolve(process.argv[3]));assert.equal(old.confirmed,true);assert.equal(old.frames,1);console.log('EXPECTED_OLD_FALSE_POSITIVE: eight shared theme receipts passed while the selected Round intensity CSS was missing');}
 if(process.env.YMCC_VARIABLE_BEFORE_MODULE){const old=await probe('before-missing-selected-variable',{configOverrides:{'Mini Carousel':{Size:'0.9'}},missingVariables:['--scale-value']},path.resolve(process.env.YMCC_VARIABLE_BEFORE_MODULE));assert.equal(old.confirmed,true);assert.equal(old.visualSnapshots[0].scaleValue,'0.7');console.log('EXPECTED_OLD_FALSE_POSITIVE: selected 0.9, variable missing, actual scale 0.7');}
 if(process.env.YMCC_CONTEXT_BEFORE_MODULE){const old=await probe('before-menu-only-missing',{pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetMissingFiles:{menu:['Round/round-outline.css']}},path.resolve(process.env.YMCC_CONTEXT_BEFORE_MODULE));assert.equal(old.confirmed,true);assert.equal(old.frames,1);console.log('EXPECTED_OLD_FALSE_POSITIVE: main page matches while the independent MainMenu focus CSS is missing');}
 if(process.env.YMCC_CONTENT_BEFORE_MODULE){const old=await probe('before-stale-content-current-path',{staleContentFiles:['Round/round-outline.css']},path.resolve(process.env.YMCC_CONTENT_BEFORE_MODULE));assert.equal(old.confirmed,true);assert.equal(old.frames,1);assert.equal(old.visualSnapshots[0].ringAnimation,'fixture_twitch');console.log('EXPECTED_OLD_FALSE_POSITIVE: previous path-only receipt accepts the same CSS path while an obsolete focus animation is actually running');}
 const scenarios=[
  ['contexts-only-main-document',{pages:[page('main')]},true],
  ['contexts-disabled-all',{disabled:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}]},true],
  ['contexts-reject-boolean-plan-protocol',{badContextPlan:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}]},false],
  ['contexts-reject-menu-foreign-websocket',{pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1',webSocketDebuggerUrl:'ws://example.invalid:8080/devtools/page/menu'}]},false],
  ['contexts-reject-duplicate-target-id',{pages:[page('main'),{...page('main','MainMenu_uid1'),url:'about:blank?popup=1'}]},false],
  ['contexts-reject-menu-without-main',{pages:[{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}]},false],
  ['contexts-ignore-unrecognized-menu-title',{pages:[page('main'),{...page('other','MainMenu_invalid'),url:'about:blank?popup=1'}]},true],
  ['contexts-closed-menu-stays-unconfirmed',{repair:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetRpcError:['menu'],expectedWrites:[]},false],
  ['contexts-SP-file-leaked-into-menu',{pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetExtraFiles:{menu:['Mini Carousel/shared.css']}},false],
  ['contexts-SP-variable-leaked-into-menu',{pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetVariableOverrides:{menu:{'Mini Carousel':{'--scale-value':'0.9'}}}},false],
  ['contexts-repair-SP-variable-leak',{repair:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetVariableOverrides:{menu:{'Mini Carousel':{'--scale-value':'0.9'}}},expectedWrites:['Mini Carousel']},true],
  ['contexts-main-menu-only-missing',{pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetMissingFiles:{menu:['Round/round-outline.css']}},false],
  ['contexts-quickaccess-only-missing',{pages:[page('main'),{...page('quick','QuickAccess_uid2'),url:'about:blank?popup=1'}],targetMissingFiles:{quick:['Round/intensity/10px.css']}},false],
  ['contexts-two-menu-uids',{pages:[page('main'),{...page('menu1','MainMenu_uid1'),url:'about:blank?popup=1'},{...page('menu2','MainMenu_uid2'),url:'about:blank?popup=1'},{...page('quick1','QuickAccess_uid1'),url:'about:blank?popup=1'},{...page('quick2','QuickAccess_uid2'),url:'about:blank?popup=1'}]},true],
  ['contexts-repair-menu-focus',{repair:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'}],targetMissingFiles:{menu:['Round/round-outline.css']},expectedWrites:['Round']},true],
  ['contexts-repair-same-theme-two-documents',{repair:true,pages:[page('main'),{...page('menu','MainMenu_uid1'),url:'about:blank?popup=1'},{...page('quick','QuickAccess_uid1'),url:'about:blank?popup=1'}],targetMissingFiles:{menu:['Round/round-outline.css'],quick:['Round/shared.css']},expectedWrites:['Round']},true],
  ['contexts-excess-menu-targets',{pages:[page('main'),...Array.from({length:9},(_,i)=>({...page('m'+i,'MainMenu_uid'+i),url:'about:blank?popup=1'}))]},false],

  ['healthy-all-eight',{},true],['boolean-wrapper-healthy',{wrapper:true},true],['boolean-wrapper-partial',{wrapper:true,included:[names[0]]},false],['single-page',{pages:[page('only')]},true],['unrelated-events',{unrelated:true},true],
  ['one-of-eight-only',{included:[names[0]],extraStyles:31},false],['no-themes',{included:[]},false],
  ['all-disabled',{disabled:true},true],['all-disabled-with-unrelated-style',{disabled:true,extraStyles:1},true],['disabled-stale-theme',{disabled:true,included:[names[1]]},false],
  ['two-main-pages',{pages:[page('first'),page('second')]},true],['one-broken-main-page',{pages:[page('first'),page('second')],targetMissing:{second:[names[2]]}},false],
  ...names.map((n,i)=>['missing-managed-theme-'+i,{missing:[n]},false]),
  ...defaultPatchFiles.map((file,i)=>['missing-active-patch-'+i,{missingFiles:[file]},false]),
  ['stale-file-current-path',{staleContentFiles:['Round/round-outline.css']},false],
  ['stale-two-files',{staleContentFiles:['Round/round-outline.css','Game Cover Reflections/shared.css']},false],
  ['duplicate-exact-current-file',{duplicateFiles:['Round/round-outline.css']},false],
  ['duplicate-within-current-style',{duplicateWithinStyle:['Round/round-outline.css']},false],
  ['repair-stale-current-path',{repair:true,staleContentFiles:['Round/round-outline.css'],expectedWrites:['Round']},true],
  ['repair-duplicate-current-file',{repair:true,duplicateFiles:['Round/round-outline.css'],expectedWrites:['Round']},true],
  ['repair-stale-content-silently-fails',{repair:true,keepMissing:true,staleContentFiles:['Round/round-outline.css'],expectedWrites:['Round']},false],
  ['alternate-selected-options',{configOverrides:{Round:{Intensity:'30px',Outlines:'Hidden'},'Mini Carousel':{Size:'0.9'},'NEX-85 Keyboard':{'Dark Mode':'No','Focus Colour':'Blue'}}},true],
  ['missing-default-config-value',{missingConfigKey:{name:'Round',key:'Intensity'},renderConfigOverrides:{Round:{Intensity:'10px'}}},true],
  ['stale-option-after-switch',{configOverrides:{Round:{Intensity:'30px'}},renderConfigOverrides:{Round:{Intensity:'10px'}}},false],
  ['both-old-and-new-option',{extraFiles:['Round/intensity/30px.css']},false],
  ['disabled-with-stale-patch-only',{disabled:true,extraFiles:['Round/intensity/10px.css']},false],
  ['shared-present-but-keyboard-palette-missing',{missingFiles:['NEX-85 Keyboard/focus/default.css']},false],
  ['second-page-patch-missing',{pages:[page('first'),page('second')],targetMissingFiles:{second:['Round/intensity/10px.css']}},false],
  ['invalid-patch-config',{configOverrides:{Round:{Intensity:'unknown'}}},false],
  ['repair-one-active-patch',{repair:true,missingFiles:['Round/intensity/10px.css'],expectedWrites:['Round']},true],
  ['repair-two-patches-one-theme',{repair:true,missingFiles:['Round/intensity/10px.css','Round/round-outline.css'],expectedWrites:['Round']},true],
  ['repair-two-themes',{repair:true,missingFiles:['Round/intensity/10px.css','Mini Carousel/seven.css'],expectedWrites:['Mini Carousel','Round']},true],
  ['repair-old-option-residue',{repair:true,configOverrides:{Round:{Intensity:'30px'}},renderConfigOverrides:{Round:{Intensity:'10px'}},expectedWrites:['Round']},true],
  ['repair-disabled-stale-patch',{repair:true,disabled:true,extraFiles:['Round/intensity/10px.css'],expectedWrites:['Round']},true],
  ['repair-backend-silently-fails',{repair:true,keepMissing:true,missingFiles:['Round/intensity/10px.css'],expectedWrites:['Round']},false],
  ['repair-unknown-js-result',{repair:true,rpcError:true,expectedWrites:[]},false],
  ['repair-editor-opens-during-probe',{repair:true,editorRace:true,missingFiles:['Round/intensity/10px.css'],expectedWrites:[]},false],
  ['repair-user-choice-changes-during-probe',{repair:true,configRace:true,missingFiles:['Round/intensity/10px.css'],expectedWrites:[]},false],
  ['missing-selected-variable',{configOverrides:{'Mini Carousel':{Size:'0.9'}},missingVariables:['--scale-value']},false],
  ['wrong-selected-variable',{variableOverrides:{'Mini Carousel':{'--scale-value':'0.5'}}},false],
  ['equivalent-variable-number',{variableOverrides:{'Game Cover Reflections':{'--ren-reflection-focused-opacity':'.10','--ren-reflection-gradient':'#FFFFFF00   60%'}}},true],
  ['missing-active-reflection-variable',{missingVariables:['--ren-reflection-blur']},false],
  ['off-with-stale-variable',{disabled:true,variableOverrides:{'Mini Carousel':{'--scale-value':'0.5'}}},false],
  ['second-page-variable-missing',{pages:[page('first'),page('second')],configOverrides:{'Mini Carousel':{Size:'0.9'}},targetMissingVariables:{second:['--scale-value']}},false],
  ['repair-selected-variable',{repair:true,configOverrides:{'Mini Carousel':{Size:'0.9'}},missingVariables:['--scale-value'],expectedWrites:['Mini Carousel']},true],
  ['repair-old-variable-after-default',{repair:true,variableOverrides:{'Art Hero':{'--met-game-carousel-font-height':'9em'}},expectedWrites:['Art Hero']},true],
  ['repair-off-variable',{repair:true,disabled:true,variableOverrides:{'Mini Carousel':{'--scale-value':'0.5'}},expectedWrites:['Mini Carousel']},true],
  ['repair-variable-silently-fails',{repair:true,keepVariables:true,configOverrides:{'Mini Carousel':{Size:'0.9'}},missingVariables:['--scale-value'],expectedWrites:['Mini Carousel']},false],
  ['foreign-websocket',{badUrl:true},false],['wrong-owner-path',{wrongOwnerPath:true},false],['foreign-owner',{foreignOwner:true},false],
  ['nonlocal-listener',{address:'0.0.0.0'},false],['no-main',{pages:[{...page('popup','Popup'),url:'about:blank?popup=1'}]},false],
  ['empty-list',{pages:[]},false],['oversized-list',{pages:Array.from({length:65},(_,i)=>page('p'+i))},false],
  ['too-many-main-pages',{pages:Array.from({length:5},(_,i)=>page('p'+i))},false],
  ['malformed-root',{pages:{id:'fake',type:'page',title:'Steam Big Picture',webSocketDebuggerUrl:`ws://127.0.0.1:${port}/devtools/page/fake`}},false],
  ['nested-page-array',{pages:[[page('nested')]]},false],['rpc-rejected',{rpcError:true},false],['wrong-result-type',{wrongType:true},false],
  ['swallowed-javascript-error',{exceptionDetails:true},false],['unknown-receipt-version',{badProtocol:true},false],['boolean-receipt-version',{booleanProtocol:true},false],
  ['incomplete-receipt',{truncatedValues:true},false],['non-string-receipt',{nonStringValue:true},false],['document-still-loading',{notReady:true},false],
  ['incomplete-config-snapshot',{badSnapshot:true},false],['invalid-config-value',{badConfigured:true},false]
 ];
 const selectedScenarios=process.env.YMCC_CONTEXT_DOM_ONLY==='1'?scenarios.filter(([name])=>name.startsWith('contexts-')):process.env.YMCC_CONTENT_DOM_ONLY==='1'?scenarios.filter(([name])=>/healthy-all-eight|stale-file-current-path|stale-two-files|duplicate-exact-current-file|duplicate-within-current-style|repair-stale-current-path|repair-duplicate-current-file|repair-stale-content-silently-fails/.test(name)):scenarios;
 for(const [name,opts,expected] of selectedScenarios){const row=await probe(name,opts);assert.equal(row.confirmed,expected,name+' '+row.receipt?.reason);assert(row.queries<=(opts.repair?4:1),name+' repeated target query');if(opts.repair)assert.deepEqual(row.themeWrites.map(write=>write.name),opts.expectedWrites,name+' unrelated or repeated theme write');if(expected){const documents=opts.pages?opts.pages.filter(t=>t?.type==='page'&&(/browserType=3/.test(t.url)||/^(MainMenu|QuickAccess)/.test(t.title))).length:2;assert(row.frames>=1&&row.frames<=(opts.repair?4:1)*documents);assert(row.powerShell.startsWith(process.env.YMCC_DOM_POWERSHELL?'7.':'5.1.'));if(!opts.wrapper)assert.equal(row.receipt.checked,true);}if(name.startsWith('missing-managed-theme-')||name.startsWith('missing-active-patch-')){assert.equal(row.receipt.checked,true);assert.equal(row.receipt.mismatched.length,1);}if(['repair-one-active-patch','repair-two-patches-one-theme','repair-two-themes'].includes(name)){assert.equal(row.visualSnapshots.find(v=>v.target==='SP').roundRadius,'0px');assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).roundRadius,'10px');}
 if(name==='repair-old-option-residue'){assert.equal(row.visualSnapshots.find(v=>v.target==='SP').roundRadius,'10px');assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).roundRadius,'30px');}
 if(name==='repair-backend-silently-fails'){assert.equal(row.visualSnapshots.find(v=>v.target==='SP').roundRadius,'0px');assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).roundRadius,'0px');}
 if(name==='repair-selected-variable'){assert.equal(row.visualSnapshots.find(v=>v.target==='SP').carouselTransform,'matrix(0.7, 0, 0, 0.7, 0, 0)');assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).carouselTransform,'matrix(0.9, 0, 0, 0.9, 0, 0)');}
 if(name==='repair-variable-silently-fails'){assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).scaleValue,'0.7');}
 if(name==='repair-stale-current-path'){assert.equal(row.visualSnapshots.find(v=>v.target==='SP').ringAnimation,'fixture_twitch');assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).ringAnimation,'none');}
 if(name==='repair-stale-content-silently-fails')assert.equal(row.visualSnapshots.filter(v=>v.target==='SP').at(-1).ringAnimation,'fixture_twitch');
 assert.deepEqual(failures,[]);console.log('PASS '+name+' confirmed='+row.confirmed+' HTTP='+row.queries+' CDP='+row.frames);}
 const actualDefaults=cases[0].variableDefaults,baselineChecks=[];
 for(const[name,variables]of Object.entries(actualDefaults)){const target=await context.newPage();await target.setContent('<style>'+fs.readFileSync(path.join(pack,'themes',name,'shared.css'),'utf8')+'</style>');const actual=await target.evaluate(keys=>Object.fromEntries(keys.map(key=>[key,getComputedStyle(document.documentElement).getPropertyValue(key).trim()])),Object.keys(variables));assert.deepEqual(actual,variables,'Canonical baseline drift '+name);baselineChecks.push({name,variables:actual});await target.close();}
 const assetReceiptChecks=[];const assetPage=await context.newPage();
 for(const file of manifest.files.filter(row=>/\.css$/i.test(row.path))){
  const css=fs.readFileSync(path.join(pack,'themes',file.path),'utf8'),property='--ymcc-file-'+crypto.createHash('sha256').update(file.path.toLowerCase(),'utf8').digest('hex').slice(0,16);
  await assetPage.setContent('<!doctype html><html><head><style>'+css+'</style></head><body></body></html>');
  const value=await assetPage.evaluate(key=>getComputedStyle(document.documentElement).getPropertyValue(key).trim(),property);
  assert.equal(value,'1','CSS parser swallowed file receipt: '+file.path);const contentValue=await assetPage.evaluate(key=>getComputedStyle(document.documentElement).getPropertyValue(key).trim(),file.contentProperty);assert.equal(contentValue,'1','CSS parser swallowed content receipt: '+file.path);assetReceiptChecks.push({file:file.path,property,value,contentProperty:file.contentProperty,contentValue});
 }
 await assetPage.close();
 const report={ok:true,baselineChecks,passed:selectedScenarios.length,cases,sourceHashes,assetReceiptChecks,realPowerShell51:!process.env.YMCC_DOM_POWERSHELL,realPowerShell7:!!process.env.YMCC_DOM_POWERSHELL,realHttpAndWebSocket:true,actualBrowserExpressionExecution:true,realSteamOperations:0,realCssOperations:0,scope:'Actual shared and configured static patch CSS files, including obsolete-option residues; actual selected dynamic values and static fallbacks included; independent renderer targets and real Steam GPU visuals remain separate acceptance'};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log('STEAM_DOM_OK cases='+selectedScenarios.length+' CSS assets='+assetReceiptChecks.length);
}finally{for(const socket of sockets)socket.destroy();await new Promise(resolve=>server.close(resolve));await context.close();await browser.close();}
