import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{createRequire}from'node:module';import{fileURLToPath}from'node:url';
export const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
export const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function pins(files){return Object.fromEntries([...new Set(files)].map(f=>[f,hash(fs.readFileSync(path.join(root,f)))]));}
export function current(expected){const mismatch=[];for(const[f,p]of Object.entries(expected)){let actual;try{actual=hash(fs.readFileSync(path.join(root,f)));}catch{}if(actual!==p)mismatch.push(f);}if(mismatch.length)throw Error('OUTPUT_SOURCE_DRIFT:'+mismatch.join(','));}
export function assertionCalls(text){const ts=createRequire(path.join(root,'package.json'))('typescript'),tree=ts.createSourceFile('retained.mjs',text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS),calls=[];if(tree.parseDiagnostics.length)throw Error('RETAINED_PARSE_FAILED');function walk(n){if(ts.isCallExpression(n)&&/^(check|assert(?:\.\w+)?)$/.test(n.expression.getText(tree)))calls.push(n.getText(tree));ts.forEachChild(n,walk);}walk(tree);return calls;}
export const productInputs=[
 'src/bridge/monitorOutput.ts','src/bridge/monitorOutputCoordinator.ts','src/bridge/monitorLayout.ts','src/bridge/monitorHudClock.ts',
 'src/monitorHudOutputEntry.ts','src/monitorNoticeOutputEntry.ts','src/bridge/monitorHudRenderer.ts','src/bridge/monitorHudLayoutLane.ts',
 'src/bridge/monitorHudViewport.ts','src/bridge/monitorNoticePlacement.ts','src/bridge/monitorShortcutNotice.ts','src/bridge/monitorHudTransportLease.ts',
 'src/bridge/monitorHudModel.ts','src/bridge/monitorHudFreshness.ts','src/bridge/monitorHudProtocol.ts','src/bridge/desktopHudProtocol.ts',
 'src/bridge/monitorHudDrag.ts','src/bridge/monitorSnapshotSignals.ts','src/bridge/monitorFrameTime.ts','src/bridge/monitorCoreTiles.ts',
 'src/bridge/monitorHudFpsVisibility.ts','src/bridge/monitorBarPacking.ts','src/bridge/monitorHudUnderlayDemo.ts','src/bridge/monitorHudUnderlayAc8.ts',
 'src/bridge/monitorHudWire.ts','src/bridge/monitorNativeDemand.ts','src/bridge/monitorHudSampleSource.ts','src/bridge/desktopHudOwner.ts',
 'src/bridge/monitorHudCarrierRouter.ts','src/bridge/steamHudRelay.ts','src/bridge/shortcutActionFeedback.ts','src/bridge/frameRateModel.ts',
 'src/components/MonitorHudTemplate.vue','src/components/MonitorCrosshair.vue','src/components/MonitorShortcutNotice.vue',
 'src/components/MonitorHoneycombFrame.vue','src/components/MonitorHoneycombDigits.vue','src/bridge/monitorHoneycombStyle.ts',
 'src/styles/tokens.css','src/styles/monitorHudOutput.css','tools/monitor_hud_vue_build.mjs',
];
export const retained=[{id:'templates',source:'tools/monitor_output_templates_selftest.mjs',receipt:'TEMPLATE-MODEL-SELFTEST.json'},
 {id:'control-clock',source:'tools/monitor_hud_control_clock_selftest.mjs',receipt:'CONTROL-CLOCK-SELFTEST.json'}];
export function createAuditPlugin(loaded){return{name:'output-source-pins',setup(b){b.onLoad({filter:/\.ts$/},args=>{const relative=path.relative(root,args.path).replaceAll('\\','/');if(!productInputs.includes(relative))throw Error('UNREVIEWED_OUTPUT_SOURCE:'+relative);const bytes=fs.readFileSync(args.path);loaded.set(relative,hash(bytes));return{contents:bytes.toString('utf8'),loader:'ts',resolveDir:path.dirname(args.path)};});}};}
