import {pathToFileURL} from 'node:url';
import { verifyAssertions, sha256 } from './common.mjs';
function replaceOnce(text,old,next,reason,changes){const count=text.split(old).length-1;if(count!==1)throw new Error('STALE_FIXTURE_ANCHOR:'+reason+':'+count);changes.push({reason,before:old,after:next});return text.replace(old,next);}
export function adaptTest(spec,original,{auditRuntime}){
 const changes=[];
 let text=original.replace(/^\uFEFF/,'');
 if(spec.id==='consumer-lifecycle'){
  text=replaceOnce(text,"plugins:[{name:'lifecycle-no-io'","plugins:[__sourcePlugin,{name:'lifecycle-no-io'",'restrict and record exact compiled product modules',changes);
  text=replaceOnce(text,"const offTop=hub.acquire(",`globalThis.__monitorRetainedExports=m;const originalCheckPush=checks.push.bind(checks);checks.push=(...names)=>{for(const name of names)__trace.push({id:'consumer-lifecycle-'+(__trace.length+1),input:{fixture:name,clock:now},consumer:'createMonitorDataHub -> createLatestDisplayFeed -> createHudSampleSource -> age lease',state:{reads,activeSnapshotBindings,sourceLeases,tdpBindings:tdpListeners.size,users:source.users(),desired:hub.desired(),timers:timers.size,calls:structuredClone(calls)},feedback:'original assertions passed',status:'PASS'});return originalCheckPush(...names)};\nconst offTop=hub.acquire(`,'capture each passed original checkpoint state without changing assertions',changes);
 }else if(spec.id==='clock-demand'){
  text=replaceOnce(text,"['./api','./ipc','./monitorData','./monitorGameLogo','./monitorHardwareMetadata']","['./api','./ipc','./monitorData','./monitorGameLogo','./monitorHardwareMetadata','./monitorNoticeHost','./gamePolicyTarget']",'isolate newly imported notification/game-policy ports',changes);
  text=replaceOnce(text,"if(a.path==='./api')text=",`if(a.path==='./monitorNoticeHost')text='export function startMonitorNoticeHost(){return{update(){},ready(){},stop(){}}}';\n   else if(a.path==='./gamePolicyTarget')text='export function getPolicyGame(){throw Error("Unexpected game-policy IO")}';\n   else if(a.path==='./api')text=`,'notification/identity ports have no window/game access',changes);
  text=replaceOnce(text,"export function subscribeMonitorSnapshot(fn)","export function isOwnedMonitorDisplaySnapshot(){return false}export function subscribeMonitorSnapshot(fn)",'new snapshot authority export rejects unowned synthetic snapshots',changes);
  text=replaceOnce(text,"export async function readMonitorOutputPreferences(){return "+"'+r+'.preferences}export function registerYmccHudOutputAdapter()", "export async function saveMonitorHudPlacement(){throw Error(\"Unexpected placement persistence\")}export async function readMonitorOutputPreferences(){return '+r+'.preferences}export function registerYmccHudOutputAdapter()",'new placement persistence export fails if invoked',changes);
  text=replaceOnce(text,"let text=fs.readFileSync(a.path,'utf8');","let text=__recordSource(a.path);",'pin and whitelist current/before compiled modules',changes);
  text=replaceOnce(text,"checks.push(name)","__trace.push({id:'clock-demand-'+(__trace.length+1),input:{fixture:name},consumer:'real model/sample source/preview/host clock-selection and hidden relay',state:Object.fromEntries(Object.entries(io).map(([side,h])=>[side,{formats:h.formats,reads:h.reads,demands:h.demands,clockUsers:h.clockUsers,snapshotBindings:h.snapshotListeners.size,tdpBindings:h.tdpListeners.size}])),feedback:'original assertions passed',status:'PASS'});checks.push(name)",'record original passing checks and demand ownership',changes);
 }else if(spec.id==='bounded-identity'){
  text=replaceOnce(text,"platform:'node',format:'esm'}","platform:'node',format:'esm',plugins:[__sourcePlugin]}",'restrict and record exact compiled model dependencies',changes);
  text=replaceOnce(text,"assert(v,n);++checks","assert(v,n);__trace.push({id:'bounded-identity-'+(__trace.length+1),input:{fixture:n},consumer:'monitorHudModel.boundedHudData / sameHudData',state:{assertionValue:v},feedback:'original assertion passed',status:'PASS'});++checks",'capture original identity assertions without cloning logic',changes);
 }
 const assertionContract=verifyAssertions(original,text);
 const prefix=`import { sourcePlugin as __sourcePlugin, recordSource as __recordSource, trace as __trace } from ${JSON.stringify(pathToFileURL(auditRuntime).href)};\n`;
 return {text:prefix+text,adaptations:changes,assertionContract,originalSha256:sha256(original),adaptedSha256:sha256(prefix+text)};
}
