// Pure presentation regression using actual mainline catalogs and rendering helpers.
// All input IDs, capabilities and persistence payloads stay at their existing values.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
const {buildSync}=require('esbuild');
const bundled=buildSync({stdin:{contents:"export * from './src/bridge/controllerOemPresentation'; export * from './src/bridge/controllerDeviceDiagram';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text;
const module={exports:{}};new Function('module','exports','require',bundled)(module,module.exports,require);
const {controllerOemDisplayCatalog,formatControllerShortcutDisplayInputs,CONTROLLER_DIAGRAMS,diagramControlState}=module.exports;
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),checks=[];
function check(value,name){assert(value,name);checks.push(name);}
function freeze(value){if(value&&typeof value==='object'){Object.freeze(value);for(const part of Object.values(value))freeze(part)}return value}
// Golden front-view positions from the native model catalogs, independent of M1/M2 numbering.
const numberedRearSlots={
 kRogKeys:{m1:4,m2:3},kRogXbox2025Keys:{m1:4,m2:3},
 kZotacKeys:{m1:3,m2:4},kMsiClawKeys:{m1:4,m2:3},kMsiClawG3EKeys:{m1:4,m2:3},
};
let catalogs=0,renamed=0;
for(const match of native.matchAll(/static const OemKeyDef (\w+)\[\] = \{([\s\S]*?)\n\};/g)){
 const keys=[...match[2].matchAll(/\{"([^\"]+)", "([^\"]+)", (\d+), OemKeyForm::\w+, (true|false)\}/g)].map(m=>({keyId:m[1],label:m[2],backIndex:Number(m[3])||null,triggerCapability:m[4]==='true'?'click-only':'full'}));
 const catalog=freeze({familyId:match[1],supported:true,keys}),before=JSON.stringify(catalog);catalogs++;
 for(const persona of ['steamdeck','ps5','xbox']){
  const displayed=controllerOemDisplayCatalog(persona,catalog);
  check(displayed.keys.length===keys.length,match[1]+' '+persona+' key count');
  check(JSON.stringify(catalog)===before,match[1]+' '+persona+' source catalog untouched');
  check(JSON.stringify(displayed.keys.map(k=>[k.keyId,k.triggerCapability]))===JSON.stringify(keys.map(k=>[k.keyId,k.triggerCapability])),match[1]+' '+persona+' input IDs/capabilities retained');
  if(persona!=='steamdeck'){check(displayed===catalog,match[1]+' '+persona+' original physical names and slots');continue}
  check(JSON.stringify(controllerOemDisplayCatalog(persona,displayed))===JSON.stringify(displayed),match[1]+' remapping is idempotent');
  for(const key of keys){
   const changed=key.backIndex!==null,slot=numberedRearSlots[match[1]]?.[key.keyId]??key.backIndex,expected=changed?[['L4','R4','L5','R5'][slot-1],slot]:[key.label,key.backIndex];
   const shown=displayed.keys.find(k=>k.keyId===key.keyId);check(shown.label===expected[0]&&shown.backIndex===expected[1],match[1]+' '+key.keyId+' exact display label and slot');
   if(!changed)continue;renamed++;
   const live=CONTROLLER_DIAGRAMS.steamdeck.controls.filter(c=>diagramControlState(c,[],[],[key.keyId],[],displayed).live);
   const mapped=CONTROLLER_DIAGRAMS.steamdeck.controls.filter(c=>diagramControlState(c,[],[],[],[key.keyId],displayed).mapped);
   check(live.length===1&&live[0].code===expected[0],match[1]+' '+key.keyId+' real press lights exact lower rear key');
   check(mapped.length===1&&mapped[0].code===expected[0],match[1]+' '+key.keyId+' selection blinks exact lower rear key');
   check(CONTROLLER_DIAGRAMS.steamdeck.controls.every(c=>!diagramControlState(c,[],[],[],[],displayed).live),match[1]+' '+key.keyId+' release clears live fill');
  }
 }
}
const sample=freeze({supported:true,familyId:'msi-claw',keys:[{keyId:'m1',label:'M1',backIndex:2,captureImplemented:false,captureReason:'fixture',triggerCapability:'full',physicalForm:'hid-button',extra:{opaque:7}},{keyId:'m2',label:'M2',backIndex:1,captureImplemented:true,triggerCapability:'full'},{keyId:'claw',label:'CLAW',backIndex:null,triggerCapability:'click-only'}]});
const input=freeze([{source:'oem',code:'m1'},{source:'oem',code:'m2'},{source:'oem',code:'claw'},{source:'keyboard',code:'F18'}]),saved=JSON.stringify(input);
check(formatControllerShortcutDisplayInputs(input,'steamdeck',sample)==='R5 + L5 + CLAW + F18','mixed recording and action text use the same Deck output names');
check(formatControllerShortcutDisplayInputs(input,'ps5',sample)==='M1 + M2 + CLAW + F18','PS keeps its catalog names');
check(JSON.stringify(input)===saved,'display formatting never rewrites persisted input IDs');
const shown=controllerOemDisplayCatalog('steamdeck',sample);
check(shown.keys[0].captureImplemented===false&&shown.keys[0].captureReason==='fixture'&&shown.keys[0].extra.opaque===7,'unavailable/unknown metadata retained');
check(!diagramControlState(CONTROLLER_DIAGRAMS.steamdeck.controls.find(c=>c.code==='R5'),[],[],['m1'],['m1'],shown).available,'renaming never activates an unavailable source');
check(formatControllerShortcutDisplayInputs([{source:'oem',code:'opaque-key'}],'steamdeck',sample)==='opaque-key','unknown inputs retain original identity');
const unsupported=freeze({...sample,supported:false});check(controllerOemDisplayCatalog('steamdeck',unsupported)===unsupported,'unsupported catalog remains unchanged');
const out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerBackKeyDisplay-20261018');fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'PRESENTATION-TESTS.json'),JSON.stringify({pass:true,catalogs,renamed,checks,savedInputIdsPreserved:true,hardwareIo:false,productionSettingsWritten:false,sourceHashes:Object.fromEntries(['native/main.cpp','src/bridge/controllerOemPresentation.ts','src/bridge/controllerDeviceDiagram.ts','src/bridge/controllerDiagramData.json'].map(f=>[f,createHash('sha256').update(fs.readFileSync(path.join(root,f))).digest('hex')]))},null,2));
console.log('PASS '+checks.length+' presentation checks across '+catalogs+' actual native catalogs; '+renamed+' rear inputs mapped to Deck display names.');
