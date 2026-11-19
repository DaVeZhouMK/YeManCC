import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';

const sha=bytes=>createHash('sha256').update(bytes).digest('hex');

// Only the primary App hook is projected. Each non-HUD owner is an explicit
// inert port; new free bindings must be reviewed rather than silently mocked.
const BINDINGS=`
const sibling = (name) => () => { calls.push(name); };
const stopPort = (name) => ({stop:sibling(name)});
let backgroundPresentationDisposed = false, resumeLifecycleActive = true;
let videoPowerProbeGeneration = 0, powerChangeRestoreGeneration = 0;
let videoPowerProbeTimer = null, dynamicBackgroundNoGameTimer = null, resumeRetryTimer = null;
let powerChangeRestoreTimer = null, sleepPlanOptimizationTimer = null, resumeVideoTimer = null;
let sleepPlanOptimizationRequest = null;
const stopUndervoltRuntime = sibling('stopUndervoltRuntime');
const cancelBootModeLaunches = sibling('cancelBootModeLaunches');
const cancelBootModeDeferredMouse = sibling('cancelBootModeDeferredMouse');
let stopBootModeReceipts = sibling('stopBootModeReceipts');
let uiPowerReconciliation = stopPort('uiPowerReconciliation');
let stopSharedPowerWatch = sibling('stopSharedPowerWatch');
let updatePredownloadHost = stopPort('updatePredownloadHost');
let controllerShortcutRuntime = stopPort('controllerShortcutRuntime');
let stopWallpaperPower = sibling('stopWallpaperPower');
let steamWallpaperHost = stopPort('steamWallpaperHost');
let stopSharedGameMedia = sibling('stopSharedGameMedia');
let radialHost = stopPort('radialHost');
let stopDeckySafetyNotifications = sibling('stopDeckySafetyNotifications');
let deckyMirrorHost = stopPort('deckyMirrorHost');
let stopAiFanService = sibling('stopAiFanService');
const resumeQueue = {invalidate:sibling('resumeQueue')};
const sleepPlanOptimizationQueue = {invalidate:sibling('sleepPlanOptimizationQueue')};
let sleepPlanOptimizationConfirmation = stopPort('sleepPlanOptimizationConfirmation');
const networkAccelerationWake = {dispose:sibling('networkAccelerationWake')};
const clearBackgroundRetry = sibling('clearBackgroundRetry');
const invalidateBackgroundMedia = sibling('invalidateBackgroundMedia');
const stopDynamicBackgroundWatch = sibling('stopDynamicBackgroundWatch');
const stopGameProfileAutoMemory = sibling('stopGameProfileAutoMemory');
let stopLosslessRuntimeOwner = sibling('stopLosslessRuntimeOwner');
const stopGamePolicyRuntime = sibling('stopGamePolicyRuntime');
let stopGameTrainerAutoPrepare = sibling('stopGameTrainerAutoPrepare');
let stopUiVisibility = sibling('stopUiVisibility');
const stopGamepad = sibling('stopGamepad'), stopGyroTelemetry = sibling('stopGyroTelemetry');
const stopAcWatch = sibling('stopAcWatch'), stopPowerSourceWatch = sibling('stopPowerSourceWatch');
let stopForegroundPowerSource = sibling('stopForegroundPowerSource');
const stopResumeWatch = sibling('stopResumeWatch'), stopFanResumeWatch = sibling('stopFanResumeWatch');
const stopSchemeWatch = sibling('stopSchemeWatch');
let stopStartupPerformanceWatch = sibling('stopStartupPerformanceWatch');
const inertEvent = () => {};
const onSharedMediaChanged = inertEvent, onWindowPlacementSettingsChanged = inertEvent, onWindowPlacementSummoned = inertEvent;
const onBackgroundChanged = inertEvent, onBackgroundOpacityChanged = inertEvent;
const onBackgroundBlurChanged = inertEvent, onBackgroundPositionChanged = inertEvent;
const onGamepadRefresh = inertEvent, onGamepadTdpDelta = inertEvent, onGamepadBrightness = inertEvent, onGamepadMouseToggle = inertEvent;
const onBackgroundWindowHidden = inertEvent, onBackgroundWindowShown = inertEvent;
const onBackgroundVideoPauseChanged = inertEvent, onFanPhysicalSuspending = inertEvent, onFanGuardState = inertEvent;
const onUiSettingsLoaded = inertEvent, onDynamicBackgroundSettingChanged = inertEvent, onDynamicBackgroundLoaded = inertEvent;
const onGameRulesChanged = inertEvent, onBackgroundVisibilityChange = inertEvent;
`;

