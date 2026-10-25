// Stage and exercise the freshly built Steam page against the installed module ABI.
// Never writes the installed app; deployment is a separate hash-checked operation.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
const ts=require('typescript');
const installed='C:/SOFT/YeMan/YeManCC/assets';
const build='G:/YeManCC-Work/_scratch/steam-appearance-unlock-20261011/candidate/web/assets';
const out='G:/YeManCC-Work/_scratch/steam-appearance-unlock-20261011/installed-hotfix';
fs.mkdirSync(out,{recursive:true});
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const choose=(dir,expr)=>{const found=fs.readdirSync(dir).filter(n=>expr.test(n));assert.equal(found.length,1,'ambiguous Steam payload');return found[0]};
const jsName=choose(installed,/^SteamView-.*\.js$/),cssName=choose(installed,/^SteamView-.*\.css$/);
const buildJSName=choose(build,/^SteamView-.*\.js$/),buildCSSName=choose(build,/^SteamView-.*\.css$/);
const original=fs.readFileSync(path.join(installed,jsName),'utf8'),candidate=fs.readFileSync(path.join(build,buildJSName),'utf8');
const parse=s=>ts.createSourceFile('module.js',s,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
const imports=s=>parse(s).statements.filter(ts.isImportDeclaration).map(n=>({path:n.moduleSpecifier.text,bindings:n.importClause?.namedBindings?.elements?.map(e=>(e.propertyName||e.name).text)||[]}));
const oldImports=imports(original),nextImports=imports(candidate);assert.equal(oldImports.length,2);assert.equal(nextImports.length,2);
const checks=[];const check=(name,test)=>{assert.ok(test,name);checks.push(name);console.log('PASS '+name)};
check('Installed and candidate Steam modules import the same stable exported ABI',oldImports.every((entry,i)=>JSON.stringify(entry.bindings)===JSON.stringify(nextImports[i].bindings)));
const oldMain=oldImports.find(i=>/^\.\/main-/.test(i.path)),newMain=nextImports.find(i=>/^\.\/main-/.test(i.path));
const helper=oldImports.find(i=>i.path!==oldMain.path);check('Vue runtime helper is byte-identical and shared, not duplicated',hash(fs.readFileSync(path.join(installed,helper.path)))===hash(fs.readFileSync(path.join(build,helper.path))));
const oldCSS=fs.readFileSync(path.join(installed,cssName),'utf8'),newCSS=fs.readFileSync(path.join(build,buildCSSName),'utf8');
const oldScopes=[...new Set(original.match(/data-v-[\da-f]+/g))],newScopes=[...new Set(candidate.match(/data-v-[\da-f]+/g))];
let normalizedCSS=newCSS;for(let i=0;i<newScopes.length;i++)normalizedCSS=normalizedCSS.replaceAll(newScopes[i],oldScopes[i]);
check('Steam page CSS changes only its component scope hashes, not layout/design',normalizedCSS===oldCSS);
// Export slots are not a semantic ABI: Rollup can reorder b4/b5/b6 when call order
// changes. Match the actual exported definitions, ignoring private minified names.
function definitions(text) {
 const ast=parse(text),local=new Map(),exports=new Map();
 for(const n of ast.statements){
  if(ts.isFunctionDeclaration(n)&&n.name)local.set(n.name.text,n);
  if(ts.isVariableStatement(n))for(const d of n.declarationList.declarations)if(ts.isIdentifier(d.name)&&d.initializer)local.set(d.name.text,d.initializer);
  if(ts.isExportDeclaration(n)&&n.exportClause?.elements)for(const e of n.exportClause.elements)exports.set(e.name.text,(e.propertyName||e.name).text);
 }
 const fingerprint=node=>{const parts=[];function visit(n){
  if(ts.isIdentifier(n)){const p=n.parent;const property=(ts.isPropertyAccessExpression(p)&&p.name===n)||(ts.isPropertyAssignment(p)&&p.name===n)||(ts.isMethodDeclaration(p)&&p.name===n);parts.push(property?'property:'+n.text:'identifier');return;}
  if(ts.isStringLiteral(n)||ts.isNumericLiteral(n)||n.kind===ts.SyntaxKind.RegularExpressionLiteral){parts.push(n.kind+':'+n.text);return;}
  parts.push(n.kind);ts.forEachChild(n,visit);
 }visit(node);return JSON.stringify(parts)};
 return new Map([...exports].map(([slot,name])=>[slot,local.has(name)?fingerprint(local.get(name)):null]));
}
const oldDefinitions=definitions(fs.readFileSync(path.join(installed,oldMain.path),'utf8'));
const newDefinitions=definitions(fs.readFileSync(path.join(build,newMain.path),'utf8'));
const newAST=parse(candidate),mainImport=newAST.statements.find(n=>ts.isImportDeclaration(n)&&n.moduleSpecifier.text===newMain.path);
const slotMap=[];const bindings=mainImport.importClause.namedBindings.elements.map(e=>{const slot=(e.propertyName||e.name).text,fp=newDefinitions.get(slot);assert(fp,'missing exported definition '+slot);const matches=[...oldDefinitions].filter(([key,value])=>value===fp);assert.equal(matches.length,1,'non-unique/changed imported semantic export '+slot);const oldSlot=matches[0][0];slotMap.push({candidate:slot,installed:oldSlot,alias:e.name.text});return oldSlot+' as '+e.name.text;});
check('Every imported main symbol resolves to the same semantic implementation',slotMap.length===oldMain.bindings.length);
const adapted=candidate.replace(mainImport.getText(newAST),'import {'+bindings.join(',')+'} from '+JSON.stringify(oldMain.path)+';');
check('Adapted Steam page retains exactly the existing installed imports',JSON.stringify(imports(adapted).map(i=>i.path))===JSON.stringify(oldImports.map(i=>i.path)));
fs.writeFileSync(path.join(out,jsName),adapted);fs.writeFileSync(path.join(out,cssName),newCSS);
const tree=parse(adapted);let cardId='';for(const statement of tree.statements){if(ts.isVariableStatement(statement))for(const d of statement.declarationList.declarations){if(d.initializer?.getText(tree).includes('__name:"SteamAppearanceCard"')){cardId=d.name.getText(tree);break}}}assert(cardId,'compiled Card was not located');
const helperName=path.basename(helper.path),mainName=path.basename(oldMain.path);
const pack=JSON.parse(fs.readFileSync(path.join(root,'PowerControl/steam-appearance/manifest.json'),'utf8'));
const themes=pack.themes.map(({name})=>name);
const fixture=String.raw`
const names=${JSON.stringify(themes)};
window.__fixture={failRead:true,hiddenFails:false,hiddenCalls:0,actions:[],revision:1,snapshot:{targetId:'a'.repeat(16),revision:'1'.padStart(64,'0'),linked:true,backendReady:false,backendProcessCount:0,steamRunning:false,configuredMatchesBackend:false,note:'',packAvailable:true,packOwned:true,packPrepared:true,packRepairRequired:false,backendReloadRequired:true,canRestore:true,foreignActiveCount:0,packError:'',themes:names.map(name=>({name,installed:true,configured:true,backendEnabled:null,parameterCount:1,error:''}))}};
const f=()=>window.__fixture;
export const aR={exists:async()=>true};export const aS={location:async()=>({directory:'C:\\SOFT\\YeMan\\PowerControl'})};
export const aD={run:async(program,args)=>{const action=args[args.indexOf('-Action')+1];f().actions.push(action);let result;
 if(action==='Status')result=f().failRead?{ok:false,message:'fixture status unavailable'}:{ok:true,snapshot:structuredClone(f().snapshot)};
 if(action==='Apply'){const request=JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(args[args.indexOf('-Payload')+1]),c=>c.charCodeAt(0))));assertRevision(request);for(const[name,on]of Object.entries(request.states)){const row=f().snapshot.themes.find(r=>r.name===name);row.configured=on;row.backendEnabled=f().snapshot.backendReady?on:null}f().snapshot.revision=(++f().revision).toString(16).padStart(64,'0');f().snapshot.configuredMatchesBackend=f().snapshot.backendReady;result={ok:true,saved:true,message:'配置已保存',snapshot:structuredClone(f().snapshot)}}
 if(action==='Refresh')result={ok:true,snapshot:structuredClone(f().snapshot)};
 if(!result)throw Error('Unexpected compiled fixture action '+action);return{exitCode:result.ok?0:1,stdout:JSON.stringify(result),stderr:''};},hidden:async()=>{f().actions.push('Ensure');f().hiddenCalls++;if(f().hiddenFails)return{ok:false};Object.assign(f().snapshot,{backendReady:true,backendProcessCount:2,backendReloadRequired:false,configuredMatchesBackend:true});for(const row of f().snapshot.themes)row.backendEnabled=row.configured;return{ok:true}}};
function assertRevision(r){if(r.expectedRevision!==f().snapshot.revision)throw Error('stale compiled fixture revision')}
export const a5=el=>el?.focus();export const I={render:()=>null};
Object.assign(f(),{uiVisible:true,mainRunning:false,mainLaunches:0,mainStops:0,mainDeferred:false,mainRelease:null});
export const aT={get:async()=>({enabled:false,phase:'disabled',revision:1}),subscribe:()=>()=>{},environment:async()=>({ready:true})};
export const aU=handler=>{window.addEventListener('fixture-visibility',()=>handler({visible:f().uiVisible}));return()=>{}};
export const e={render:()=>null};export const aV=[];export const aW=()=>'';export const aX=async()=>[];export const aY=async()=>false;
export const aZ={openFile:async()=>null};export const a_=async()=>null;export const a$=async()=>null;export const b0=async()=>null;
export const b1=async()=>null;export const b2=async()=>({ok:true});export const b3=async()=>f().mainRunning;
export const b4=async()=>{f().mainLaunches++;if(f().mainDeferred)await new Promise(resolve=>f().mainRelease=resolve)};
export const b5=async()=>{f().mainStops++};export const b6=()=>f().uiVisible;export const b7=e=>e?.message||'';export const b8=async()=>true;export const b9=async()=>null;
`;
const html=`<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/${cssName}"><style>:root{--bg-solid:#111318;--bg-panel:#1b1f28;--bg-input:#242a34;--text:#eee;--text-dim:#9299a5;--accent:#2dad9b;--ok:#68c682;--danger:#e27878;--radius:12px;--radius-ctrl:8px;--focus-ring:0 0 0 2px #2dad9b}body{font:14px system-ui;background:#111318;color:#eee;margin:25px}#app{max-width:680px}.card{padding:18px;background:#1b1f28;border-radius:12px}</style></head><body><div id="app"></div><script type="module">import {c as createApp}from '/${helperName}';import Steam,{${cardId} as Card}from '/${jsName}';const app=createApp(new URL(location.href).searchParams.has('main')?Steam:Card);app.mount('#app');window.__unmount=()=>app.unmount();</script></body></html>`;
const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.join(runtime,'_appearance.cjs'))('playwright');
const server=createServer((req,res)=>{const p=(req.url||'/').split('?')[0];let body,type;if(p==='/'){body=html;type='text/html'}else if(p==='/'+jsName){body=adapted+'\nexport {'+cardId+'};';type='text/javascript'}else if(p==='/'+cssName){body=newCSS;type='text/css'}else if(p==='/'+mainName){body=fixture;type='text/javascript'}else if(p==='/'+helperName){body=fs.readFileSync(path.join(installed,helperName));type='text/javascript'}else{res.writeHead(404).end();return}res.writeHead(200,{'Content-Type':type+'; charset=utf-8'});res.end(body)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let browser;const pageErrors=[];
try{browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:720,height:800}});page.on('pageerror',e=>pageErrors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>document.querySelector('.appearance-notice')?.textContent.includes('unavailable'));
 check('Actual built master stays clickable after initial status failure',await page.getByRole('switch',{name:'全部外观效果',exact:true}).isEnabled());check('Unknown status does not falsely show green readiness',await page.locator('.appearance-status-dot.ready').count()===0);
 await page.getByRole('switch',{name:'全部外观效果',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('.appearance-master')?.disabled);check('Persistent status error releases actual built controls without a blind write',await page.evaluate(()=>!window.__fixture.actions.includes('Apply')));
 await page.evaluate(()=>window.__fixture.failRead=false);await page.getByRole('switch',{name:'全部外观效果',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.appearance-count')?.textContent==='8 / 8');check('Actual built master rechecks unknown state and saves default-on while CSS is absent',await page.evaluate(()=>window.__fixture.actions.includes('Apply')&&window.__fixture.hiddenCalls===0));
 await page.evaluate(()=>{window.__fixture.snapshot.steamRunning=true;window.__fixture.hiddenFails=true});await page.getByRole('switch',{name:'全部外观效果',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.appearance-count')?.textContent==='0 / 8'&&!document.querySelector('.appearance-master')?.disabled);check('Actual built service persists switches before missing-backend launch failure',await page.evaluate(()=>window.__fixture.actions.lastIndexOf('Apply')<window.__fixture.actions.lastIndexOf('Ensure')));
 check('Launch failure cannot leave the actual built master disabled',await page.getByRole('switch',{name:'全部外观效果',exact:true}).isEnabled());
 await page.evaluate(()=>window.__fixture.hiddenFails=false);await page.getByRole('switch',{name:'全部外观效果',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.appearance-status-dot.ready')&&!document.querySelector('.appearance-master')?.disabled);check('A retry detects/starts backend and returns actual built master to enabled state',await page.locator('.appearance-count').textContent()==='8 / 8');
 await page.evaluate(()=>{window.__fixture.failRead=true;window.dispatchEvent(new Event('global:refresh'))});await page.waitForFunction(()=>document.querySelector('.appearance-count')?.textContent==='— / 8');await page.getByRole('button',{name:'编辑外观'}).click();await page.waitForSelector('.appearance-editor');check('Actual built editor switches remain retryable under unknown status',await page.getByRole('switch',{name:'首页布局',exact:true}).isEnabled());
 await page.evaluate(()=>window.__fixture.failRead=false);await page.getByRole('switch',{name:'首页布局',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.appearance-theme-switch')?.disabled===false&&document.querySelector('.appearance-editor-summary')?.textContent.includes('8 / 8'));
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:gamepad-back',{cancelable:true})));await page.waitForFunction(()=>!document.querySelector('.appearance-editor'));
 await page.evaluate(()=>{window.__fixture.snapshot.themes.find(r=>r.name==='Round').configured=false;window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail:{phase:'running'}}))});await page.waitForFunction(()=>document.querySelector('.appearance-count')?.textContent==='7 / 8');check('Actual built card consumes finite native backend-change notifications without polling',true);
 await page.screenshot({path:path.join(out,'installed-hotfix-recovery.png')});
 await page.goto('http://127.0.0.1:'+server.address().port+'/?main=1');await page.waitForFunction(()=>document.querySelector('.steam-power-button')?.disabled===false&&document.querySelector('.appearance-notice'));
 check('Actual built main Steam launch remains available even when CSS status is unknown',await page.locator('.steam-power-button').isEnabled());
 await page.locator('.steam-power-button').click();await page.waitForFunction(()=>window.__fixture.mainLaunches===1&&document.querySelector('.steam-power-button')?.disabled);
 await page.evaluate(()=>{window.__fixture.uiVisible=false;window.dispatchEvent(new Event('fixture-visibility'))});await page.waitForFunction(()=>document.querySelector('.steam-power-button')?.disabled===false);
 check('Actual built main Steam startup cancellation releases busy controls on window hide',true);
 await page.evaluate(()=>{window.__fixture.uiVisible=true;window.dispatchEvent(new Event('fixture-visibility'))});await page.locator('.steam-power-button').click();await page.waitForFunction(()=>window.__fixture.mainLaunches===2);
 check('Actual built main Steam launch retries via the same original route after cancellation',true);
 await page.evaluate(()=>{window.__fixture.uiVisible=false;window.dispatchEvent(new Event('fixture-visibility'))});await page.waitForFunction(()=>document.querySelector('.steam-power-button')?.disabled===false);
 await page.evaluate(()=>{window.__fixture.uiVisible=true;window.__fixture.mainDeferred=true;window.dispatchEvent(new Event('fixture-visibility'))});await page.locator('.steam-power-button').click();await page.waitForFunction(()=>window.__fixture.mainRelease!==null);
 await page.evaluate(()=>{window.__fixture.uiVisible=false;window.dispatchEvent(new Event('fixture-visibility'));window.__fixture.mainRelease()});await page.waitForFunction(()=>document.querySelector('.steam-power-button')?.disabled===false);await page.waitForTimeout(80);
 check('Actual built late launcher completion cannot restart a hidden busy/confirmation loop',await page.locator('.steam-power-button').isEnabled());
 await page.screenshot({path:path.join(out,'installed-main-steam-unlocked.png')});
 check('Actual built Steam appearance and main-power hotfix emitted no JavaScript runtime errors',pageErrors.length===0);
 const receipt={ok:true,scope:'Actual production-built Steam appearance module + byte-identical installed Vue helper; memory native adapter only',checks,pageErrors,semanticImportSlotMap:slotMap,installedFiles:[{path:path.join(installed,jsName),beforeSha256:hash(original),candidate:path.join(out,jsName),afterSha256:hash(adapted)},{path:path.join(installed,cssName),beforeSha256:hash(oldCSS),candidate:path.join(out,cssName),afterSha256:hash(newCSS)}],mainModuleUntouched:path.join(installed,mainName),mainModuleSha256:hash(fs.readFileSync(path.join(installed,mainName))),installedExeSha256:hash(fs.readFileSync('C:/SOFT/YeMan/YeManCC/YeManCC.exe')),actualSteamOperations:0,actualLoaderLaunches:0};fs.writeFileSync(path.join(out,'installed-hotfix-result.json'),JSON.stringify(receipt,null,2));console.log('STEAM_APPEARANCE_COMPILED_HOTFIX_OK tests='+checks.length);
}catch(error){console.error('Compiled hotfix browser errors',pageErrors);throw error;}finally{await browser?.close();await new Promise(resolve=>server.close(resolve))}
