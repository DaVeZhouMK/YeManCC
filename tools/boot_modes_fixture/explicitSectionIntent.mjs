import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {pathToFileURL} from 'node:url';import {createHash} from 'node:crypto';
export async function runExplicitSectionIntent(out,args=[],options={}){
const M=process.cwd();
const fields=[
 ['theme','ui',['theme'],['background','ui','theme'],'blue-black','other-theme'],
 ['dynamic','ui',['dynamicBackgroundEnabled'],['background','ui','dynamicBackgroundEnabled'],true,false],
 ['opacity','ui',['backgroundOpacity'],['background','ui','backgroundOpacity'],.7,.3],
 ['blur','ui',['backgroundBlur'],['background','ui','backgroundBlur'],12,4],
 ['video','ui',['videoBatteryPause'],['background','ui','videoBatteryPause'],true,false],
 ['wallpaper','ui',['steamDynamicWallpaperEnabled'],['steamWallpaper','enabled'],true,false],
 ['download','ui',['steamWallpaperBackgroundDownload'],['steamWallpaper','download'],true,false],
 ['threshold','ui',['steamWallpaperDownloadCpuThreshold'],['steamWallpaper','cpuThreshold'],15,30],
 ['decky','extensions',['deckySidebar','enabled'],['steamDecky','enabled'],true,false],
 ['recovery','sleep',['autoWakeRecovery'],['sleep','steamAutoRecovery'],true,false],
 ['sleep','sleep',['pauseGameOnSleep'],['sleep','config','pauseGameOnSleep'],false,true],
 ['monitor','rtss',['monitorOutput','ymccTemplate'],['monitor','preferences','ymccTemplate'],'A','B'],
 ['fan','fan',['preset'],['fan','preset'],'balanced','soft'],
 ['fanBoot','startupDesired',['fanControl'],['fan','enabled'],false,true],
 ['touch','screenTouchpads',['profiles','steamdeck','scale'],['touch','profiles','steamdeck','scale'],100,70],
];
const all=['global','xbox','steam','xbox-steam'].flatMap(mode=>fields.flatMap(field=>['default','boot-mode','runtime','mutator'].map(origin=>({mode,field,origin,key:mode+'/'+field[0]+'/'+origin}))));
const selections=args.filter(v=>v.startsWith('--explicit-section-only='));assert(selections.length<=1,'Duplicate explicit-section selector');
const groups=args.filter(v=>v.startsWith('--explicit-section-group='));assert(groups.length<=1&&(!groups.length||!selections.length),'Choose exact explicit section or group');
const group=groups[0]?.slice('--explicit-section-group='.length);assert(group===undefined||group==='common-fan','Unknown explicit-section group');
const commonOnly=field=>['fan','fanBoot','fanCurve'].includes(field[0]);
const sharedOnly=field=>commonOnly(field)||field[1]==='sleep';
// Retain all original case identities. The common-only changed-value controls
// are explicit opt-in, not additions to default minimum/whole-suite execution.
const commonChanges=['global','xbox','steam','xbox-steam'].flatMap(mode=>[
 {mode,field:fields.find(field=>field[0]==='fan'),origin:'default',changed:true,key:mode+'/fan/changed'},
 ...[true,false].map(enabled=>({mode,field:['fanBoot','startupDesired',['fanControl'],['fan','enabled'],enabled,!enabled],origin:'default',changed:true,key:mode+'/fanBoot/'+(enabled?'on':'off')+'-changed'})),
 {mode,field:['fanCurve','fan',['nodes'],['fan','nodes'],[{temp:45,fan:30},{temp:85,fan:90}],[{temp:40,fan:25},{temp:80,fan:75}]],origin:'default',changed:true,key:mode+'/fanCurve/changed'},
]);
const selected=group==='common-fan'?[...all.filter(s=>commonOnly(s.field)),...commonChanges]:selections.length?[...all,...commonChanges].filter(s=>s.key===selections[0].slice('--explicit-section-only='.length)):all;assert(selected.length,'Unknown explicit-section selector');
const source=options.preferencesSource;
const native=options.native;assert(native&&typeof native.invoke==='function','Owned original native required');
const {createDiskRuntime}=await import(pathToFileURL(path.join(M,'tools/boot_modes_fixture/diskSettings.mjs')));
const disk=await createDiskRuntime(path.join(out,'disk'),native,{settingsSources:source?{bootModeGlobalPreferences:source}:undefined,moduleDirectory:source?undefined:path.join(out,'compiled-runtime')}),a=disk.a,r=a.repository;
const get=(o,p)=>p.reduce((v,k)=>v?.[k],o);const put=(o,p,value)=>{for(const k of p.slice(0,-1))o=o[k]??={};o[p.at(-1)]=structuredClone(value);};
const rows=[];let complete=false;const digest=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const sourceHashes=Object.fromEntries([...new Set([...a.inputs,'tools/boot_modes_fixture/explicitSectionIntent.mjs','src/bridge/bootModeGlobalPreferences.ts','src/bridge/settingsRepository.ts'])].map(file=>[file,digest(path.resolve(M,file))]));
try{
 await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
 const initial=await a.boot.loadBootModes({editorOnly:true});await a.boot.saveBootModes(initial.document,initial.document.revision,initial.initialSource);
 const seed=disk.read();
 for(const s of selected){
  const [field,section,rootPath,globalPath,value,old]=s.field;
  const data=structuredClone(seed),doc=data.extensions.bootModes;doc.current=s.mode;doc.boot=s.mode;
  put(data,[section,...rootPath],s.changed?old:value);put(doc.global,globalPath,old);
  const own=s.mode==='global'?'steam':s.mode;doc.scenes[own].overrides[globalPath[0]]={sentinel:'independent'};
  doc.scenes[own].rememberedOverrides={[globalPath[0]]:{sentinel:'remembered'}};
  doc.presets[own]={kept:{name:'kept',overrides:{[globalPath[0]]:{sentinel:'preset'}}}};
  doc.global[globalPath[0]].futureSentinel='global-unknown-preserved';
  disk.writeRaw(data);const before=await r.loadSettings(),beforeMode=structuredClone(before.extensions.bootModes),patch={};
  const submitted=s.changed?value:get(before,[section,...rootPath]);put(patch,rootPath,submitted);
  const options=s.origin==='default'||s.origin==='mutator'?{}:{origin:s.origin};
  if(s.origin==='mutator')await r.updateSettings(d=>put(d,[section,...rootPath],submitted),options);
  else await r.saveSettingsSection(section,patch,undefined,options);
  const after=disk.read(),projects=s.origin==='default'&&!sharedOnly(s.field),expected=projects?submitted:old;
  const row={key:s.key,commonOnly:sharedOnly(s.field),beforeRoot:get(before,[section,...rootPath]),afterRoot:get(after,[section,...rootPath]),submitted,beforeGlobal:get(beforeMode.global,globalPath),expected,actual:get(after.extensions.bootModes.global,globalPath),beforeRevision:beforeMode.revision,afterRevision:after.extensions.bootModes.revision};rows.push(row);
  assert.deepEqual(row.actual,expected,sharedOnly(s.field)?'Shared preference must not enter a retired mode bank '+s.key:'Explicit ordinary section choice must reach global even when runtime already matches '+s.key);
  assert.deepEqual(row.afterRoot,submitted,'The original common/ordinary preference was not actually saved '+s.key);
  const expectedGlobal=structuredClone(beforeMode.global);if(projects)put(expectedGlobal,globalPath,expected);
  assert.deepEqual(after.extensions.bootModes.global,expectedGlobal,'Unmentioned global leaves were captured '+s.key);
  assert.deepEqual(after.extensions.bootModes.scenes,beforeMode.scenes,'Independent scene was overwritten '+s.key);
  assert.deepEqual(after.extensions.bootModes.presets,beforeMode.presets);assert.deepEqual(after.extensions.bootModes.activeSnapshot,beforeMode.activeSnapshot);
  assert.equal(after.extensions.bootModes.global[globalPath[0]].futureSentinel,'global-unknown-preserved');
  assert.equal(row.afterRevision,row.beforeRevision+(projects?1:0),'Only an actual mode projection owns a new mode revision '+s.key);
  if(sharedOnly(s.field))assert.deepEqual(after.extensions.bootModes,beforeMode,'Shared save changed the four-mode owner '+s.key);
  assert.deepEqual(after.games,before.games);assert.deepEqual(after.gameProfiles,before.gameProfiles);
  const reopened=await disk.moduleAt('reopened-'+rows.length),loaded=await reopened.repository.loadSettings();
  assert.deepEqual(get(loaded,[section,...rootPath]),submitted,'Common preference did not survive a fresh repository '+s.key);
  assert.deepEqual(loaded.extensions.bootModes,after.extensions.bootModes,'Four-mode bank changed on fresh reload '+s.key);
  row.reopenedRoot=get(loaded,[section,...rootPath]);row.assertionsPassed=true;
 }
 complete=true;
}finally{
 a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();disk.flush();
 const sourceClosing=Object.fromEntries(Object.keys(sourceHashes).map(file=>[file,digest(path.resolve(M,file))]));
 fs.writeFileSync(path.join(out,'EXPLICIT-INTENT.json'),JSON.stringify({rows,actualRows:rows.length,selectedNames:selected.map(s=>s.key),complete,source:source??path.join(M,'src/bridge/bootModeGlobalPreferences.ts'),sourceSha256:digest(source??path.join(M,'src/bridge/bootModeGlobalPreferences.ts')),sourceHashes,sourceClosing,scope:'Original real settings repository/native file writer and fresh reopens; fan and sleep are shared-only; no feature engine calls'},null,2),{flag:'wx'});
 assert.deepEqual(sourceClosing,sourceHashes,'Explicit preference source changed during execution');
}

}
