import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
// Maintenance inventory only: no network, Steam launch, CDP connection or setting write.
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const map=JSON.parse(fs.readFileSync(path.join(root,'docs/steam-integration-map.json'),'utf8'));
const excluded=new Set(map.excludedDirectoryNames);
const textExtensions=new Set(['.h','.cpp','.c','.ts','.tsx','.vue','.js','.mjs','.ps1','.py','.bat','.vbs','.json','.md']);
const files=[];
function walk(relative){
  const dir=path.join(root,relative);if(!fs.existsSync(dir))return;
  for(const e of fs.readdirSync(dir,{withFileTypes:true})){
    if(e.isSymbolicLink())continue;
    const rel=relative+'/'+e.name;
    if(e.isDirectory()){if(!excluded.has(e.name))walk(rel);}
    else if(textExtensions.has(path.extname(e.name).toLowerCase()))files.push(rel);
  }
}
for(const dir of map.discoveryRoots)walk(dir);
const glob=pattern=>new RegExp('^'+pattern.split('**').map(part=>part.split('*').map(s=>s.replace(/[.+?^${}()|[\]\\]/g,'\\$&')).join('[^/]*')).join('.*')+'$');
const areas=map.areas.map(a=>({...a,matchers:a.sourcePatterns.map(glob),tests:a.testPatterns.map(glob)}));
const missingAnchors=areas.flatMap(a=>a.anchors.filter(p=>!fs.existsSync(path.join(root,p))).map(p=>({area:a.id,path:p})));
const runtimeToken=/SteamClient\b|steam:\/\/|ScopeRunningApps|steam\.(?:settings|uiMode|uiState)|steamAppearance|deckySidebar|shortcuts\.vdf|loginusers\.vdf|libraryfolders\.vdf|steam_appid|CSSLoader/;
const shared=new Set(Object.keys(map.sharedSourceBoundaries));
const candidates=files.filter(p=>/steam|decky/i.test(path.basename(p))||shared.has(p)||(() => {
  const stat=fs.statSync(path.join(root,p));
  return stat.size<8*1024*1024 && runtimeToken.test(fs.readFileSync(path.join(root,p),'utf8'));
})());
const inventory=candidates.map(p=>({path:p,areas:areas.filter(a=>a.matchers.some(re=>re.test(p))||a.tests.some(re=>re.test(p))||a.anchors.includes(p)).map(a=>a.id),sharedBoundary:shared.has(p),testOrTool:p.startsWith('tools/')}));
const unmatched=inventory.filter(p=>!p.areas.length&&!p.sharedBoundary);
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const live=fs.readFileSync(path.join(root,'native/steam_live.js'),'utf8');
const ipc=[...native.matchAll(/ipc_on\("([^"]+)"/g)].map(m=>m[1]).filter(s=>/steam|decky|sleepRecovery/i.test(s));
const liveOperations=[...new Set([...live.matchAll(/['"]((?:sleep|menu|settings|overlay|mouse|ui)\.[a-z-]+(?:\.[a-z-]+)?)['"]/g)].map(m=>m[1]))].sort();
const report={schemaVersion:1,guide:map.guide,sourceMapIntegrityValid:missingAnchors.length===0,steamCompatibilityVerified:false,readOnly:true,networkOperations:0,hardwareOperations:0,
  implementation:[...fs.readFileSync(path.join(root,'native/sleep_recovery_auto.h'),'utf8').matchAll(/steam-wake-round[\w-]+/g)].map(m=>m[0]).filter((s,i,a)=>a.indexOf(s)===i),
  checkedOn:map.reviewedAt,areaCount:areas.length,candidateCount:inventory.length,missingAnchors,unmappedCandidates:unmatched,
  ipc,liveOperations,areas:areas.map(a=>({id:a.id,label:a.label,anchors:a.anchors,symbols:a.symbols,upgradeReview:a.upgradeReview,
    matchingTests:files.filter(p=>a.tests.some(re=>re.test(p)))})),inventory};
const outputAt=process.argv.indexOf('--output');
if(outputAt>=0){if(!process.argv[outputAt+1])throw Error('--output requires a file');fs.writeFileSync(path.resolve(process.argv[outputAt+1]),JSON.stringify(report,null,2)+'\n');}
if(process.argv.includes('--json'))console.log(JSON.stringify(report,null,2));
else {
  console.log(`Steam integration map: ${areas.length} areas, ${inventory.length} candidate files, ${missingAnchors.length} missing anchors, ${unmatched.length} unmapped candidates.`);
  console.log('Source map only; Steam compatibility and field behavior are NOT verified. See STEAM-UPGRADE.md.');
  for(const p of missingAnchors)console.log('MISSING '+p.area+' '+p.path);
  for(const p of unmatched)console.log('REVIEW '+p.path);
}
if(missingAnchors.length)process.exitCode=1;
