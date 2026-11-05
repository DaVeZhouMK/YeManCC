// Read-only release check. UI acceptance is valid only for the exact tested sources.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const evidence=process.argv[2] && path.resolve(process.argv[2]);
if(!evidence)throw Error('Usage: node tools/gamepad_ui_release_gate.mjs <evidence-directory>');
const read=name=>JSON.parse(fs.readFileSync(path.join(evidence,name),'utf8').replace(/^\uFEFF/,''));
const sha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const build=read('build-result.json');
if(process.argv.includes('--performance-ts')){
 const report=read('ts-navigation-gate.json');
 if(!build.pass||!report.pass||report.pageErrors?.length||report.actions?.some(item=>!item.pass))throw Error('TS controller acceptance failed');
 for(const height of [720,800])for(const name of ['performance/simulation/right','performance/simulation/combined-auto','performance/simulation/combined-manual']){
  const item=report.results?.find(row=>row.height===height&&row.name===name);
  if(!item||item.count<1||item.issues?.length||item.errors?.length)throw Error('Missing TS navigation state: '+name+' @ '+height);
 }
 for(const height of [720,800])if(!report.actions?.some(row=>row.height===height&&row.pass))throw Error('TS action/return test missing');
 for(const [relative,expected]of Object.entries(build.sourceHashes||{})){
  const file=path.isAbsolute(relative)?relative:path.join(root,relative);
  if(!fs.existsSync(file)||sha(file)!==expected)throw Error('Tested UI source changed: '+relative);
 }
 const detail={pass:true,scope:'performance-ts-only',physicalController:false,fullUiAccepted:false,states:report.results.length,actions:report.actions.length,sourceFiles:Object.keys(build.sourceHashes||{}).length};
 fs.writeFileSync(path.join(evidence,'ts-release-gate.json'),JSON.stringify(detail,null,2));console.log(JSON.stringify(detail));process.exit(0);
}
const report=read('navigation-gate.json');
if(!build.pass || !report.pass || !report.inputsUnchanged || report.failures?.length || report.pageErrors?.length)throw Error('Controller acceptance failed or incomplete');
if(process.argv.includes('--gyro-control')){
 if(report.scope!=='gyro-control'||report.fullUiAccepted!==false||report.changedSourceInputs?.length)throw Error('Gyro control selected scope invalid');
 const cases='tools/ymcc_sim/global/ui-navigation/templates/gyroNavigationCases.mjs';
 if(sha(path.join(root,cases))!==sha(path.join(evidence,'gyroNavigationCases.mjs')))throw Error('Gyro acceptance runner differs from canonical cases');
 const {gyroControlSelection}=await import('./ymcc_sim/global/ui-navigation/templates/gyroNavigationCases.mjs');
 for(const height of [720,800]){
  const actions=report.actions.filter(row=>row.height===height);
  if(actions.some(row=>row.pass!==true)||JSON.stringify(actions.map(row=>row.name).sort())!==JSON.stringify([...gyroControlSelection.actions].sort()))throw Error('Gyro exact action selection mismatch @ '+height);
  for(const name of gyroControlSelection.states){
   const states=report.results.filter(row=>row.height===height&&row.name===name);
   if(states.length!==1||states.some(row=>row.count<1||row.issues?.length||row.errors?.length))throw Error('Missing or failed gyro navigation state: '+name+' @ '+height);
  }
 }
 for(const [relative,expected] of Object.entries(build.sourceHashes||{})){
  const file=path.isAbsolute(relative)?path.resolve(relative):path.resolve(root,...relative.split(/[\\/]/));
  const within=(base,target)=>{const rel=path.relative(base,target);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
  const actual=fs.realpathSync(file);
  if(!(within(root,file)&&within(fs.realpathSync(root),actual))&&!(within(evidence,file)&&within(fs.realpathSync(evidence),actual)))throw Error('Untested current source outside source/evidence: '+relative);
  if(sha(actual)!==expected)throw Error('Tested UI source changed: '+relative);
 }
 for(const required of ['src/gamepad/engine.ts','src/gamepad/spatial.ts','src/gamepad/focus.ts','src/components/GyroInputOutputPreview.vue','src/components/GyroStatusPreview.vue','src/views/GyroMotionView.vue','src/components/GyroSimulationWindow.vue'])if(!Object.keys(build.sourceHashes||{}).some(file=>file.replaceAll('\\','/')===required))throw Error('Gyro original consumer source missing: '+required);
 for(const required of ['build-result.json','site/index.html','site/product.js','site/product.css','verify.mjs','engineAudit.ts','entry.ts','gyroNavigationCases.mjs'])if(!report.testedInputs?.[required])throw Error('Missing gyro test input fingerprint: '+required);
 for(const [relative,expected] of Object.entries(report.testedInputs||{})){
  const file=path.resolve(evidence,...relative.split(/[\\/]/));
  if(!file.startsWith(evidence+path.sep)||sha(file)!==expected)throw Error('Acceptance input changed: '+relative);
 }
 const detail={pass:true,scope:'gyro-control',fullUiAccepted:false,physicalController:false,actions:report.actions.length,states:report.results.length,directions:report.directions,sourceFiles:Object.keys(build.sourceHashes||{}).length};
 fs.writeFileSync(path.join(evidence,'gyro-control-release-gate.json'),JSON.stringify(detail,null,2));console.log(JSON.stringify(detail));process.exit(0);
}
if(process.argv.includes('--lossless')){
 if(report.scope!=='lossless'||report.fullUiAccepted!==false||report.changedSourceInputs?.length)throw Error('Lossless selected scope invalid');
 const name='Lossless one-click toggle preserves parameters and unbinds only the current EXE; lamp is process-only';
 if(report.actions?.length!==2||report.actions.some(row=>row.name!==name||row.pass!==true)||new Set(report.actions.map(row=>row.height)).size!==2)throw Error('Lossless exact original action selection mismatch');
 for(const height of [720,800]){
  if(!report.actions.some(row=>row.height===height&&row.name===name))throw Error('Missing Lossless action viewport');
  const states=report.results?.filter(row=>row.height===height&&row.name==='top-game-menu/lossless-button');
  if(!states?.length||states.some(row=>row.count<1||row.issues?.length||row.errors?.length))throw Error('Missing Lossless real-engine navigation state');
  const popup=report.results?.find(row=>row.height===height&&row.name==='top-game-menu/lossless-actions');
  if(!popup||popup.count!==3||popup.issues?.length||popup.errors?.length)throw Error('Missing Lossless popup navigation and B dismissal coverage');
 }
 for(const [relative,expected]of Object.entries(build.sourceHashes||{})){
  const file=path.isAbsolute(relative)?path.resolve(relative):path.resolve(root,...relative.split(/[\\/]/));
  const within=(base,target)=>{const rel=path.relative(base,target);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
  const actual=fs.realpathSync(file);
  if(!(within(root,file)&&within(fs.realpathSync(root),actual))&&!(within(evidence,file)&&within(fs.realpathSync(evidence),actual)))throw Error('Untested current source outside source/evidence: '+relative);
  if(sha(actual)!==expected)throw Error('Tested UI source changed: '+relative);
 }
 for(const required of ['src/gamepad/engine.ts','src/gamepad/spatial.ts','src/gamepad/focus.ts','src/components/GameQuickActions.vue','src/components/GameLosslessMenu.vue','src/bridge/quickapp.ts'])if(!Object.keys(build.sourceHashes||{}).some(file=>file.replaceAll('\\','/')===required))throw Error('Lossless original consumer source missing: '+required);
 for(const required of ['build-result.json','site/index.html','site/product.js','site/product.css','verify.mjs','engineAudit.ts','entry.ts','trainerMenuCases.mjs'])if(!report.testedInputs?.[required])throw Error('Missing Lossless test input fingerprint: '+required);
 for(const [relative,expected]of Object.entries(report.testedInputs||{})){
  const file=path.resolve(evidence,...relative.split(/[\\/]/));
  if(!file.startsWith(evidence+path.sep)||sha(file)!==expected)throw Error('Acceptance input changed: '+relative);
 }
 const detail={pass:true,scope:'lossless',fullUiAccepted:false,physicalController:false,actions:report.actions.length,states:report.results.length,sourceFiles:Object.keys(build.sourceHashes||{}).length};
 fs.writeFileSync(path.join(evidence,'lossless-release-gate.json'),JSON.stringify(detail,null,2));console.log(JSON.stringify(detail));process.exit(0);
}

const required=['/radial','radial/editor','radial/editor-children','radial/recorder','/fan','/schedule','/button-mapping','/gyro-motion','/settings','/power','/rtss','/steam','/sleep','/quick',
 'settings/battery-charge','settings/fxsound-repair','/settings/device','performance/manual/dedicated','performance/manual/global','performance/auto/global',
 'top-game-menu/dedicated/auto','top-game-menu/dedicated/manual','controller/editor',
 'performance/simulation/right','performance/simulation/combined-auto','performance/simulation/combined-manual','controller/editor-natural-panes','controller/steam-mouse','controller/standard-mouse','gyro/simulation','gyro/simulation-advanced','gyro/simulation-delete','settings/log-confirm','monitor/appearance-expanded'];
for(const height of [720,800])for(const name of required){
 const item=report.results?.find(item=>item.height===height&&item.name===name);
 if(!item || item.count<1 || item.issues?.length || item.errors?.length)throw Error(`Missing or failed state: ${name} @ ${height}`);
}
if(!report.actions?.length || report.actions.some(item=>!item.pass))throw Error('Controller action/return tests missing or failed');
for(const height of [720,800])for(const name of [
 'Wallpaper preview uses lightweight motion without recursive UI or default video',
 'Wallpaper preview plays selected media and commits drag position only on release',
 'Wallpaper preview cancels pointer drafts and supports keyboard and gamepad recenter',
 'Wallpaper preview gamepad owns both axes, A saves and B cancels',
 'Wallpaper preview pauses hidden media and releases it when settings tab closes',
 'Gyro native full-screen owner accepts motion before DOM focus settles',
 'Gyro native DOM blur keeps the admitted owner; real native blur ends it',
 'Gyro native ipc:window.hidden stops input and ipc:window.shown never auto plays',
 'Gyro native ipc:window.minimized stops input and ipc:window.restored never auto plays',
 'Gyro native ipc:power.suspending stops input and ipc:power.resumed never auto plays',
 'Gyro safe-zero observation cannot become active motion in the full-screen simulator',
 'Gyro native FPS receives aiming and firing while DOM focus is unsettled',
 'Gyro H5 still requires browser focus and stops on browser blur',
 'Gyro preset: native revision conflict can switch racing back to FPS',
 'Gyro preset: choices during a held save keep the latest request and remain reachable',
 'G3E missed power commit: native Ready restores gyro and actual queued gamepad commands',
 'G3E next real sleep stays closed; showing the window cannot wake or restart it',
 'G3E late native snapshot cannot release a newer sleep and focus can reconcile its own Ready',
 'G3E presentation recovery does not invoke a power commit, input restart or hardware action',
])if(!report.actions.some(action=>action.height===height&&action.name===name&&action.pass))throw Error('Missing gyro full-screen input ownership regression: '+name+' @ '+height);

for(const [relative,expected] of Object.entries(build.sourceHashes||{})){
 const file=path.isAbsolute(relative)?path.resolve(relative):path.resolve(root,...relative.split(/[\\/]/));
 const within=(base,target)=>{const rel=path.relative(base,target);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
 const actual=fs.realpathSync(file);
 if(!(within(root,file)&&within(fs.realpathSync(root),actual))&&!(within(evidence,file)&&within(fs.realpathSync(evidence),actual)))throw Error('Untested current source outside source/evidence: '+relative);
 if(sha(actual)!==expected)throw Error('Untested current source: '+relative);
}
for(const requiredFile of ['src/gamepad/engine.ts','src/gamepad/spatial.ts','src/gamepad/focus.ts','src/gamepad/performanceNavigation.ts']){
 if(!Object.keys(build.sourceHashes).some(file=>file.replaceAll('\\','/')===requiredFile))throw Error('Production navigation source not included: '+requiredFile);
}
for(const requiredInput of ['build-result.json','site/index.html','site/product.js','site/product.css','verify.mjs','engineAudit.ts','entry.ts']){
 if(!report.testedInputs?.[requiredInput])throw Error('Missing test input fingerprint: '+requiredInput);
}
for(const [relative,expected] of Object.entries(report.testedInputs||{})){
 const file=path.resolve(evidence,...relative.split(/[\\/]/));
 if(!file.startsWith(evidence+path.sep) || sha(file)!==expected)throw Error('Acceptance input changed: '+relative);
}
console.log(`Controller UI gate passed: ${report.states} states / ${report.directions} directions / ${report.actionCases} action cases; exact source match. Physical-controller acceptance: ${report.physicalController===true?'recorded':'not performed'}.`);
