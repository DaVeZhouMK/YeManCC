import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:sha(b)};};
export function renderT1CloseBorrow({repo,source,selection,consumerPath}){
 const sourceBytes=fs.readFileSync(source),text=sourceBytes.toString('utf8').replace(/\r\n/g,'\n');
 const root=path.dirname(fileURLToPath(import.meta.url)),template=path.join(root,selection.template);
 const names=['physicalReleaseWindowAdmissionBlocked','physicalReleaseWindowCaptureQueuedOpen','physicalReleaseWindowQueuedContextRetired',
  'physicalReleaseWindowQueuedOpenRetired','physicalReleaseWindowRetireQueuedOpen','physicalReleaseWindowRefreshDecision',
  'physicalReleaseWindowRefreshDecisionReason','physicalReleaseWindowOpen','physicalReleaseWindowClose','physicalReleaseWindowRequest','physicalReleaseWindowToggleRequest'];
 const members=names.map(n=>extract(text,n));
 const a=text.indexOf('static constexpr ULONGLONG kPhysicalReleaseWindowMs'),b=text.indexOf('// ── GP-GBRETURN-1（M1/M3/M4/M5）',a);
 if(a<0||b<a)throw Error('Window declarations drift');
 const originalRegion=text.slice(a,b),needle='static std::atomic<int> g_physicalReleaseWindowStage{0};';
 if(originalRegion.split(needle).length!==2)throw Error('Stage storage binding drift');
 const region=originalRegion.replace(needle,'static FixtureStageAtomic g_physicalReleaseWindowStage{0};');
 const constants=(text.match(/^static constexpr ULONGLONG kT1[^\n]+/gm)??[]).join('\n');
 const observe=(text.match(/^static constexpr ULONGLONG kPhysicalReleaseWindowDutyObserveMs[^\n]+/m)??[])[0];if(!observe)throw Error('Observe deadline missing');
 const decisionAt=text.indexOf('enum class PhysicalRefreshDecision : int {');if(decisionAt<0)throw Error('Refresh decision missing');
 const decision=text.slice(decisionAt,text.indexOf('};',decisionAt)+2);
 const duty=extract(text,'PhysicalReleaseWindowCloseDuty','struct');
 const extra=observe+'\n'+decision+'\n'+duty.code+`
static std::mutex g_physicalReleaseWindowDutyMx;
static PhysicalReleaseWindowCloseDuty g_physicalReleaseWindowDuty;
static std::atomic<unsigned> g_xinputSlotIdentityEpoch{11};
static std::atomic<bool> g_hidHideCycleInFlight{false};
static DWORD g_xinputVirtualEndpointMask=1;
static int refreshCalls=0;
static bool refreshAccepted=false;
static BYTE ymccOpenXInputGetUserIndex(const std::wstring&){return 0xFF;}
static std::vector<std::string> hidHideParseCommandPaths(const std::string& raw,const char*){if(raw!="owned-list")throw std::runtime_error("unexpected list");return {"HID_FIXTURE_OWNED"};}
static bool hidHideContainsPath(const std::vector<std::string>& paths,const std::string& wanted){return std::find(paths.begin(),paths.end(),wanted)!=paths.end();}
static bool physicalReleaseWindowDutyBeginRefresh(const std::wstring&,unsigned long long,ULONGLONG,const char*,std::string* why){++refreshCalls;if(why)*why=refreshAccepted?"accepted":"fixture-begin-refused";return refreshAccepted;}
static void inputHostLogSubmitStats(const char*){}
static bool ymccOpenXInputVisiblePersona(){return false;}
static bool ymccSlotHandoffSchedule(){throw std::runtime_error("unselected slot handoff");}
`;
 let cpp=fs.readFileSync(template,'utf8');
 for(const [marker,value]of [['@@DECLARATIONS@@',constants+'\n'+region],['@@EXTRA_DECLARATIONS@@',extra],['@@METHODS@@',members.map(x=>x.code).join('\n')]]){
  if(cpp.split(marker).length!==2)throw Error('Template marker drift '+marker);cpp=cpp.replace(marker,()=>value);
 }
 if(/@@\w+@@/.test(cpp))throw Error('Unexpanded marker');
 const jsonPath=path.join(repo,'deps/json/json.hpp'),diagnosticHeaderPath=path.join(repo,'native/input_host_diagnostic_dispatch.h');
 const inputs=[{path:source,bytes:sourceBytes.length,sha256:sha(sourceBytes)},pin(template),pin(fileURLToPath(import.meta.url)),pin(consumerPath),pin(jsonPath),pin(diagnosticHeaderPath),pin(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs')),pin(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'))];
 const sourceManifest={source:inputs[0],inputs,newlineNormalization:'CRLF to LF',consumedMembers:[...members,duty].map(({code,...r})=>r),declarationsOriginalSha256:sha(originalRegion),declarationsProjectedSha256:sha(region),
  storageProjection:{original:needle,projected:'static FixtureStageAtomic g_physicalReleaseWindowStage{0};',semantics:'Delegates same std::atomic load/store/CAS and memory-order arguments; store0 emits one controlled fixture producer notification.',notExactProductionDeclaration:true},
  boundary:'Actual entire Close/Request/Toggle/Open and guards unchanged; explicit memory device/clock/log ports plus disclosed atomic storage notification. No real thread scheduling, physical IO or global concurrency proof.'};
 return {cpp,sourceBytes,sourceManifest,expectedCases:[...selection.cases],jsonPath,diagnosticHeaderPath};
}
