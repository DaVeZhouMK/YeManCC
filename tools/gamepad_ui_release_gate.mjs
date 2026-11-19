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
// Existing boot-mode component consumer. No new test ID or default parent gate.
if(process.argv.includes('--boot-modes')){
 const report=read('boot-modes-browser.json');
 if(!report.pass||report.scope!=='all'||report.wholeSuite!=='executed'||report.fullSelectionCompleted!==true||report.collectFailures!==false||report.notRun?.length||report.counts?.passed!==121||report.counts?.failed!==0||report.assertions?.length!==121||report.assertions.some(row=>!row.pass)||report.pageErrors?.length||report.networkRejected?.length)throw Error('Boot-mode complete original UI selection is not accepted');
 if(new Set(report.assertions.map(row=>String(row.height)+'\0'+row.name)).size!==121)throw Error('Boot-mode original case identities are duplicated');
 const required=['src/gamepad/engine.ts','src/gamepad/focus.ts','src/gamepad/spatial.ts','src/views/PowerView.vue','src/components/IntegratedPage.vue','src/components/BootModesPanel.vue','src/components/BootModeWorkbench.vue','src/components/BootModeFields.vue','src/components/ScreenTouchpadsSettings.vue','src/components/Dropdown.vue','src/components/Slider.vue','src/components/Toggle.vue','src/bridge/bootModeModel.ts','src/bridge/bootModes.ts','tools/boot_modes_browser_selftest.mjs'];
 for(const file of required)if(typeof report.sourceHashes?.[file]!=='string')throw Error('Actual boot-mode UI source missing: '+file);
 for(const [relative,expected]of Object.entries(report.sourceHashes)){
  const file=path.isAbsolute(relative)?relative:path.join(root,relative);
  if(!fs.existsSync(file)||sha(file)!==expected)throw Error('Boot-mode tested UI source changed: '+relative);
 }
 for(const height of [720,800]){
  if(report.assertions.filter(row=>row.height===height).length!==60)throw Error('Boot-mode viewport case selection incomplete: '+height);
  const viewport=report.initialViewports?.find(row=>row.height===height),shell=report.shells?.find(row=>row.height===height),layouts=report.layouts?.filter(row=>row.height===height);
  if(!viewport?.powerView||!viewport.integratedPage||viewport.viewport.width!==1280||viewport.viewport.height!==height||!shell||!layouts?.length||layouts.some(row=>!row.controls||!row.checks||row.failures?.length))throw Error('Boot-mode actual shell/rows incomplete: '+height);
  for(const name of ['A opens actual Dropdown; B cancels and restores its trigger; A confirms','Slider changes only in A edit mode; B restores the old value; second A commits','saved editor B returns to the panel; hidden and disabled controls never enter candidates','actual wrapper keeps startup and system tools visible and releases width owners and receipt listener on teardown'])if(!report.assertions.some(row=>row.height===height&&row.name===name&&row.pass))throw Error('Boot-mode original action contract missing: '+name+' @ '+height);
 }
 const result={pass:true,scope:'boot-modes-only',bootModesUiAccepted:true,fullUiAccepted:false,physicalController:false,installedApplication:false,originalCases:121,directionChecks:report.layouts.reduce((sum,row)=>sum+row.checks,0),viewports:[{width:1280,height:720},{width:1280,height:800}],sourceFiles:Object.keys(report.sourceHashes).length,originalReport:{path:path.join(evidence,'boot-modes-browser.json'),sha256:sha(path.join(evidence,'boot-modes-browser.json'))},gateSource:{path:fileURLToPath(import.meta.url),sha256:sha(fileURLToPath(import.meta.url))}};
 fs.writeFileSync(path.join(evidence,'boot-modes-release-gate.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));process.exit(0);
}
// Selected existing TaskView menu fixture; no full-navigation/physical claim.
if(process.argv.includes('--task-view')){
 const selected=process.argv.includes('--selection')?process.argv[process.argv.indexOf('--selection')+1]:'task-view-ui.json';if(typeof selected!=='string'||path.basename(selected)!==selected)throw Error('Invalid TaskView UI selection');
 const selection=read(selected);
 if(!Array.isArray(selection.reports)||selection.reports.length!==2||selection.reports.some(folder=>typeof folder!=='string'||!folder||folder.includes('/')||folder.includes('\\')||folder==='.'||folder==='..')||new Set(selection.reports).size!==2)throw Error('TaskView exact two-resolution selection missing');
 const required=['src/components/GameRecognitionControl.vue','src/components/GameQuickActions.vue','src/gamepad/engine.ts','src/gamepad/focus.ts','src/gamepad/spatial.ts','src/gamepad/performanceNavigation.ts','src/gamepad/activationEvidence.ts','src/gamepad/taskViewTouchActivation.ts','src/bridge/api.ts','src/bridge/ipc.ts','tools/task_view_menu_browser_selftest.mjs'];
 const names=['Actual engine directional navigation obeys row order and edges; B closes and Y restores focus','Semantic Y opens real Vue menu and gives 切换程序 DOM/visual focus without IPC','Two direct touchscreen activations use the same Shell/minimize chain without another injected contact','Touch no-delivery causes no Shell replay and permits a later explicit activation','Touch no-event causes no Shell replay and permits a later explicit activation'];
 let actions=0,directions=0;
 for(const [index,folder]of selection.reports.entries()){
  const directory=path.join(evidence,folder),report=JSON.parse(fs.readFileSync(path.join(directory,'results.json'),'utf8'));
  if(!report.ok||report.failure||report.errors?.length||report.blockedRequests?.length||report.findings?.length||!report.serverClosed||!report.browserClosed||report.viewport?.width!==1280||report.viewport?.height!==[720,800][index])throw Error('TaskView browser acceptance incomplete '+folder);
  if(report.cases?.length!==18||report.navigation?.length!==10||report.navigation.some(row=>row.visible!==true)||names.some(name=>!report.cases.some(row=>row.name===name)))throw Error('TaskView actual activation/navigation selection missing '+folder);
  for(const file of required)if(!report.sourceHashes.some(row=>row.path===file))throw Error('TaskView production source missing '+file);
  for(const row of report.sourceHashes){const file=path.resolve(root,row.path),rel=path.relative(root,file);if(rel==='..'||rel.startsWith('..'+path.sep)||path.isAbsolute(rel)||sha(file)!==row.sha256)throw Error('TaskView tested source changed '+row.path)}
  for(const [name,expected]of [['fixture.js',report.hashes.fixtureJs],['fixture.html',report.hashes.fixtureHtml]])if(sha(path.join(directory,name))!==expected)throw Error('TaskView built input changed '+name);
  if(report.fixture.hardwareQueries.length||report.fixture.requests.some(row=>!['window.touchTaskViewButton','shell.taskView','window.minimize'].includes(row.cmd)))throw Error('TaskView fixture escaped its IPC scope');
  actions+=report.cases.length;directions+=report.navigation.length;
 }
 const result={pass:true,scope:'task-view-selected-activation-and-navigation',actions,directions,physicalController:false,actualWindowsInput:false,fullUiAccepted:false,toolSha256:sha(fileURLToPath(import.meta.url))};
 const receipt=selection.receipt||'task-view-ui-current-release-gate.json';if(typeof receipt!=='string'||path.basename(receipt)!==receipt||!receipt.endsWith('.json'))throw Error('Invalid TaskView gate receipt name');
 fs.writeFileSync(path.join(evidence,receipt),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));process.exit(0);
}

if(process.argv.includes('--lighting-ownership')){
 const summary=read('summary.json'),report=read('browser-results.json');
 if(!summary.ownershipOnly||!summary.allPassed||!summary.sourceStable||summary.exit!==0||summary.fullUiAccepted!==false||summary.hardwareIo!==false||summary.installedApplicationStarted!==false||!report.ok||report.errors?.length)throw Error('Lighting ownership selected UI acceptance failed');
 const catalog=JSON.parse(fs.readFileSync(path.join(root,'capabilities/hc-device-catalog.json'),'utf8').replace(/^\uFEFF/,''));
 const rows=catalog.domains.lighting.oemOwnership.filter(row=>['hc.device.AYANEOAIR','hc.device.AYANEOAIR1S','hc.device.AYANEOAIRPlusAMD','hc.device.AYANEOSlide'].includes(row.deviceId));
 if(rows.length!==4)throw Error('Missing lighting ownership UI representatives');
 const expected=[720,800].flatMap(height=>[...rows.map(row=>'ownership:'+row.deviceId+'-'+height),'ownership:unsupported-'+height,'ownership:other-family-'+height]);
 if(JSON.stringify(report.cases)!==JSON.stringify(expected))throw Error('Lighting ownership exact selection mismatch');
 const required=['src/components/ControllerFeedbackCapabilitiesCard.vue','src/components/Dropdown.vue','src/components/Slider.vue','src/components/Toggle.vue','src/components/InlineIcon.vue','src/bridge/api.ts','src/gamepad/sliderRepeat.ts','src/gamepad/engine.ts','src/gamepad/focus.ts','src/gamepad/spatial.ts','src/gamepad/performanceNavigation.ts','src/styles/tokens.css','capabilities/hc-device-catalog.json','tools/controller_lighting_operations_browser_selftest.mjs','tools/controller_lighting_lifecycle_browser_selftest.mjs'];
 for(const file of required)if(typeof summary.sourceHashes?.[file]!=='string'||sha(path.join(root,file))!==summary.sourceHashes[file])throw Error('Lighting ownership tested source changed: '+file);
 if(path.resolve(summary.source)!==path.join(root,required[0]))throw Error('Lighting ownership requires actual current component');
 const detail={pass:true,scope:'lighting-ownership',fullUiAccepted:false,physicalController:false,cases:expected,sourceFiles:required.length,inputs:{summary:sha(path.join(evidence,'summary.json')),results:sha(path.join(evidence,'browser-results.json')),probe:sha(path.join(evidence,'browser-probe.mjs'))}};
 fs.writeFileSync(path.join(evidence,'lighting-ownership-release-gate.json'),JSON.stringify(detail,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(detail));process.exit(0);
}

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
if(process.argv.includes('--custom-touch')){
 if(report.scope!=='custom-touch'||report.fullUiAccepted!==false||report.changedSourceInputs?.length)throw Error('Custom touch selected scope invalid');
 const names=['global Ins default and gamepad off','keyboard selection, cancel and unlimited additions','templates affect only appearance and retain mappings','slider A commits and B cancels without writing','fullscreen editor save, cancel and reset contract','failed save preserves last committed settings','reentry retains global keys and deletion remains reachable'];
 for(const height of [720,800]){for(const name of names)if(!report.actions.some(row=>row.height===height&&row.name==='Custom touch: '+name&&row.pass))throw Error('Missing custom touch action '+name+' @ '+height);for(const name of ['default','keyboard','gamepad','many-keys']){const row=report.results.find(row=>row.height===height&&row.name==='custom-touch/'+name);if(!row||!row.count||row.issues?.length||row.errors?.length)throw Error('Missing custom touch navigation '+name+' @ '+height);}}
 for(const file of ['src/components/CustomTouchSettings.vue','src/views/TouchMouseView.vue','src/bridge/customTouch.ts','src/gamepad/engine.ts'])if(!Object.keys(build.sourceHashes).some(item=>item.replaceAll('\\','/')===file))throw Error('Actual custom touch source missing '+file);
 for(const [relative,expected]of Object.entries(build.sourceHashes))if(sha(path.isAbsolute(relative)?relative:path.join(root,relative))!==expected)throw Error('Tested source changed '+relative);
 if(sha(path.join(root,'tools/ymcc_sim/global/ui-navigation/templates/customTouchCases.mjs'))!==sha(path.join(evidence,'customTouchCases.mjs')))throw Error('Custom touch canonical cases changed');
 const detail={pass:true,scope:'custom-touch',fullUiAccepted:false,physicalController:false,states:report.states,actions:report.actions.length,sourceFiles:Object.keys(build.sourceHashes).length};fs.writeFileSync(path.join(evidence,'custom-touch-release-gate.json'),JSON.stringify(detail,null,2),{flag:'wx'});console.log(JSON.stringify(detail));process.exit(0);
}
if(process.argv.includes('--overlay')){
 if(report.scope!=='overlay'||report.fullUiAccepted!==false||report.changedSourceInputs?.length)throw Error('Overlay selected scope invalid');
 const names=['native-summon-retained-without-document-focus','old-session-native-event-rejected','slider-edit-survives-nonactivating-blur','formal-native-navigation-switches-one-page','queued-message-cancelled-by-close-and-new-session','monitor-editor-retains-draft-and-focus-session','hidden-session-rejects-late-native-message','ordinary-native-blur-still-revokes','floating-summon-default-and-gamepad-toggle-persist','floating-summon-off-survives-reentry-and-can-return'].map(name=>'Overlay: '+name);
 if(report.actions.length!==20)throw Error('Overlay exact selection incomplete');
 for(const height of [720,800]){
  const actual=report.actions.filter(row=>row.height===height);
  if(JSON.stringify(actual.map(row=>row.name))!==JSON.stringify(names)||actual.some(row=>!row.pass))throw Error('Overlay exact semantic/lifecycle selection mismatch');
  for(const route of ['/schedule','/fan','/button-mapping','/touch-mouse','/gyro-motion','/steam','/power','/sleep','/rtss','/settings','/quick']){
   const row=report.results.find(row=>row.height===height&&row.name==='overlay/ordinary'+route);
   if(!row||row.count<1||row.issues?.length||row.errors?.length)throw Error('Overlay ordinary route incomplete '+route);
  }
 }
 for(const [relative,expected]of Object.entries(build.sourceHashes||{}))if(sha(path.isAbsolute(relative)?relative:path.join(root,relative))!==expected)throw Error('Tested overlay source changed: '+relative);
 for(const [relative,expected]of Object.entries(report.testedInputs||{}))if(sha(path.join(evidence,relative))!==expected)throw Error('Overlay fixture input changed: '+relative);
 if(sha(path.join(root,'tools/ymcc_sim/global/ui-navigation/templates/overlayPresentationCases.mjs'))!==sha(path.join(evidence,'overlayPresentationCases.mjs')))throw Error('Overlay cases changed');
 const detail={pass:true,scope:'overlay',fullUiAccepted:false,physicalController:false,actualGame:false,states:report.states,actions:report.actions.length};
 fs.writeFileSync(path.join(evidence,'overlay-release-gate.json'),JSON.stringify(detail,null,2));console.log(JSON.stringify(detail));process.exit(0);
}
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
if(process.argv.includes('--trainer')){
 if(report.scope!=='trainer'||report.fullUiAccepted!==false||report.changedSourceInputs?.length)throw Error('Trainer selected scope invalid');
 if(sha(path.join(root,'tools/ymcc_sim/global/ui-navigation/templates/trainerMenuCases.mjs'))!==sha(path.join(evidence,'trainerMenuCases.mjs')))throw Error('Trainer canonical cases changed');
 for(const height of [720,800]){
  for(const seed of ['no-scan','delayed-scan']){
   const item=report.results?.find(row=>row.height===height&&row.name==='top-game-menu/trainer/'+seed);
   if(!item||item.count<1||item.issues?.length||item.errors?.length)throw Error('Missing trainer navigation state');
  }
  const actions=report.actions.filter(row=>row.height===height);
  if(actions.length!==9||actions.some(row=>row.pass!==true)||!actions.some(row=>row.name==='Trainer launch keeps failure and uncertain receipt visible in the submenu')||!['controller B','mouse close'].every(method=>actions.some(row=>row.name==='Scanner pending startup is cancelled by '+method+' without late launch')))throw Error('Trainer exact action selection incomplete');
 }
 for(const required of ['src/gamepad/engine.ts','src/gamepad/focus.ts','src/components/GameQuickActions.vue','src/components/GameTrainerMenu.vue','src/components/GameValueScanner.vue','src/bridge/gameTrainer.ts','src/bridge/gameValueScan.ts','src/bridge/trainerLifecycle.ts'])if(!Object.keys(build.sourceHashes||{}).some(file=>file.replaceAll('\\','/')===required))throw Error('Trainer actual source missing: '+required);
 for(const [relative,expected]of Object.entries(build.sourceHashes||{}))if(sha(path.isAbsolute(relative)?relative:path.join(root,relative))!==expected)throw Error('Tested trainer UI source changed: '+relative);
 for(const [relative,expected]of Object.entries(report.testedInputs||{}))if(sha(path.join(evidence,relative))!==expected)throw Error('Trainer evidence changed: '+relative);
 const detail={pass:true,scope:'trainer',fullUiAccepted:false,physicalController:false,actions:report.actions.length,states:report.results.length,sourceFiles:Object.keys(build.sourceHashes||{}).length};
 fs.writeFileSync(path.join(evidence,'trainer-release-gate.json'),JSON.stringify(detail,null,2),{flag:'wx'});console.log(JSON.stringify(detail));process.exit(0);
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
