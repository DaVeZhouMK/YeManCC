import {cloneBootValue,createBootModeDocument,resolveBootMode} from '@/bridge/bootModeModel';
import {screenTouchpadDefaults,screenTouchpadProfile} from '@/bridge/screenTouchpads';
import seed from 'boot-seed';
import {calls} from './runtimePorts';
const copy=cloneBootValue;
for(const persona of ['disabled','elite','steamdeck','dualsense-edge']) seed.touch.profiles[persona]=screenTouchpadDefaults(screenTouchpadProfile(persona));
// Dedicated negative replay only: remembered presets may contain pre-activation native defaults.
// The ordinary preview retains the complete DEFAULTS-like seed from runtimePorts.
if((window as any).__bootSeedDisabledGyroPresets===true)seed.gyro.motion.presets=Object.fromEntries(['fps','racing','custom','steam'].map(key=>[key,{...copy(seed.gyro.motion),enabled:false,outputMode:'disabled',unit:null}]));
let document=createBootModeDocument(seed);
let startupResult:any;
document.bootEnabled=false;
document.controllerTemplates={Fixture:{name:'Fixture',rules:[]}};
const apps=[{name:'Fixture application',path:'C:/fixture/app.exe'}];
const support=Object.fromEntries(['hybridCores','performance','fan','charge','gyro','hibernate'].map(key=>[key,{state:'supported',reason:'Isolated fixture',choices:[60,80,100]}]));
support.gyro={state:'unknown',reason:'Fixture gyro capability remains unknown'};
export const memory={supportReads:[] as string[],holdSupportCategory:'',releaseSupportRead:null as null|(()=>void),applyAttempts:[] as Array<{id:string;startup:unknown}>,connections:[] as Array<{label:string;connected:boolean}>,connectionQueries:0,nextBootNotice:'',overviewReads:0,editorReads:0,holdNextOverviewRead:false,releaseOverviewRead:null as null|(()=>void),requiresCapture:false,holdNextEditorRead:false,releaseEditorRead:null as null|(()=>void),failNextEditorRead:false,holdNextSave:false,releaseSave:null as null|(()=>void),calls:[] as any[],nativeCalls:calls,get document(){return copy(document);},get startupResult(){return startupResult?copy(startupResult):undefined;},completeStartup(mode:any,pending:string[]=[],failed=false){const sourceRevision=document.revision;if(!failed)document={...document,current:mode,activeSnapshot:resolveBootMode(document,mode),revision:document.revision+1};startupResult={schema:1,boot:'fixture-windows-boot-1',mode,sourceRevision,revision:document.revision,status:failed?'failed':pending.length?'partial':'applied',applied:[],skipped:[],pending:copy(pending),...(failed?{error:pending[0]||'fixture startup failed'}:{})};window.dispatchEvent(new CustomEvent('boot-mode:changed',{detail:{mode,applied:[],skipped:[],pending:copy(pending)}}));},failNextSave:false,apps,support};
(window as any).__bootMemory=memory;
export async function loadBootModes(options:any={}){if(options.overviewOnly){memory.overviewReads++;if(memory.holdNextOverviewRead||(window as any).__bootHoldInitialOverview){memory.holdNextOverviewRead=false;(window as any).__bootHoldInitialOverview=false;await new Promise<void>(resolve=>memory.releaseOverviewRead=resolve);memory.releaseOverviewRead=null;}}else {memory.editorReads++;if(memory.holdNextEditorRead){memory.holdNextEditorRead=false;await new Promise<void>(resolve=>memory.releaseEditorRead=resolve);memory.releaseEditorRead=null;}if(memory.failNextEditorRead){memory.failNextEditorRead=false;throw Error('fixture editor refresh failure');}}return {document:copy(document),startupResult:startupResult?copy(startupResult):undefined,apps:copy(apps),requiresCapture:options.overviewOnly&&memory.requiresCapture,support:options.overviewOnly||options.editorOnly?{}:copy(support)};}
export async function saveBootModes(draft:any,expected=draft.revision){memory.calls.push({op:'save',expected,revision:document.revision});if(memory.holdNextSave){memory.holdNextSave=false;await new Promise<void>(resolve=>memory.releaseSave=resolve);memory.releaseSave=null;}if(memory.failNextSave){memory.failNextSave=false;throw new Error('fixture save failure');}if(expected!==document.revision)throw new Error('fixture revision conflict');document={...copy(draft),current:document.current,boot:document.boot,bootEnabled:document.bootEnabled,activeSnapshot:copy(document.activeSnapshot),revision:document.revision+1};return copy(document);}
export async function chooseBootMode(id:any,draft:any,enabled=true){if(draft.revision!==document.revision)throw new Error('fixture revision conflict');memory.calls.push({op:'choose',id,enabled});document={...copy(draft),current:document.current,activeSnapshot:copy(document.activeSnapshot),boot:id,bootEnabled:enabled,revision:document.revision+1};delete document.bootNotice;if(memory.nextBootNotice){document.bootNotice=memory.nextBootNotice;memory.nextBootNotice='';}return copy(document);}
export async function applyBootMode(id:any,draft:any,options:any={}){memory.applyAttempts.push({id,startup:options?.startup});if(options?.startup!==true)throw new Error('模式仅在重启后的下次开机应用');if(draft.revision!==document.revision)throw new Error('fixture revision conflict');memory.calls.push({op:'apply',id});document={...copy(draft),current:id,revision:document.revision+1};return {document:copy(document),receipt:{mode:id,applied:[],skipped:[],pending:[]}};}
export async function bootModeConnections(_id:string){return [];}
export async function addBootSharedApplication(name:string,path:string){apps.push({name,path});}

export async function readBootModeSupport(category?:string){
 memory.supportReads.push(category||'all');
 if(memory.holdSupportCategory===category){memory.holdSupportCategory='';await new Promise<void>(resolve=>memory.releaseSupportRead=resolve);memory.releaseSupportRead=null;}
 const keys=category==='performance'?['hybridCores','performance','charge','fan']:category==='sleep'?['hibernate']:category==='input'?['gyro']:category?[]:Object.keys(support);
 return Object.fromEntries(keys.filter(key=>key in support).map(key=>[key,copy(support[key])]));
}
