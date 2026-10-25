// Actual Win5 HID parser + keyboard evaluator/publisher + merged source state.
// External producer admission is fake memory; no Windows input or device API runs.
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');if(!process.argv[2]||!process.argv[3])throw Error('Usage: <output> <before-main.cpp> [HC-framed-fixtures.json] [source-main.cpp for negative replay]');
const out=path.resolve(process.argv[2]),oldPath=path.resolve(process.argv[3]);fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(process.argv[5]?path.resolve(process.argv[5]):path.join(root,'native/main.cpp'),'utf8'),before=fs.readFileSync(oldPath,'utf8');
const r=spawnSync(process.execPath,[path.join(root,'tools/oem_capability_matrix_fixture.mjs'),path.join(out,'matrix-definitions')],{encoding:'utf8'});if(r.status!==0)throw Error(r.stderr||r.stdout);
const matrix=fs.readFileSync(path.join(out,'matrix-definitions/actual-oem-capability-matrix.cpp'),'utf8'),split=matrix.indexOf('struct Model{');if(split<0)throw Error('No matrix marker');const declarations=matrix.slice(0,split).replace('void appendNativeLifecycleLog(const char*,const json& row){fixtureLog=row;}', 'std::vector<json> sampleLogs;void appendNativeLifecycleLog(const char* event,const json& row){fixtureLog=row;if(std::string(event)=="gpd-back-buttons")sampleLogs.push_back(row);}');
if(process.argv[4])fs.copyFileSync(path.resolve(process.argv[4]),path.join(out,'hc-marshalled-frames.json'));
function fn(s,name){const m=new RegExp('^static [^\\n;]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(s);if(!m)throw Error('Missing '+name);return s.slice(m.index,s.indexOf('\n}',m.index)+2);}
function block(marker,end='\n};'){const i=source.indexOf(marker);if(i<0)throw Error('No '+marker);const e=source.indexOf(end,i);if(e<0)throw Error('No closing '+marker);return source.slice(i,e+end.length);}
const actual=[block('enum class OemProducerId'),block('static constexpr std::array<uint64_t, kOemProducerCount> kOemProducerMasks'),block('struct OemProducerState')];
actual[0]=actual[0].slice(0,actual[0].indexOf(';')+1)+'\nstatic constexpr size_t kOemProducerCount=static_cast<size_t>(OemProducerId::Count);';
const external=String.raw`
static std::mutex g_oemProducerMx;
static std::array<OemProducerState,kOemProducerCount> g_oemProducerStates{};
static std::array<std::atomic<uint64_t>,kOemProducerCount> g_oemProducerAuthorities{};
static std::atomic<int> g_oemBackProfileId{0},g_oemBackState{0},g_oemBackStateKb{0},g_gpdChordSnapPhysical{0},g_msiDinputBackBits{0};
static std::atomic<uint32_t> g_oemKbDownRaw{0},g_gpdBackIfaceGen{2},g_gpdChordPubGen{2},g_gpdChordSnapBaseKey{0},g_gpdChordSnapPrefix{0};
static std::atomic<uint64_t> g_oemKeyMask{0},g_oemKeyMaskHid{0};
static std::atomic<bool> g_gpdBackIfaceReady{true};
static uint32_t g_oemKbDown=0;static bool g_oemMouseLeft=false,g_oemMouseX2=false;
static struct {uint64_t actionGeneration=1;}g_gpdBack;
static bool oemProducerCurrent(OemProducerId source,uint64_t generation){return source==OemProducerId::Gpd && generation==g_gpdBack.actionGeneration;}
static bool oemProducerPublish(OemProducerId source,uint64_t generation,uint64_t mask){
 if(!oemProducerCurrent(source,generation))return false;
 auto& state=g_oemProducerStates[static_cast<size_t>(source)];state.generation=state.nextGeneration=generation;state.physicalMask=mask & kOemProducerMasks[static_cast<size_t>(source)];state.mask=state.physicalMask;
 state.authorityMask=kOemProducerMasks[static_cast<size_t>(source)];g_oemProducerAuthorities[static_cast<size_t>(source)]=state.authorityMask;state.sampled=true;return true;
}
static int msiDinputBackAuthoritativeMask(){return 0;}
`;
const definitions=[fn(source,'oemProducerAuthorityMask'),fn(source,'oemProducerPhysicalBackState'),fn(source,'gpdBackParse'),fn(source,'oemBackHookRecompute'),fn(source,'oemBackMergedDisplayState')];
const cpp='#include <fstream>\n#include <array>\n#include <atomic>\n#include <mutex>\n#include <stdexcept>\n'+declarations+'\n'+actual.join('\n')+'\n'+external+'\n'+source.match(/static constexpr BYTE kGpdBackMask[^;]+;/)[0]+'\n'+definitions.join('\n\n')+'\n'+fn(before,'oemChordEvalRules').replace('oemChordEvalRules(', 'beforeOemChordEvalRules(')+String.raw`
static void reset(){
 for(auto& p:g_oemProducerStates)p={};for(auto& a:g_oemProducerAuthorities)a=0;g_oemBackProfileId=int(OemChordProfileId::GpdWin5);
 g_oemBackState=g_oemBackStateKb=g_gpdChordSnapPhysical=g_msiDinputBackBits=0;g_gpdBackIfaceGen=g_gpdChordPubGen=2;
 g_oemKeyMask=g_oemKeyMaskHid=0;g_oemKbDownRaw=0;g_oemKbDown=0;g_gpdBackIfaceReady=true;
 g_oemMouseLeft=g_oemMouseX2=false;g_gpdBack.actionGeneration=1;
}
int main(){unsigned checks=0;const auto check=[&](bool p,const char* msg){++checks;if(!p)throw std::runtime_error(msg);};
try{
 if(std::ifstream("hc-marshalled-frames.json").good()){
  std::ifstream input("hc-marshalled-frames.json");json fixtures;input>>fixtures;json mismatches=json::array();
  reset();g_gpdBack.actionGeneration=5000;sampleLogs.clear();
  for(const auto& entry:fixtures){
   const std::string hex=entry.at("rawHex");std::vector<BYTE> raw;
   for(size_t i=0;i<hex.size();i+=2)raw.push_back(BYTE(std::stoul(hex.substr(i,2),nullptr,16)));
   const int expected=int(entry.at("left").get<bool>()) | (int(entry.at("right").get<bool>())<<1);
   const uint64_t mask=(entry.at("left").get<bool>()?kOemBitL4:0)|(entry.at("right").get<bool>()?kOemBitR4:0)|(entry.at("modeSwitch").get<bool>()?kOemBitGamepad:0);
   gpdBackParse(raw.data(),raw.size(),5000);
   if(g_oemBackState!=expected || g_oemKeyMaskHid!=mask || oemProducerPhysicalBackState()!=expected || g_oemProducerStates[static_cast<size_t>(OemProducerId::Gpd)].physicalMask!=mask)
    mismatches.push_back({{"frame",entry},{"expectedBack",expected},{"actualBack",g_oemBackState.load()},{"expectedMask",std::to_string(mask)},{"actualMask",std::to_string(g_oemKeyMaskHid.load())}});
   checks+=4;
  }
  std::ofstream("hc-framing-replay.json")<<json{{"fixtures",fixtures.size()},{"mismatchCount",mismatches.size()},{"mismatches",mismatches},{"hardwareIo",false}}.dump(2);
  check(mismatches.empty(),"raw ReadFile frames must match actual HC HidReport marshalling and Gamepad switch semantics");
  check(sampleLogs.size()==fixtures.size(),"first frame and key transitions are logged once");
  const auto& first=sampleLogs.front();check(first.at("reportBodyOffset")==1 && first.at("reportLength")==64 && first.at("actionGeneration")==5000,"source receipt names framing and generation");
  const auto& last=sampleLogs.back();check(last.at("bodyByte7")==0x69 && last.at("bodyByte8")==0x69 && last.at("bodyByte9")==0x69,"only relevant HC body bytes are observed");
  BYTE raw[64]{};raw[0]=0x7f;raw[8]=raw[9]=raw[10]=0x69;const auto n=sampleLogs.size();
  for(int i=0;i<100;++i)gpdBackParse(raw,64,5000);
  check(sampleLogs.size()==n,"unchanged held reports do not spam lifecycle logs");
  raw[8]=raw[9]=raw[10]=0;gpdBackParse(raw,64,5000);
  check(g_oemKeyMaskHid==0&&g_oemBackState==0,"all-key release clears both semantic and rear state");
  raw[8]=0x69;gpdBackParse(raw,64,5000);check(g_oemKeyMaskHid==kOemBitGamepad&&g_oemBackState==0&&oemProducerPhysicalBackState()==0,"Gamepad switch cannot create a rear press");
  raw[8]=0;gpdBackParse(raw,64,5000);check(g_oemKeyMaskHid==0&&g_oemBackState==0,"switch-only release is observable");
  const auto releasedLogs=sampleLogs.size();
  for(size_t length=0;length<=10;++length){raw[8]=raw[9]=raw[10]=0x69;gpdBackParse(raw,length,5000);}
  gpdBackParse(nullptr,64,5000);gpdBackParse(raw,64,4999);
  check(g_oemKeyMaskHid==0&&g_oemBackState==0&&sampleLogs.size()==releasedLogs,"malformed and stale frames neither publish nor log input");
  g_gpdBack.actionGeneration=5001;raw[8]=raw[9]=raw[10]=0;gpdBackParse(raw,64,5001);
  check(sampleLogs.size()==releasedLogs+1&&sampleLogs.back().at("actionGeneration")==5001,"new source generation logs its first neutral frame");
  std::ofstream("sample-log-fixtures.json")<<json(sampleLogs).dump(2);
 }
 for(unsigned round=0;round<100;++round){
  for(int side=0;side<2;++side){reset();const uint32_t key=side==0?kVkF14:kVkF15;const uint64_t canonical=side==0?kOemBitL4:kOemBitR4;const int expected=1<<side;
   g_oemKbDown=key;oemBackHookRecompute();const int keyboardBack=g_oemBackStateKb;check(g_oemKeyMask==canonical,"Win5 fallback canonical key retained");
   BYTE raw[64]{};raw[9+side]=kGpdBackMask;gpdBackParse(raw,64,1);
   check(g_oemBackState==expected,"actual HC-compatible HID side retained");
   check(keyboardBack==g_oemBackState,"Win5 same canonical rear key must have same side in HID and keyboard routes");
   check(oemProducerPhysicalBackState()==expected,"actual producer physical rear aggregation matches HID");
   check(oemBackMergedDisplayState()==0,"authoritative HID suppresses current keyboard duplicate");
   // Explicit per-key authority contract case, not an observed current Win5
   // HID report: current parser publishes full L4/R4 authority by default.
   g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::Gpd)]=canonical;
   check(oemBackMergedDisplayState()==0,"per-key authority uses the same fallback direction");
   // Old keyboard press may remain cached when HID releases: no invented held key.
   raw[9+side]=0;gpdBackParse(raw,64,1);check(oemProducerPhysicalBackState()==0,"HID physical release clears producer");
   check(oemBackMergedDisplayState()==0,"HID release cannot reveal stale keyboard press on the wrong side");
   for(auto& p:g_oemProducerStates)p={};for(auto& a:g_oemProducerAuthorities)a=0;check(oemBackMergedDisplayState()==expected,"fallback returns to the same physical side when HID authority is withdrawn");
   g_gpdBackIfaceGen=3;check(oemBackMergedDisplayState()==0,"old keyboard generation revoked without a new callback");
   g_gpdBackIfaceReady=false;g_oemKbDown=key;oemBackHookRecompute();check(g_oemKeyMask==0&&g_oemBackStateKb==0,"closed GPD source cannot publish fallback");
   g_gpdBackIfaceReady=true;g_oemKbDown=0;oemBackHookRecompute();check(g_oemBackStateKb==0,"real release remains neutral");
   g_oemKbDown=key;oemBackHookRecompute();check(g_oemBackStateKb==expected,"fresh generation real press recovers correct side");
  }
 }
 // Every unrelated chord profile must retain exact prior action and side semantics.
 for(int id=0;id<int(std::size(kOemProfiles));++id){const auto& p=oemChordProfileFor(id);
  for(int i=0;i<p.count;++i)for(bool ready:{false,true})for(uint32_t prefix:{0u,uint32_t(kVkWin),uint32_t(kVkCtrl|kVkShift)}){
   uint64_t a=0,b=0;int sa=0,sb=0,pa=0,pb=0;uint32_t ba=0,bb=0,xa=0,xb=0;
   const auto& r=p.rules[i];beforeOemChordEvalRules(id,r.keys|prefix,r.mouseChord,ready,&a,&sa,&pa,&ba,&xa);
   oemChordEvalRules(id,r.keys|prefix,r.mouseChord,ready,&b,&sb,&pb,&bb,&xb);
   check(a==b&&pa==pb&&ba==bb&&xa==xb,"canonical IDs/physical diagnostics/prefixes are unchanged in every profile");
   if(id!=int(OemChordProfileId::GpdWin5)&&id!=int(OemChordProfileId::GpdLegacy))check(sa==sb,"all non-GPD rear output directions unchanged");
  }
 }
 reset();BYTE raw[64]{};raw[9]=kGpdBackMask;gpdBackParse(raw,64,999);check(g_oemBackState==0&&g_oemKeyMaskHid==0,"stale HID generation is still rejected");
 gpdBackParse(raw,9,1);check(g_oemBackState==0,"short HID report cannot invent state");
 std::cout<<"WIN5_REAR_ROUTE_PARITY_OK checks="<<checks<<" actual_hid_keyboard_publish_merge=true hardware_io=none external_producer_admission=fake\n";return 0;
}catch(const std::exception&e){std::cerr<<"WIN5_REAR_ROUTE_PARITY_FAIL "<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'actual-win5-rear-source-parity.cpp'),cpp);const sha=s=>crypto.createHash('sha256').update(s).digest('hex');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,mainSha256:sha(source),beforeMainSha256:sha(before),framingFixturesSha256:process.argv[4]?sha(fs.readFileSync(path.resolve(process.argv[4]))):null,sourceOverride:process.argv[5]?path.resolve(process.argv[5]):null,functions:definitions.map((s,i)=>({name:['oemProducerAuthorityMask','oemProducerPhysicalBackState','gpdBackParse','oemBackHookRecompute','oemBackMergedDisplayState'][i],sha256:sha(s)})),testSha256:sha(cpp),hardwareIo:false,realProducerAdmission:false},null,2));console.log('Generated actual HID/keyboard source parity fixture; device APIs absent.');