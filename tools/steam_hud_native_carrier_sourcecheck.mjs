// Compare dynamic capability signatures against retained installed Steam source.
// Only function objects / pure flag factory evaluated with inert enums; no native hook invoked.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),ts=require('typescript');
const dir=path.resolve(process.argv[2]||'');assert.ok(dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\native-ready-'));
const review=fs.readFileSync('G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/SOURCE-REVIEW-LATEST.txt','utf8').trim();
const prior=JSON.parse(fs.readFileSync(path.join(review,'LOCAL-STEAM-SOURCE-MODULES.json'),'utf8'));
for(const f of prior.sources){const hash=crypto.createHash('sha256').update(fs.readFileSync(f.file)).digest('hex');assert.equal(hash,f.sha256,'Installed Steam source changed: regenerate static snapshots before any live test');}
const library=fs.readFileSync(path.join(review,'library-module-11131.source.txt'),'utf8'),hooks=fs.readFileSync(path.join(review,'library-module-63439.source.txt'),'utf8');
function fn(code,name){const sf=ts.createSourceFile('read-only-snapshot.js',code,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS),nodes=[];const walk=n=>{if(ts.isFunctionDeclaration(n)&&n.name?.text===name)nodes.push(n);ts.forEachChild(n,walk);};walk(sf);assert.equal(nodes.length,1);return nodes[0].getText(sf);}
const entries=[...library.matchAll(/e2\[e2\.(\w+)\s*=\s*(\d+)\]\s*=\s*"\1"/g)].map(m=>[m[1],Number(m[2])]),kindKeys=['Overlay','Notification','Tooltip','PopupContextMenu','StandaloneContextMenu'];
const flags=Object.fromEntries(entries.filter(([k])=>!kindKeys.includes(k))),kinds=Object.fromEntries(entries.filter(([k])=>kindKeys.includes(k)));
const context=vm.createContext({n:flags,i:kinds,v:()=>0});
const flagFactory=vm.runInContext('('+fn(library,'S')+')',context,{timeout:1000}),nativeHook=vm.runInContext('('+fn(hooks,'d')+')',context,{timeout:1000});
const {discoverNativePopupCapabilities}=require(path.join(dir,'native-carrier.cjs'));
const caps=discoverNativePopupCapabilities(new Map([[17,{currentHook:nativeHook}],[33,{currentFlags:flagFactory,flags,kinds}]]));assert.equal(caps.available,true);
const report={pass:true,scope:'read-only installed-source signatures + pure flags in inert VM',hookSignatureMatches:true,flagFactorySignatureMatches:true,
  flags:flagFactory(kinds.Notification),installedSources:prior.sources,liveSteamTouched:false,nativeHookInvoked:false,gamePixelsVerified:false};
fs.writeFileSync(path.join(dir,'NATIVE-SOURCE-SIGNATURE-CHECK.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