export function projectAppUnmount(bytes,{parse},ts,rendererEntry){
  const text=bytes.toString('utf8');
  assert(Buffer.from(text,'utf8').equals(bytes),'APP_SOURCE_MUST_BE_UTF8');
  const {descriptor,errors}=parse(text,{filename:'App.vue'});
  assert.deepEqual(errors,[],'APP_SFC_PARSE');
  const script=descriptor.scriptSetup;
  assert.equal(script?.lang,'ts','APP_SCRIPT_SETUP_REQUIRED');
  assert.equal(text.slice(script.loc.start.offset,script.loc.end.offset),script.content,'APP_SCRIPT_SOURCE_OFFSETS');
  const ast=ts.createSourceFile('App.setup.ts',script.content,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
  assert.deepEqual(ast.parseDiagnostics,[],'APP_SETUP_TS_PARSE');
  const hooks=ast.statements.filter(node=>ts.isExpressionStatement(node)&&ts.isCallExpression(node.expression)&&
    ts.isIdentifier(node.expression.expression)&&node.expression.expression.text==='onUnmounted');
  const matches=hooks.flatMap(hook=>{
    const [callback]=hook.expression.arguments;
    if(hook.expression.arguments.length!==1||!ts.isArrowFunction(callback)||!ts.isBlock(callback.body))return [];
    return callback.body.statements.filter(node=>{
      if(!ts.isExpressionStatement(node)||!ts.isCallExpression(node.expression))return false;
      const target=node.expression.expression;
      return ts.isPropertyAccessExpression(target)&&ts.isIdentifier(target.expression)&&target.expression.text==='monitorHudHost'&&target.name.text==='stop';
    }).map(stop=>({hook,callback,stop}));
  });
  assert.equal(matches.length,1,'UNIQUE_PRIMARY_APP_UNMOUNT_HUD_STOP');
  const [{hook,callback,stop}]=matches;
  assert.equal(stop.expression.arguments.length,0,'APP_STOP_ARGUMENTS');
  const begin=hook.getStart(ast),end=hook.end,raw=script.content.slice(begin,end);
  const stopBegin=stop.getStart(ast)-begin,stopEnd=stop.end-begin;
  const stopRaw=raw.slice(stopBegin,stopEnd),mutated=raw.slice(0,stopBegin)+raw.slice(stopEnd);
  assert.equal(mutated.slice(0,stopBegin)+stopRaw+mutated.slice(stopBegin),raw,'APP_STOP_MUTATION_ROUND_TRIP');
  const startOffset=script.loc.start.offset+begin;
  const projection={source:'src/App.vue',sourceBytes:bytes.length,sourceSha256:sha(bytes),hookIndex:hooks.indexOf(hook),otherRootHooks:hooks.length-1,
    hookByteOffset:Buffer.byteLength(text.slice(0,startOffset)),hookBytes:Buffer.byteLength(raw),hookSha256:sha(raw),
    callbackSha256:sha(callback.getText(ast)),mutationSha256:sha(mutated),removedStatement:stopRaw};
  return {projection,raw,mutated,entry:`
import {createRenderer,h,onUnmounted} from ${JSON.stringify(rendererEntry)};
export function mountAppUnmountProjection(host, skipMonitorStop){
  let monitorHudHost = host;
  const calls = [], errors = [];
  ${BINDINGS}
  const container = {children:[]};
  const renderer = createRenderer({
    insert(node,parent){node.parent=parent;parent.children.push(node);},
    remove(node){const p=node.parent;const index=p.children.indexOf(node);if(index<0)throw Error('MISSING_RENDER_NODE');p.children.splice(index,1);node.parent=null;},
    parentNode:node=>node.parent, nextSibling:()=>null, createElement:tag=>({tag,children:[]}),
    createText:text=>({text}), createComment:text=>({text}), setText:(node,text)=>{node.text=text;},
    setElementText:(node,text)=>{node.text=text;}, patchProp(){},
  });
  const app = renderer.createApp({setup(){
    if(skipMonitorStop){
      ${mutated}
    }else{
      ${raw}
    }
    return ()=>h('app-unmount-projection');
  }});
  app.config.errorHandler=(error,instance,phase)=>{errors.push({error:String(error),phase});};
  app.mount(container);
  return {
    mounted:container.children.length===1, unmount:()=>app.unmount(),
    inspect:()=>({nodeCount:container.children.length,hostReferenceCleared:monitorHudHost===null,
      backgroundPresentationDisposed,resumeLifecycleActive,calls:[...calls],errors:[...errors]}),
  };
}
`};
}
